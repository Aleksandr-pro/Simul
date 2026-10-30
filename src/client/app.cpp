#include "razlom/client/app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "razlom/core/rng.hpp"
#include "sokol_time.h"

namespace razlom::client {
namespace {

constexpr float kMouseSensitivity = 0.0022f;
constexpr float kTouchSensitivity = 0.0045f;
constexpr float kWalkSpeed = 5.4f;
constexpr float kSprintSpeed = 8.4f;
constexpr float kFlySpeed = 12.0f;

std::string trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) --end;
    return text.substr(begin, end - begin);
}

bool parseAddress(const std::string& raw, std::string& host, uint16_t& port) {
    std::string text = trim(raw);
    if (text.empty()) return false;
    const std::string scheme = "razlom://";
    if (text.rfind(scheme, 0) == 0) text = text.substr(scheme.size());
    const size_t slash = text.find('/');
    if (slash != std::string::npos) text = text.substr(0, slash);

    port = net::kDefaultPort;
    const size_t colon = text.rfind(':');
    if (colon != std::string::npos) {
        const std::string portText = text.substr(colon + 1);
        const int value = std::atoi(portText.c_str());
        if (value <= 0 || value > 65535) return false;
        port = static_cast<uint16_t>(value);
        text = text.substr(0, colon);
    }
    host = text;
    return !host.empty();
}

}  // namespace

bool App::init() {
    if (!font_.build(30.0f)) {
        std::printf("[разлом] не удалось построить атлас шрифта\n");
        return false;
    }
    if (!renderer_.init()) {
        std::printf("[разлом] рендерер не инициализирован: %s\n", renderer_.lastError().c_str());
        return false;
    }
    renderer_.initFontTexture(font_.pixels(), FontAtlas::kSize);
    if (!hud_.init(&font_)) return false;

    hotbar_ = {kStone, kBrick, kPlanks, kLog, kSand, kGlass, kStoneBrick, kDirt, kTnt};
    screen_ = Screen::Menu;
    screenWidth_ = sapp_width();
    screenHeight_ = sapp_height();
    renderer_.setScreenSize(screenWidth_, screenHeight_);
    return true;
}

void App::cleanup() {
    disconnectAll();
    renderer_.shutdown();
}

void App::disconnectAll() {
    client_.disconnect();
    if (server_) {
        server_->stop();
        server_.reset();
    }
    hosting_ = false;
    worldReady_ = false;
    roomLink_.clear();
    roomCode_.clear();
    players_.clear();
    debris_.clear();
    hasHighlight_ = false;
}

void App::enterMenu() {
    disconnectAll();
    screen_ = Screen::Menu;
    status_.clear();
    error_.clear();
    if (mouseLocked_) {
        sapp_lock_mouse(false);
        mouseLocked_ = false;
    }
}

void App::startHosting() {
    disconnectAll();
    error_.clear();
    status_ = "Создаём мир…";

    SessionConfig config;
    Rng rng(static_cast<uint32_t>(stm_ms(stm_now())) ^ 0x5f3759dfu);
    config.seed = rng.next();
    config.mode = GameMode::Sandbox;
    config.maxPlayers = 4;
    config.tickRate = 20;

    server_ = std::make_unique<net::Server>(config);
    uint16_t port = net::kDefaultPort;
    bool started = false;
    for (int attempt = 0; attempt < 8 && !started; ++attempt, ++port) {
        started = server_->start(port);
    }
    if (!started) {
        error_ = "Не удалось открыть порт для мира";
        server_.reset();
        screen_ = Screen::Menu;
        return;
    }

    hosting_ = true;
    roomLink_ = server_->joinLink();
    roomCode_ = server_->roomCode();
    maxPlayers_ = config.maxPlayers;

    if (!client_.connect("127.0.0.1", server_->port(), playerName_)) {
        error_ = "Не удалось подключиться к своему же миру";
        disconnectAll();
        screen_ = Screen::Menu;
        return;
    }
    joinRequested_ = true;
    joinTimer_ = 0.0f;
    screen_ = Screen::Loading;
    status_ = "Мир создаётся одной кнопкой…";
}

void App::startJoining(const std::string& address) {
    std::string host;
    uint16_t port = 0;
    if (!parseAddress(address, host, port)) {
        error_ = "Введите адрес вида 192.168.1.10:47800 или ссылку razlom://…";
        return;
    }
    disconnectAll();
    error_.clear();
    status_ = "Подключаемся к " + host + ":" + std::to_string(port) + "…";
    joinTimer_ = 0.0f;
    joinRequested_ = true;
    screen_ = Screen::Loading;
    client_.connect(host, port, playerName_);
}

void App::beginWorld(uint32_t seed, GameMode mode, const std::vector<BlockChange>& history) {
    worldSeed_ = seed;
    mode_ = mode;

    world_ = World(seed);
    generateWorld(world_, seed);
    for (const BlockChange& change : history) {
        world_.applyRemote(change.x, change.y, change.z, change.block);
    }
    renderer_.resetWorld(world_);

    players_.clear();
    debris_.clear();
    log_.clear();
    worldReady_ = true;
    pendingWelcome_ = false;

    local_ = LocalPlayer{};
    local_.pos = spawnPosition(world_);
    local_.flying = (mode == GameMode::Sandbox);
    local_.health = 100;
    local_.dead = false;

    // Если сервер уже знает нашу позицию — берём её.
    for (const net::PlayerSnapshot& p : playersFromWelcome_) {
        if (p.id == selfId_) {
            local_.pos = p.pos;
            local_.yaw = p.yaw;
            break;
        }
    }

    addLog(std::string("Мир загружен: ") + gameModeName(mode));
    screen_ = Screen::Loading;
    status_ = "Строим чанки…";
}

void App::addLog(const std::string& text) {
    log_.push_back(text);
    if (log_.size() > 5) log_.erase(log_.begin());
}

void App::addToast(const std::string& text) {
    toast_ = text;
    toastTimer_ = 2.6f;
}

void App::look(float dx, float dy) {
    local_.yaw -= dx * kMouseSensitivity;
    local_.pitch -= dy * kMouseSensitivity;
    local_.pitch = clampf(local_.pitch, -1.53f, 1.53f);
}

void App::handleNetwork(float dt) {
    net::NetInbox inbox;
    inbox.clear();
    client_.poll(inbox);

    if (inbox.welcome) {
        selfId_ = inbox.playerId;
        playersFromWelcome_ = inbox.players;
        players_ = inbox.players;
        beginWorld(inbox.seed, static_cast<GameMode>(inbox.mode), inbox.history);
        maxPlayers_ = 4;
    }

    if (inbox.playersUpdated) players_ = inbox.players;
    if (!inbox.debris.empty()) debris_ = inbox.debris;
    else if (mode_ != GameMode::Sandbox) debris_.clear();

    for (const BlockChange& change : inbox.changes) {
        world_.applyRemote(change.x, change.y, change.z, change.block);
    }

    for (const SessionEvent& event : inbox.events) {
        switch (event.kind) {
            case SessionEventKind::Chat:
            case SessionEventKind::Damage:
            case SessionEventKind::Win:
                addLog(event.text);
                addToast(event.text);
                break;
            case SessionEventKind::Join:
            case SessionEventKind::Leave:
                addLog(event.text);
                break;
            case SessionEventKind::Collapse:
                addLog(event.text);
                break;
            case SessionEventKind::Explosion:
                addLog(event.text);
                break;
            default:
                addLog(event.text);
                break;
        }
    }

    if (inbox.disconnected && screen_ != Screen::Menu) {
        error_ = inbox.disconnectReason.empty() ? "Соединение с хостом потеряно" : inbox.disconnectReason;
        enterMenu();
        return;
    }

    const net::ClientState state = client_.state();
    if (state == net::ClientState::Failed) {
        error_ = client_.error().empty() ? "Не удалось подключиться к миру" : client_.error();
        enterMenu();
        return;
    }

    // своё здоровье/состояние — из снимка сервера
    for (const net::PlayerSnapshot& p : players_) {
        if (p.id != selfId_) continue;
        local_.health = p.health;
        local_.dead = (p.flags & net::kFlagDead) != 0;
        if ((p.pos - local_.pos).lengthSq() > 2.5f * 2.5f && screen_ == Screen::Playing) {
            local_.pos = p.pos;
        }
        break;
    }

    if (screen_ == Screen::Loading) {
        joinTimer_ += dt;
        if (joinTimer_ > 10.0f) {
            error_ = "Хост не ответил за 10 секунд";
            enterMenu();
        }
    }
}

void App::updateMovement(float dt) {
    if (local_.dead) {
        local_.vel = Vec3{};
        return;
    }

    const Camera camera{local_.pos + Vec3{0.0f, kEyeHeight, 0.0f}, local_.yaw, local_.pitch};
    (void)camera;

    Vec3 forwardFlat{-std::sin(local_.yaw), 0.0f, -std::cos(local_.yaw)};
    Vec3 rightFlat{std::cos(local_.yaw), 0.0f, -std::sin(local_.yaw)};

    float moveForward = 0.0f;
    float moveRight = 0.0f;
    if (keys_[SAPP_KEYCODE_W] || keys_[SAPP_KEYCODE_UP]) moveForward += 1.0f;
    if (keys_[SAPP_KEYCODE_S] || keys_[SAPP_KEYCODE_DOWN]) moveForward -= 1.0f;
    if (keys_[SAPP_KEYCODE_D] || keys_[SAPP_KEYCODE_RIGHT]) moveRight += 1.0f;
    if (keys_[SAPP_KEYCODE_A] || keys_[SAPP_KEYCODE_LEFT]) moveRight -= 1.0f;
    moveForward += stickDY_;
    moveRight += stickDX_;

    Vec3 wish = forwardFlat * moveForward + rightFlat * moveRight;
    if (wish.lengthSq() > 1.0f) wish = wish.normalized();
    const float wishLen = std::min(1.0f, wish.length());

    if (local_.flying) {
        Vec3 velocity = wish * (kFlySpeed * wishLen);
        if (keys_[SAPP_KEYCODE_SPACE] || jumpRequested_) velocity.y += kFlySpeed;
        if (keys_[SAPP_KEYCODE_LEFT_SHIFT] || keys_[SAPP_KEYCODE_LEFT_CONTROL]) velocity.y -= kFlySpeed;
        const MoveResult result = movePlayer(world_, local_.pos, velocity, dt, true);
        local_.pos = result.pos;
        local_.onGround = false;
    } else {
        const bool sprint = keys_[SAPP_KEYCODE_LEFT_SHIFT];
        const float speed = sprint ? kSprintSpeed : kWalkSpeed;
        Vec3 velocity = wish * (speed * wishLen);
        velocity.y = local_.vel.y - kGravity * dt;
        if ((keys_[SAPP_KEYCODE_SPACE] || jumpRequested_) && local_.onGround) {
            velocity.y = kJumpSpeed;
        }
        velocity.y = std::max(velocity.y, -58.0f);
        const MoveResult result = movePlayer(world_, local_.pos, velocity, dt, false);
        local_.pos = result.pos;
        local_.onGround = result.onGround;
        if (result.onGround && velocity.y < 0.0f) velocity.y = 0.0f;
        local_.vel = velocity;
    }
    jumpRequested_ = false;

    if (local_.pos.y < -8.0f) {
        local_.pos = spawnPosition(world_);
        local_.vel = Vec3{};
        addToast("Вы упали в бездну — вернули на спавн");
    }
}

void App::sendInput(float dt) {
    if (!client_.connected()) return;
    inputAccumulator_ += dt;
    if (inputAccumulator_ < 1.0f / 25.0f) return;
    inputAccumulator_ = 0.0f;

    uint8_t flags = 0;
    if (local_.flying) flags |= net::kFlagFlying;
    if (local_.onGround) flags |= net::kFlagOnGround;
    if (local_.dead) flags |= net::kFlagDead;
    if (keys_[SAPP_KEYCODE_LEFT_SHIFT]) flags |= net::kFlagSprint;
    client_.sendInput(local_.pos, local_.yaw, local_.pitch, flags, local_.slot);
}

void App::tryEdit(bool breakBlock) {
    if (!worldReady_ || local_.dead || !client_.connected()) return;
    const Vec3 eye = local_.pos + Vec3{0.0f, kEyeHeight, 0.0f};
    const Camera camera{eye, local_.yaw, local_.pitch};
    const RayHit hit = raycastVoxel(world_, eye, camera.forward(), kReachDistance);
    if (!hit.hit) return;

    if (breakBlock) {
        client_.sendBreak(hit.block);
    } else {
        const uint8_t block = hotbar_[static_cast<size_t>(local_.slot) % hotbar_.size()];
        client_.sendPlace(hit.previous, block);
    }
}

void App::triggerExplosion() {
    if (!worldReady_ || !client_.connected()) return;
    const Vec3 eye = local_.pos + Vec3{0.0f, kEyeHeight, 0.0f};
    const Camera camera{eye, local_.yaw, local_.pitch};
    const RayHit hit = raycastVoxel(world_, eye, camera.forward(), kReachDistance);
    if (!hit.hit) return;
    const Vec3 center = hit.block.toVec3() + Vec3{0.5f, 0.5f, 0.5f};
    client_.sendExplode(center, 4.2f);
}

void App::updatePlaying(float dt) {
    updateMovement(dt);

    // подсветка блока под прицелом
    hasHighlight_ = false;
    if (worldReady_) {
        const Vec3 eye = local_.pos + Vec3{0.0f, kEyeHeight, 0.0f};
        const Camera camera{eye, local_.yaw, local_.pitch};
        const RayHit hit = raycastVoxel(world_, eye, camera.forward(), kReachDistance);
        if (hit.hit) {
            hasHighlight_ = true;
            highlight_ = hit.block;
        }
    }

    breakCooldown_ -= dt;
    placeCooldown_ -= dt;
    if (breakHeld_ && breakCooldown_ <= 0.0f) {
        tryEdit(true);
        breakCooldown_ = 0.24f;
    }
    if (placeHeld_ && placeCooldown_ <= 0.0f) {
        tryEdit(false);
        placeCooldown_ = 0.2f;
    }

    sendInput(dt);

    pingAccumulator_ += dt;
    if (pingAccumulator_ > 1.0f) {
        pingAccumulator_ = 0.0f;
        client_.sendPing();
    }

    if (toastTimer_ > 0.0f) toastTimer_ -= dt;
}

void App::frame() {
    const float dt = clampf(static_cast<float>(stm_sec(stm_laptime(&lastFrameTime_))), 0.0f, 0.05f);
    time_ += dt;
    fpsAccumulator_ += dt;
    ++fpsFrames_;
    if (fpsAccumulator_ >= 0.5f) {
        fps_ = static_cast<int>(static_cast<float>(fpsFrames_) / fpsAccumulator_);
        fpsAccumulator_ = 0.0f;
        fpsFrames_ = 0;
    }

    screenWidth_ = sapp_width();
    screenHeight_ = sapp_height();
    renderer_.setScreenSize(screenWidth_, screenHeight_);

    handleNetwork(dt);

    if (screen_ == Screen::Loading && client_.connected() && worldReady_) {
        renderer_.buildChunks(world_, 64);
        const int pending = renderer_.pendingChunks(world_);
        loadingTotal_ = std::max(loadingTotal_, pending + 1);
        loadingProgress_ = 1.0f - static_cast<float>(pending) / static_cast<float>(loadingTotal_);
        if (pending == 0) {
            screen_ = Screen::Playing;
            loadingProgress_ = 1.0f;
            if (!touchMode_ && !mouseLocked_) {
                sapp_lock_mouse(true);
                mouseLocked_ = true;
            }
            addToast(hosting_ ? "Мир готов — зовите друзей по ссылке" : "Вы в мире");
        }
    }

    if (screen_ == Screen::Playing) updatePlaying(dt);

    // --- интерфейс ---
    HudInput input;
    input.mouseX = mouseX_;
    input.mouseY = mouseY_;
    input.down = hudDown_ || breakHeld_;
    input.pressed = hudClick_;
    input.released = false;
    input.touchMode = touchMode_;
    input.width = screenWidth_;
    input.height = screenHeight_;

    HudModel model;
    buildHudModel(model, input);

    HudResult result;
    hud_.draw(model, input, result);
    hudClick_ = false;
    if (result.action != HudAction::None) handleHudAction(result);

    FrameData frameData;
    frameData.camera.position = local_.pos + Vec3{0.0f, kEyeHeight, 0.0f};
    frameData.camera.yaw = local_.yaw;
    frameData.camera.pitch = local_.pitch;
    frameData.camera.fovDegrees = 74.0f;
    frameData.world = (screen_ == Screen::Playing || screen_ == Screen::Paused) && worldReady_ ? &world_ : nullptr;
    frameData.debris = &debris_;
    frameData.players = &players_;
    frameData.selfPlayerId = selfId_;
    frameData.hasHighlight = hasHighlight_ && screen_ == Screen::Playing && worldReady_;
    frameData.highlight = highlight_;
    frameData.ui = &hud_.vertices();
    frameData.time = time_;
    frameData.maxRemesh = 8;
    frameData.showSky = true;
    renderer_.render(frameData);
}

void App::buildHudModel(HudModel& model, const HudInput& input) {
    (void)input;
    model.screen = screen_;
    model.roomLink = roomLink_;
    model.roomCode = roomCode_;
    model.joinAddress = joinAddress_;
    model.error = error_;
    model.status = status_;
    model.loadingProgress = loadingProgress_;
    model.modeName = gameModeName(mode_);
    model.modeTagline = gameModeTagline(mode_);
    model.creative = (mode_ == GameMode::Sandbox);
    model.playerName = playerName_;
    model.health = local_.health;
    model.dead = local_.dead;
    model.selectedSlot = local_.slot;
    model.fps = fps_;
    model.ping = client_.latencyMs();
    model.playersOnline = static_cast<int>(players_.size());
    model.maxPlayers = maxPlayers_;
    model.log = log_;
    model.toast = toast_;
    model.toastTimer = toastTimer_;
    model.showTouch = touchMode_;

    model.players.clear();
    for (const net::PlayerSnapshot& p : players_) {
        HudPlayerRow row;
        row.name = p.name;
        row.health = p.health;
        row.self = p.id == selfId_;
        row.alive = (p.flags & net::kFlagDead) == 0;
        model.players.push_back(row);
    }

    model.hotbar.clear();
    for (uint8_t block : hotbar_) {
        HudSlot slot;
        slot.block = block;
        slot.label = blockDef(block).name;
        model.hotbar.push_back(slot);
    }
}

void App::handleHudAction(const HudResult& result) {
    switch (result.action) {
        case HudAction::Host:
            startHosting();
            break;
        case HudAction::OpenJoin:
            screen_ = Screen::Join;
            error_.clear();
            status_.clear();
            joinAddress_.clear();
            if (mouseLocked_) {
                sapp_lock_mouse(false);
                mouseLocked_ = false;
            }
            if (touchMode_) sapp_show_keyboard(true);
            break;
        case HudAction::JoinSubmit:
            if (touchMode_) sapp_show_keyboard(false);
            startJoining(joinAddress_);
            break;
        case HudAction::Back:
            if (touchMode_) sapp_show_keyboard(false);
            screen_ = Screen::Menu;
            error_.clear();
            status_.clear();
            break;
        case HudAction::Catalog:
            screen_ = Screen::Catalog;
            break;
        case HudAction::Resume:
            screen_ = Screen::Playing;
            if (!touchMode_ && !mouseLocked_) {
                sapp_lock_mouse(true);
                mouseLocked_ = true;
            }
            break;
        case HudAction::CopyLink:
            if (!roomLink_.empty()) {
                sapp_set_clipboard_string(roomLink_.c_str());
                addToast("Ссылка скопирована: " + roomLink_);
            } else {
                addToast("Ссылка есть только у хоста мира");
            }
            break;
        case HudAction::QuitToMenu:
            enterMenu();
            break;
        case HudAction::ModeSandbox:
            client_.sendSetMode(GameMode::Sandbox);
            screen_ = Screen::Playing;
            break;
        case HudAction::ModeLastStanding:
            client_.sendSetMode(GameMode::LastStanding);
            screen_ = Screen::Playing;
            break;
        case HudAction::ModeBaseDefense:
            client_.sendSetMode(GameMode::BaseDefense);
            screen_ = Screen::Playing;
            break;
        case HudAction::Respawn:
            client_.sendRespawn();
            local_.dead = false;
            break;
        case HudAction::Explode:
            triggerExplosion();
            break;
        case HudAction::Attack:
            client_.sendAttack();
            break;
        case HudAction::ToggleFly:
            if (mode_ == GameMode::Sandbox) local_.flying = !local_.flying;
            break;
        case HudAction::SelectSlot:
            if (result.slot >= 0 && result.slot < static_cast<int>(hotbar_.size())) {
                local_.slot = static_cast<uint8_t>(result.slot);
            }
            break;
        case HudAction::None:
            break;
    }
}

void App::handleKeyDown(const sapp_event* ev) {
    if (ev->key_code >= 512) return;
    const bool repeat = ev->key_repeat;
    keys_[ev->key_code] = true;

    if (screen_ == Screen::Join) {
        if (ev->key_code == SAPP_KEYCODE_ENTER) {
            if (touchMode_) sapp_show_keyboard(false);
            startJoining(joinAddress_);
        } else if (ev->key_code == SAPP_KEYCODE_BACKSPACE) {
            if (!joinAddress_.empty()) joinAddress_.pop_back();
        } else if (ev->key_code == SAPP_KEYCODE_ESCAPE) {
            screen_ = Screen::Menu;
            if (touchMode_) sapp_show_keyboard(false);
        }
        return;
    }

    if (screen_ == Screen::Playing) {
        if (ev->key_code == SAPP_KEYCODE_ESCAPE) {
            screen_ = Screen::Paused;
            if (mouseLocked_) {
                sapp_lock_mouse(false);
                mouseLocked_ = false;
            }
            breakHeld_ = false;
            placeHeld_ = false;
            return;
        }
        if (repeat) return;
        if (ev->key_code >= SAPP_KEYCODE_1 && ev->key_code <= SAPP_KEYCODE_9) {
            local_.slot = static_cast<uint8_t>(ev->key_code - SAPP_KEYCODE_1);
        } else if (ev->key_code == SAPP_KEYCODE_F) {
            if (mode_ == GameMode::Sandbox) {
                local_.flying = !local_.flying;
                addToast(local_.flying ? "Полёт включён" : "Полёт выключен");
            }
        } else if (ev->key_code == SAPP_KEYCODE_Q) {
            client_.sendAttack();
        } else if (ev->key_code == SAPP_KEYCODE_E) {
            triggerExplosion();
        } else if (ev->key_code == SAPP_KEYCODE_TAB) {
            screen_ = Screen::Paused;
        }
        return;
    }

    if (screen_ == Screen::Paused && ev->key_code == SAPP_KEYCODE_ESCAPE) {
        screen_ = Screen::Playing;
        if (!touchMode_ && !mouseLocked_) {
            sapp_lock_mouse(true);
            mouseLocked_ = true;
        }
        return;
    }

    if (screen_ == Screen::Menu && ev->key_code == SAPP_KEYCODE_ESCAPE) {
        // ничего
    }
}

void App::handleKeyUp(const sapp_event* ev) {
    if (ev->key_code < 512) keys_[ev->key_code] = false;
}

void App::handleTouchBegan(const sapp_event* ev) {
    touchMode_ = true;
    const TouchLayout& layout = hud_.layout(screenWidth_, screenHeight_);

    for (int i = 0; i < ev->num_touches; ++i) {
        const sapp_touchpoint& touch = ev->touches[i];
        if (!touch.changed) continue;
        const float x = touch.pos_x;
        const float y = touch.pos_y;
        mouseX_ = x;
        mouseY_ = y;

        if (screen_ == Screen::Menu || screen_ == Screen::Join || screen_ == Screen::Catalog ||
            screen_ == Screen::Paused) {
            hudClick_ = true;
            hudDown_ = true;
            continue;
        }
        if (screen_ != Screen::Playing) continue;

        // кнопки действий
        if (layout.breakBlock.contains(x, y)) {
            breakHeld_ = true;
            breakCooldown_ = 0.0f;
            continue;
        }
        if (layout.placeBlock.contains(x, y)) {
            placeHeld_ = true;
            placeCooldown_ = 0.0f;
            continue;
        }
        if (layout.jump.contains(x, y)) {
            jumpRequested_ = true;
            continue;
        }
        if (layout.explode.contains(x, y)) {
            triggerExplosion();
            continue;
        }
        if (layout.fly.contains(x, y)) {
            if (mode_ == GameMode::Sandbox) local_.flying = !local_.flying;
            continue;
        }
        for (int slotIndex = 0; slotIndex < 9; ++slotIndex) {
            if (layout.hotbar[slotIndex].contains(x, y)) {
                local_.slot = static_cast<uint8_t>(slotIndex);
                break;
            }
        }
        if (layout.stick.contains(x, y)) {
            stickTouch_ = touch.identifier;
            stickOriginX_ = layout.stick.cx();
            stickOriginY_ = layout.stick.cy();
            continue;
        }
        // правая половина — обзор
        if (x > static_cast<float>(screenWidth_) * 0.35f && lookTouch_ == 0 && stickTouch_ != touch.identifier) {
            lookTouch_ = touch.identifier;
            lookLastX_ = x;
            lookLastY_ = y;
        }
    }
}

void App::handleTouchMoved(const sapp_event* ev) {
    for (int i = 0; i < ev->num_touches; ++i) {
        const sapp_touchpoint& touch = ev->touches[i];
        const float x = touch.pos_x;
        const float y = touch.pos_y;

        if (touch.identifier == stickTouch_) {
            const float dx = x - stickOriginX_;
            const float dy = y - stickOriginY_;
            const float radius = 70.0f * hud_.scale();
            stickDX_ = clampf(dx / radius, -1.0f, 1.0f);
            stickDY_ = clampf(-dy / radius, -1.0f, 1.0f);
            const float len = std::sqrt(stickDX_ * stickDX_ + stickDY_ * stickDY_);
            if (len < 0.16f) {
                stickDX_ = 0.0f;
                stickDY_ = 0.0f;
            }
        } else if (touch.identifier == lookTouch_) {
            const float dx = x - lookLastX_;
            const float dy = y - lookLastY_;
            local_.yaw -= dx * kTouchSensitivity;
            local_.pitch -= dy * kTouchSensitivity;
            local_.pitch = clampf(local_.pitch, -1.53f, 1.53f);
            lookLastX_ = x;
            lookLastY_ = y;
        }
        mouseX_ = x;
        mouseY_ = y;
    }
}

void App::handleTouchEnded(const sapp_event* ev) {
    if (screen_ == Screen::Menu || screen_ == Screen::Join || screen_ == Screen::Catalog || screen_ == Screen::Paused) {
        hudDown_ = false;
    }
    for (int i = 0; i < ev->num_touches; ++i) {
        const sapp_touchpoint& touch = ev->touches[i];
        if (!touch.changed) continue;
        if (touch.identifier == stickTouch_) {
            stickTouch_ = 0;
            stickDX_ = 0.0f;
            stickDY_ = 0.0f;
        } else if (touch.identifier == lookTouch_) {
            lookTouch_ = 0;
        }
    }
    // отпускание кнопок действий: если ни один палец не на кнопке — сбрасываем
    bool anyOnBreak = false;
    bool anyOnPlace = false;
    const TouchLayout& layout = hud_.layout(screenWidth_, screenHeight_);
    for (int i = 0; i < ev->num_touches; ++i) {
        const sapp_touchpoint& touch = ev->touches[i];
        if (touch.changed) continue;
        if (layout.breakBlock.contains(touch.pos_x, touch.pos_y)) anyOnBreak = true;
        if (layout.placeBlock.contains(touch.pos_x, touch.pos_y)) anyOnPlace = true;
    }
    if (!anyOnBreak) breakHeld_ = false;
    if (!anyOnPlace) placeHeld_ = false;
}

void App::onEvent(const sapp_event* ev) {
    switch (ev->type) {
        case SAPP_EVENTTYPE_KEY_DOWN: handleKeyDown(ev); break;
        case SAPP_EVENTTYPE_KEY_UP: handleKeyUp(ev); break;
        case SAPP_EVENTTYPE_CHAR: {
            if (screen_ == Screen::Join && ev->char_code >= 32 && ev->char_code < 127 &&
                joinAddress_.size() < 64) {
                joinAddress_.push_back(static_cast<char>(ev->char_code));
            }
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_MOVE: {
            mouseX_ = ev->mouse_x;
            mouseY_ = ev->mouse_y;
            if (screen_ == Screen::Playing && sapp_mouse_locked()) {
                look(ev->mouse_dx, ev->mouse_dy);
            }
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_DOWN: {
            mouseX_ = ev->mouse_x;
            mouseY_ = ev->mouse_y;
            if (screen_ == Screen::Playing) {
                if (!sapp_mouse_locked() && !touchMode_) {
                    sapp_lock_mouse(true);
                    mouseLocked_ = true;
                    break;
                }
                if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
                    breakHeld_ = true;
                    breakCooldown_ = 0.0f;
                } else if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
                    placeHeld_ = true;
                    placeCooldown_ = 0.0f;
                } else {
                    triggerExplosion();
                }
            } else {
                hudClick_ = true;
                hudDown_ = true;
            }
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_UP: {
            if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) breakHeld_ = false;
            if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT) placeHeld_ = false;
            hudDown_ = false;
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_SCROLL: {
            if (screen_ == Screen::Playing && !hotbar_.empty()) {
                int slot = static_cast<int>(local_.slot);
                slot += ev->scroll_y > 0.0f ? -1 : 1;
                slot = clampi(slot, 0, static_cast<int>(hotbar_.size()) - 1);
                local_.slot = static_cast<uint8_t>(slot);
            }
            break;
        }
        case SAPP_EVENTTYPE_TOUCHES_BEGAN: handleTouchBegan(ev); break;
        case SAPP_EVENTTYPE_TOUCHES_MOVED: handleTouchMoved(ev); break;
        case SAPP_EVENTTYPE_TOUCHES_ENDED:
        case SAPP_EVENTTYPE_TOUCHES_CANCELLED:
            handleTouchEnded(ev);
            break;
        case SAPP_EVENTTYPE_RESIZED:
            screenWidth_ = ev->window_width;
            screenHeight_ = ev->window_height;
            renderer_.setScreenSize(screenWidth_, screenHeight_);
            break;
        default:
            break;
    }
}

}  // namespace razlom::client
