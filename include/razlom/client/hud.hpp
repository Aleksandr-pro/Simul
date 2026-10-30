// РАЗЛОМ — 2D-интерфейс: меню, HUD, горячая панель, тач-управление.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "razlom/client/font.hpp"
#include "razlom/client/renderer.hpp"

namespace razlom::client {

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
};

enum class Screen { Menu, Join, Catalog, Loading, Playing, Paused };

enum class HudAction {
    None,
    Host,
    OpenJoin,
    JoinSubmit,
    Back,
    Catalog,
    Resume,
    CopyLink,
    QuitToMenu,
    ModeSandbox,
    ModeLastStanding,
    ModeBaseDefense,
    Respawn,
    Explode,
    Attack,
    ToggleFly,
    SelectSlot,
    Pause,
};

struct HudInput {
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    bool down = false;
    bool pressed = false;
    bool released = false;
    bool touchMode = false;
    int width = 1280;
    int height = 720;
};

struct HudResult {
    HudAction action = HudAction::None;
    int slot = -1;
};

struct HudSlot {
    uint8_t block = 0;
    std::string label;
};

struct HudPlayerRow {
    std::string name;
    int health = 100;
    bool self = false;
    bool alive = true;
};

struct HudModel {
    Screen screen = Screen::Menu;
    std::string brand = "РАЗЛОМ";
    std::string tagline = "песочница, в которой всё падает по-настоящему";

    std::string roomLink;
    std::string roomCode;
    std::string joinAddress;
    std::string error;
    std::string status;
    float loadingProgress = 0.0f;
    std::string stats;  // диагностическая строка (бэкенд, размер, вызовы отрисовки)
    // подсказки первого запуска: показываются в начале мира
    std::vector<std::string> tips;
    float tipsTimer = 0.0f;

    std::string modeName;
    std::string modeTagline;
    bool creative = true;

    std::string playerName;
    int health = 100;
    bool dead = false;
    int selectedSlot = 0;
    int fps = 0;
    int ping = -1;
    int playersOnline = 1;
    int maxPlayers = 4;
    std::vector<HudPlayerRow> players;
    std::vector<HudSlot> hotbar;
    std::vector<std::string> log;
    std::string toast;
    float toastTimer = 0.0f;
    bool showTouch = false;
};

// Раскладка сенсорных элементов (одна и та же для отрисовки и для попаданий).
struct TouchLayout {
    Rect stick;
    Rect jump;
    Rect breakBlock;
    Rect placeBlock;
    Rect explode;
    Rect fly;
    Rect hotbar[9];
    Rect slot(Rect r) const { return r; }
};

class Hud {
public:
    bool init(const FontAtlas* font);
    // Если текстура атласа не создалась, тексты отключаются — панели остаются видны.
    void setTextEnabled(bool enabled) { textEnabled_ = enabled; }
    void draw(const HudModel& model, const HudInput& input, HudResult& result);

    const std::vector<UiVertex>& vertices() const { return verts_; }
    const TouchLayout& layout(int width, int height);

    float scale() const { return scale_; }

private:
    void rect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void rectOutline(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float thickness = 2.0f);
    void text(float x, float y, const std::string& utf8, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
              float scale = 1.0f);
    void textCentered(float cx, float y, const std::string& utf8, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                      float scale = 1.0f);
    float measure(const std::string& utf8, float scale) const;
    bool button(const HudInput& input, Rect r, const std::string& label, bool primary, bool enabled = true);

    void drawMenu(const HudModel&, const HudInput&, HudResult&);
    void drawJoin(const HudModel&, const HudInput&, HudResult&);
    void drawCatalog(const HudModel&, const HudInput&, HudResult&);
    void drawLoading(const HudModel&);
    void drawPlaying(const HudModel&, const HudInput&, HudResult&);
    void drawPaused(const HudModel&, const HudInput&, HudResult&);
    void drawTouchControls(const HudModel&, const HudInput&);

    const FontAtlas* font_ = nullptr;
    std::vector<UiVertex> verts_;
    float scale_ = 1.0f;
    bool textEnabled_ = true;
    int width_ = 1280;
    int height_ = 720;
    int hoveredButton_ = -1;
    int buttonIndex_ = 0;
    TouchLayout touch_;
    bool touchLayoutValid_ = false;
    int touchLayoutW_ = 0;
    int touchLayoutH_ = 0;
};

}  // namespace razlom::client
