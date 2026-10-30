// РАЗЛОМ — клиентское приложение: меню, мир, игрок, сеть, интерфейс.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "razlom/client/font.hpp"
#include "razlom/client/hud.hpp"
#include "razlom/client/renderer.hpp"
#include "razlom/core/session.hpp"
#include "razlom/core/world.hpp"
#include "razlom/net/client.hpp"
#include "razlom/net/server.hpp"
#include "sokol_app.h"

namespace razlom::client {

class App {
public:
    bool init();
    void frame();
    void cleanup();
    void onEvent(const sapp_event* ev);

    // Подключение по ссылке из командной строки (razlom://host:port).
    void openAddress(const std::string& address) { startJoining(address); }

private:
    struct LocalPlayer {
        Vec3 pos;
        Vec3 vel;
        float yaw = 0.0f;
        float pitch = -0.12f;
        bool onGround = false;
        bool flying = false;
        int health = 100;
        bool dead = false;
        uint8_t slot = 0;
    };

    enum class TouchRole { None = 0, Stick = 1, Look = 2 };

    void startHosting();
    void startJoining(const std::string& address);
    void disconnectAll();
    void enterMenu();
    void beginWorld(uint32_t seed, GameMode mode, const std::vector<BlockChange>& history);
    void handleNetwork(float dt);
    void updatePlaying(float dt);
    void updateMovement(float dt);
    void sendInput(float dt);
    void tryEdit(bool breakBlock);
    void triggerExplosion();
    void addLog(const std::string& text);
    void addToast(const std::string& text);
    void look(float dx, float dy);
    void buildHudModel(HudModel& model, const HudInput& input);
    void handleHudAction(const HudResult& result);
    void handleKeyDown(const sapp_event* ev);
    void handleKeyUp(const sapp_event* ev);
    void handleTouchBegan(const sapp_event* ev);
    void handleTouchMoved(const sapp_event* ev);
    void handleTouchEnded(const sapp_event* ev);
    bool isEditingText() const { return screen_ == Screen::Join; }

    Renderer renderer_;
    FontAtlas font_;
    Hud hud_;
    Screen screen_ = Screen::Menu;
    Screen screenBeforePause_ = Screen::Playing;

    net::Client client_;
    std::unique_ptr<net::Server> server_;
    bool hosting_ = false;
    bool worldReady_ = false;
    bool pendingWelcome_ = false;
    uint8_t selfId_ = 0;
    uint32_t worldSeed_ = 0;
    GameMode mode_ = GameMode::Sandbox;

    World world_{1};
    std::vector<net::PlayerSnapshot> players_;
    std::vector<net::PlayerSnapshot> playersFromWelcome_;
    std::vector<net::DebrisSnapshot> debris_;
    std::vector<std::string> log_;
    std::string toast_;
    float toastTimer_ = 0.0f;

    LocalPlayer local_;
    std::vector<uint8_t> hotbar_;

    std::string roomLink_;
    std::string roomCode_;
    std::string joinAddress_;
    std::string error_;
    std::string status_;
    std::string playerName_ = "Кубик";
    int maxPlayers_ = 4;

    bool hasHighlight_ = false;
    IVec3 highlight_;

    bool keys_[512] = {};
    bool mouseLocked_ = false;
    bool touchMode_ = false;
    float mouseX_ = 0.0f;
    float mouseY_ = 0.0f;
    bool hudClick_ = false;
    bool hudDown_ = false;

    // тач
    uintptr_t stickTouch_ = 0;
    uintptr_t lookTouch_ = 0;
    float stickOriginX_ = 0.0f;
    float stickOriginY_ = 0.0f;
    float stickDX_ = 0.0f;
    float stickDY_ = 0.0f;
    float lookLastX_ = 0.0f;
    float lookLastY_ = 0.0f;
    bool jumpRequested_ = false;

    // ввод действий
    bool breakHeld_ = false;
    bool placeHeld_ = false;
    float breakCooldown_ = 0.0f;
    float placeCooldown_ = 0.0f;
    float inputAccumulator_ = 0.0f;
    float pingAccumulator_ = 0.0f;
    float joinTimer_ = 0.0f;
    bool joinRequested_ = false;
    float loadingProgress_ = 0.0f;
    int loadingTotal_ = 1;

    uint64_t lastFrameTime_ = 0;
    float time_ = 0.0f;
    float fpsAccumulator_ = 0.0f;
    int fpsFrames_ = 0;
    int fps_ = 0;
    int screenWidth_ = 1280;
    int screenHeight_ = 720;
    std::string lastServerPosSnap_;
};

}  // namespace razlom::client
