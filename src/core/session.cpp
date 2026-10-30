#include "razlom/core/session.hpp"

#include <algorithm>
#include <cmath>

#include "razlom/core/rng.hpp"

namespace razlom {
namespace {

constexpr size_t kMaxHistory = 150000;
constexpr size_t kMaxDebris = 640;
constexpr float kExplosionPlayerDamage = 78.0f;

std::string sanitize(const std::string& input, size_t maxLen) {
    std::string out;
    out.reserve(std::min(input.size(), maxLen));
    for (char c : input) {
        if (out.size() >= maxLen) break;
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || u == 127) continue;
        out.push_back(c);
    }
    if (out.empty()) out = "Игрок";
    return out;
}

}  // namespace

const char* gameModeName(GameMode mode) {
    switch (mode) {
        case GameMode::Sandbox: return "Песочница";
        case GameMode::LastStanding: return "Последний выживший";
        case GameMode::BaseDefense: return "Защита базы";
    }
    return "Песочница";
}

const char* gameModeTagline(GameMode mode) {
    switch (mode) {
        case GameMode::Sandbox: return "креатив, полёт, бесконечные блоки";
        case GameMode::LastStanding: return "PvP, здоровье, респавн";
        case GameMode::BaseDefense: return "каркас режима — собирается в редакторе";
    }
    return "";
}

Session::Session(const SessionConfig& config) : config_(config), world_(config.seed) {
    history_.reserve(4096);
    historyIndex_.reserve(8192);
    reset(config.seed, config.mode);
}

void Session::reset(uint32_t seed, GameMode mode) {
    config_.seed = seed;
    config_.mode = mode;
    generateWorld(world_, seed);
    world_.clearChanges();

    history_.clear();
    historyIndex_.clear();
    fluids_.clear();
    fire_.clear();
    debris_.clear();
    solves_.clear();
    explosions_.clear();
    pendingChanges_.clear();
    events_.clear();

    spawnPoint_ = spawnPosition(world_);
    tickIndex_ = 0;
    simAccumulator_ = 0.0f;
    modeTimer_ = 0.0f;
    winResetTimer_ = -1.0f;
    winnerId_ = 0;

    for (Player& p : players_) respawn(p);
}

uint32_t Session::addPlayer(const std::string& name, std::string* error) {
    if (static_cast<int>(players_.size()) >= config_.maxPlayers) {
        if (error) *error = "Мир заполнен (максимум " + std::to_string(config_.maxPlayers) + " игроков)";
        return 0;
    }
    Player p;
    p.id = nextPlayerId_++;
    p.name = sanitize(name, 16);
    const int index = static_cast<int>(players_.size());
    const float angle = static_cast<float>(index) * 2.0f * kPi / 4.0f;
    p.pos = spawnPoint_ + Vec3{std::cos(angle) * 2.5f, 0.0f, std::sin(angle) * 2.5f};
    p.yaw = angle;
    p.health = 100;
    p.flags = creative() ? net::kFlagFlying : 0;
    p.slot = 1;
    players_.push_back(p);

    SessionEvent ev;
    ev.kind = SessionEventKind::Join;
    ev.text = p.name + " в мире";
    ev.playerId = p.id;
    events_.push_back(ev);
    if (error) error->clear();
    return p.id;
}

void Session::removePlayer(uint32_t id) {
    auto it = std::find_if(players_.begin(), players_.end(), [id](const Player& p) { return p.id == id; });
    if (it == players_.end()) return;
    SessionEvent ev;
    ev.kind = SessionEventKind::Leave;
    ev.text = it->name + " покинул мир";
    ev.playerId = id;
    events_.push_back(ev);
    players_.erase(it);
}

Player* Session::findPlayer(uint32_t id) {
    for (Player& p : players_)
        if (p.id == id) return &p;
    return nullptr;
}

const Player* Session::findPlayer(uint32_t id) const {
    for (const Player& p : players_)
        if (p.id == id) return &p;
    return nullptr;
}

Vec3 Session::eyePosition(const Player& player) const {
    return player.pos + Vec3{0.0f, kEyeHeight, 0.0f};
}

bool Session::withinReach(const Player& player, const IVec3& pos) const {
    const Vec3 center = pos.toVec3() + Vec3{0.5f, 0.5f, 0.5f};
    return distSq(eyePosition(player), center) <= (kReachDistance + 0.8f) * (kReachDistance + 0.8f);
}

bool Session::applyInput(uint32_t id, const Vec3& pos, float yaw, float pitch, uint8_t flags, uint8_t slot) {
    Player* p = findPlayer(id);
    if (!p) return false;
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) return false;
    if (!std::isfinite(yaw) || !std::isfinite(pitch)) return false;

    // простая защита: не даём телепортироваться далеко за один пакет
    if ((pos - p->pos).lengthSq() > 12.0f * 12.0f) return false;

    if (p->alive) {
        p->pos = pos;
        p->yaw = yaw;
        p->pitch = clampf(pitch, -1.55f, 1.55f);
        p->slot = static_cast<uint8_t>(clampi(slot, 0, 8));
        p->flags = creative() ? (flags | net::kFlagFlying) : (flags & ~net::kFlagFlying);
        if (!p->alive) p->flags |= net::kFlagDead;
    }
    return true;
}

void Session::afterWorldEdit(const IVec3& pos) {
    fluids_.wakeAround(world_, pos.x, pos.y, pos.z, 2);
}

void Session::scheduleSolve(const std::vector<IVec3>& seeds, const IVec3& origin) {
    if (seeds.empty()) return;
    if (solves_.size() > 24) return;  // не копим лавину задач
    PendingSolve req;
    req.origin = origin;
    req.seeds = seeds;
    solves_.push_back(std::move(req));
}

bool Session::requestBreak(uint32_t id, const IVec3& pos) {
    Player* p = findPlayer(id);
    if (!p || !p->alive) return false;
    if (!World::inBounds(pos.x, pos.y, pos.z)) return false;
    if (!withinReach(*p, pos)) return false;
    const uint8_t block = world_.get(pos.x, pos.y, pos.z);
    if (block == kAir || block == kBedrock || isWater(block)) return false;

    if (block == kTnt) {
        PendingExplosion boom;
        boom.pos = pos.toVec3() + Vec3{0.5f, 0.5f, 0.5f};
        boom.radius = 4.6f;
        boom.fuse = 0.55f;
        explosions_.push_back(boom);
        events_.push_back(SessionEvent{SessionEventKind::Explosion, boom.pos, "ТНТ активирован", p->id});
        return true;
    }

    world_.set(pos.x, pos.y, pos.z, kAir);
    afterWorldEdit(pos);
    scheduleSolve({pos}, pos);
    return true;
}

bool Session::requestPlace(uint32_t id, const IVec3& pos, uint8_t block) {
    Player* p = findPlayer(id);
    if (!p || !p->alive) return false;
    if (!World::inBounds(pos.x, pos.y, pos.z)) return false;
    if (!withinReach(*p, pos)) return false;
    if (block == kAir || block == kBlockCount) return false;
    if (std::string(blockDef(block).id) == "void") return false;
    const uint8_t current = world_.get(pos.x, pos.y, pos.z);
    if (current != kAir && !isWater(current) && current != kFire) return false;

    world_.set(pos.x, pos.y, pos.z, block);
    afterWorldEdit(pos);
    return true;
}

void Session::doExplosion(const Vec3& pos, float radius) {
    const float r = clampf(radius, 1.0f, 8.0f);
    ExplosionResult res = explode(world_, pos, r, &fire_, &fluids_);
    if (!res.affected.empty()) {
        const IVec3 origin{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)),
                           static_cast<int>(std::floor(pos.z))};
        scheduleSolve(res.affected, origin);
    }

    events_.push_back(SessionEvent{SessionEventKind::Explosion, pos,
                                   "Взрыв: " + std::to_string(res.destroyed) + " блоков", 0});

    for (Player& p : players_) {
        if (!p.alive) continue;
        const float d = (eyePosition(p) - pos).length();
        const float range = r * 1.9f;
        if (d < range) {
            const int dmg = static_cast<int>(kExplosionPlayerDamage * (1.0f - d / range));
            damagePlayer(p, dmg, "взрыв");
        }
    }
}

bool Session::requestExplode(uint32_t id, const Vec3& pos, float radius) {
    Player* p = findPlayer(id);
    if (!p || !p->alive) return false;
    const IVec3 cell{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)),
                     static_cast<int>(std::floor(pos.z))};
    if (!World::inBounds(cell.x, cell.y, cell.z)) return false;
    if (distSq(eyePosition(*p), pos) > 10.0f * 10.0f) return false;
    doExplosion(pos, radius);
    return true;
}

bool Session::requestAttack(uint32_t id) {
    Player* attacker = findPlayer(id);
    if (!attacker || !attacker->alive || !pvpEnabled()) return false;

    const Vec3 eye = eyePosition(*attacker);
    const Vec3 dir{std::cos(attacker->pitch) * std::sin(attacker->yaw), std::sin(attacker->pitch),
                   std::cos(attacker->pitch) * std::cos(attacker->yaw)};
    float bestT = 3.6f;
    Player* victim = nullptr;
    for (Player& other : players_) {
        if (other.id == id || !other.alive) continue;
        const AABB box{other.pos, other.pos + Vec3{0.0f, kPlayerHeight, 0.0f}};
        AABB grown{box.min - Vec3{0.34f, 0.0f, 0.34f}, box.max + Vec3{0.34f, 0.0f, 0.34f}};
        const float t = rayAabb(Ray{eye, dir}, grown, bestT);
        if (t >= 0.0f && t < bestT) {
            bestT = t;
            victim = &other;
        }
    }
    if (!victim) return false;
    damagePlayer(*victim, 24, attacker->name);
    return true;
}

void Session::requestRespawn(uint32_t id) {
    Player* p = findPlayer(id);
    if (!p) return;
    respawn(*p);
}

void Session::requestMode(GameMode mode) {
    if (mode == config_.mode) return;
    reset(config_.seed, mode);
    SessionEvent ev;
    ev.kind = SessionEventKind::Info;
    ev.text = std::string("Режим: ") + gameModeName(mode);
    events_.push_back(ev);
}

void Session::chat(uint32_t id, const std::string& text) {
    const Player* p = findPlayer(id);
    if (!p) return;
    SessionEvent ev;
    ev.kind = SessionEventKind::Chat;
    ev.playerId = id;
    ev.text = p->name + ": " + sanitize(text, 120);
    events_.push_back(ev);
}

void Session::damagePlayer(Player& player, int amount, const std::string& reason) {
    if (creative() && reason != "взрыв") return;
    if (!player.alive) return;
    player.health = static_cast<int16_t>(std::max(0, static_cast<int>(player.health) - amount));
    if (player.health > 0) return;

    player.alive = false;
    player.health = 0;
    player.flags |= net::kFlagDead;
    player.respawnTimer = 3.5f;

    SessionEvent ev;
    ev.kind = SessionEventKind::Damage;
    ev.playerId = player.id;
    ev.text = player.name + " выбыл (" + reason + ")";
    events_.push_back(ev);
}

void Session::respawn(Player& player) {
    player.alive = true;
    player.health = 100;
    player.velocity = Vec3{};
    player.flags = creative() ? net::kFlagFlying : 0;
    const float angle = static_cast<float>(player.id) * 1.7f;
    player.pos = spawnPoint_ + Vec3{std::cos(angle) * 2.0f, 0.0f, std::sin(angle) * 2.0f};
}

int Session::aliveCount() const {
    int n = 0;
    for (const Player& p : players_)
        if (p.alive) ++n;
    return n;
}

void Session::spawnDebrisFromCollapse(const CollapseResult& collapse) {
    int spawned = 0;
    for (const IVec3& p : collapse.unstable) {
        const uint8_t block = world_.get(p.x, p.y, p.z);
        if (block == kAir) continue;
        world_.set(p.x, p.y, p.z, kAir);
        fluids_.wakeAround(world_, p.x, p.y, p.z, 1);

        if (debris_.size() >= kMaxDebris) continue;
        Rng rng(hash3(p.x, p.y, p.z, tickIndex_ + 17u));
        DebrisEntity d;
        d.id = nextDebrisId_++;
        d.block = block;
        d.pos = p.toVec3() + Vec3{0.5f, 0.5f, 0.5f};
        d.vel = Vec3{rng.range(-1.2f, 1.2f), rng.range(0.4f, 2.6f), rng.range(-1.2f, 1.2f)};
        d.life = 6.0f;
        debris_.push_back(d);
        ++spawned;
    }
    if (spawned >= 6) {
        events_.push_back(SessionEvent{SessionEventKind::Collapse, {},
                                       "Обрушение: " + std::to_string(spawned) + " блоков" + 
                                           (collapse.truncated ? " (частично)" : ""),
                                       0});
    }
}

void Session::processSolves(int maxSolves) {
    int done = 0;
    while (!solves_.empty() && done < maxSolves) {
        PendingSolve req = std::move(solves_.back());
        solves_.pop_back();
        const CollapseResult res = solver_.evaluate(world_, req.origin, StructureSolver::kDefaultRadius, &req.seeds);
        if (!res.unstable.empty()) spawnDebrisFromCollapse(res);
        ++done;
    }
}

void Session::updateModeLogic(float dt) {
    if (winResetTimer_ >= 0.0f) {
        winResetTimer_ -= dt;
        if (winResetTimer_ <= 0.0f) {
            winResetTimer_ = -1.0f;
            winnerId_ = 0;
            for (Player& p : players_) respawn(p);
            events_.push_back(SessionEvent{SessionEventKind::Info, {}, "Новый раунд", 0});
        }
        return;
    }
    if (config_.mode != GameMode::LastStanding) return;
    if (players_.size() < 2) return;
    if (aliveCount() != 1) return;
    for (const Player& p : players_) {
        if (!p.alive) continue;
        winnerId_ = p.id;
        events_.push_back(SessionEvent{SessionEventKind::Win, p.pos, "Победа: " + p.name, p.id});
        winResetTimer_ = 6.0f;
        break;
    }
}

void Session::tick(float dt) {
    ++tickIndex_;
    dt = clampf(dt, 0.0f, 0.2f);

    // --- вода и огонь (12 Гц, с бюджетом) ---
    simAccumulator_ += dt;
    float step = 1.0f / 12.0f;
    int steps = 0;
    while (simAccumulator_ >= step && steps < 3) {
        fluids_.tick(world_, 2600);
        fire_.tick(world_, tickIndex_, 384);
        simAccumulator_ -= step;
        ++steps;
    }

    // --- обрушения (не больше двух зон за тик) ---
    processSolves(2);

    // --- отложенные взрывы (ТНТ) ---
    for (size_t i = 0; i < explosions_.size();) {
        explosions_[i].fuse -= dt;
        if (explosions_[i].fuse <= 0.0f) {
            doExplosion(explosions_[i].pos, explosions_[i].radius);
            explosions_.erase(explosions_.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }

    // --- падающие обломки ---
    for (size_t i = 0; i < debris_.size();) {
        DebrisEntity& d = debris_[i];
        d.life -= dt;
        d.vel.y -= kGravity * dt;
        d.pos += d.vel * dt;

        const int bx = static_cast<int>(std::floor(d.pos.x));
        const int by = static_cast<int>(std::floor(d.pos.y));
        const int bz = static_cast<int>(std::floor(d.pos.z));
        bool settle = d.life <= 0.0f || d.pos.y < 1.0f;

        if (!settle && World::inBounds(bx, by - 1, bz)) {
            const uint8_t below = world_.get(bx, by - 1, bz);
            if (isSolidBlock(below) || isWater(below)) settle = true;
        }

        if (!settle) {
            ++i;
            continue;
        }

        if (World::inBounds(bx, by, bz)) {
            const uint8_t target = world_.get(bx, by, bz);
            if (target == kAir || isWater(target) || target == kFire) {
                world_.set(bx, by, bz, d.block);
            }
        }
        debris_.erase(debris_.begin() + static_cast<long>(i));
    }

    // --- таймеры респавна ---
    for (Player& p : players_) {
        if (p.alive) continue;
        p.respawnTimer -= dt;
        if (p.respawnTimer <= 0.0f) respawn(p);
    }

    updateModeLogic(dt);
    modeTimer_ += dt;
}

std::vector<BlockChange> Session::takeChanges() {
    std::vector<BlockChange> changes = world_.takeChanges();
    for (const BlockChange& c : changes) recordHistory(c);
    return changes;
}

void Session::recordHistory(const BlockChange& change) {
    const int idx = World::index(change.x, change.y, change.z);
    auto it = historyIndex_.find(idx);
    if (it != historyIndex_.end()) {
        history_[it->second] = change;
        return;
    }
    if (history_.size() >= kMaxHistory) {
        // Компактим: оставляем вторую половину истории.
        const size_t drop = history_.size() / 2;
        history_.erase(history_.begin(), history_.begin() + static_cast<long>(drop));
        historyIndex_.clear();
        for (size_t i = 0; i < history_.size(); ++i) {
            historyIndex_[World::index(history_[i].x, history_[i].y, history_[i].z)] = static_cast<uint32_t>(i);
        }
        it = historyIndex_.find(idx);
        if (it != historyIndex_.end()) {
            history_[it->second] = change;
            return;
        }
    }
    historyIndex_[idx] = static_cast<uint32_t>(history_.size());
    history_.push_back(change);
}

std::vector<SessionEvent> Session::takeEvents() {
    std::vector<SessionEvent> out;
    out.swap(events_);
    return out;
}

std::vector<net::PlayerSnapshot> Session::playerSnapshots() const {
    std::vector<net::PlayerSnapshot> out;
    out.reserve(players_.size());
    for (const Player& p : players_) {
        net::PlayerSnapshot s;
        s.id = p.id;
        s.name = p.name;
        s.pos = p.pos;
        s.yaw = p.yaw;
        s.pitch = p.pitch;
        s.health = p.health;
        s.flags = p.flags;
        s.slot = p.slot;
        out.push_back(s);
    }
    return out;
}

std::vector<net::DebrisSnapshot> Session::debrisSnapshot() const {
    std::vector<net::DebrisSnapshot> out;
    out.reserve(debris_.size());
    for (const DebrisEntity& d : debris_) {
        net::DebrisSnapshot s;
        s.id = d.id;
        s.pos = d.pos;
        s.block = d.block;
        out.push_back(s);
    }
    return out;
}

uint32_t Session::worldHash() const {
    uint32_t hash = 2166136261u;
    const uint8_t* data = world_.data();
    const size_t count = static_cast<size_t>(World::kSizeX) * World::kSizeY * World::kSizeZ;
    for (size_t i = 0; i < count; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

}  // namespace razlom
