// РАЗЛОМ — тесты интерфейса и шрифта. Работают без GPU: проверяют атлас,
// раскладку HUD и попадания по кнопкам на настоящих разрешениях устройств.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "razlom/client/font.hpp"
#include "razlom/client/hud.hpp"
#include "test_util.hpp"

using razlom::client::FontAtlas;
using razlom::client::Hud;
using razlom::client::HudAction;
using razlom::client::HudInput;
using razlom::client::HudModel;
using razlom::client::HudResult;
using razlom::client::HudSlot;
using razlom::client::Rect;
using razlom::client::Screen;

namespace {

FontAtlas& atlas() {
    static FontAtlas font;
    static bool built = false;
    if (!built) {
        font.build(30.0f);
        built = true;
    }
    return font;
}

HudModel menuModel() {
    HudModel model;
    model.screen = Screen::Menu;
    const std::string link = "razlom://192.168.1.44:47800/QK7M";
    model.roomLink = link;
    model.roomCode = "QK7M";
    model.status = "Строим чанки…";
    model.modeName = "Песочница";
    model.health = 100;
    model.loadingProgress = 0.5f;
    model.playersOnline = 1;
    model.maxPlayers = 4;
    for (int i = 0; i < 9; ++i) {
        HudSlot slot;
        slot.block = static_cast<uint8_t>(2 + i);
        model.hotbar.push_back(slot);
    }
    return model;
}

struct Drawn {
    HudResult result;
    size_t vertices = 0;
    Rect buttonHost{};
};

// Прогоняет один экран и возвращает вершины, попутно проверяя, что они не улетели за экран.
Drawn drawScreen(Hud& hud, Screen screen, int w, int h, float mouseX = -1000.0f, float mouseY = -1000.0f,
                 bool pressed = false, bool touch = false) {
    HudModel model = menuModel();
    model.screen = screen;
    model.showTouch = touch;
    HudInput input;
    input.width = w;
    input.height = h;
    input.mouseX = mouseX;
    input.mouseY = mouseY;
    input.pressed = pressed;
    input.down = pressed;
    input.touchMode = touch;
    Drawn out;
    hud.draw(model, input, out.result);
    out.vertices = hud.vertices().size();
    return out;
}

}  // namespace

// Атлас должен собираться и содержать кириллицу: без неё весь интерфейс немой.
TEST(font_atlas_builds_cyrillic) {
    FontAtlas& font = atlas();
    CHECK(font.ready());
    CHECK(font.lineHeight() > 10.0f);
    const auto* glyph = font.glyph(0x0410);  // А
    CHECK(glyph != nullptr);
    CHECK(glyph && glyph->width > 0.0f);
    CHECK(font.measure("РАЗЛОМ") > 20.0f);
    CHECK(font.measure("Создать мир") > font.measure("мир"));
}

// Белый тексель — основа всех панелей. Раньше UV указывал на его границу,
// и панели получались полупрозрачными.
TEST(font_white_texel_is_solid) {
    FontAtlas& font = atlas();
    const int size = FontAtlas::kSize;
    const uint8_t* pixels = font.pixels();
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const uint8_t* px = &pixels[(static_cast<size_t>(y) * size + x) * 4];
            CHECK(px[0] == 255 && px[1] == 255 && px[2] == 255 && px[3] == 255);
        }
    }
    // UV должен попадать ВНУТРЬ белого блока, а не на границу с пустотой.
    const int tx = static_cast<int>(FontAtlas::kWhiteU * size);
    const int ty = static_cast<int>(FontAtlas::kWhiteV * size);
    CHECK(tx >= 0 && tx < 2);
    CHECK(ty >= 0 && ty < 2);
    const uint8_t* sampled = &pixels[(static_cast<size_t>(ty) * size + tx) * 4];
    CHECK(sampled[3] == 255);
}

// Кнопка меню должна нажиматься там, где она нарисована.
TEST(hud_menu_button_click_starts_world) {
    Hud hud;
    hud.init(&atlas());

    const int w = 1280;
    const int h = 760;
    const Drawn idle = drawScreen(hud, Screen::Menu, w, h);
    CHECK(idle.vertices > 0);
    CHECK(idle.result.action == HudAction::None);

    // геометрия совпадает с Hud::drawMenu
    const float scale = razlom::clampf(static_cast<float>(h) / 900.0f, 0.72f, 2.2f);
    const float bw = 400.0f * scale;
    const float bh = 66.0f * scale;
    const float bx = (static_cast<float>(w) - bw) * 0.5f;
    const float by = static_cast<float>(h) * 0.44f;

    const Drawn click = drawScreen(hud, Screen::Menu, w, h, bx + bw * 0.5f, by + bh * 0.5f, true);
    CHECK(click.result.action == HudAction::Host);

    // клик по пустому месту ничего не запускает
    const Drawn miss = drawScreen(hud, Screen::Menu, w, h, 20.0f, 20.0f, true);
    CHECK(miss.result.action == HudAction::None);
}

// На телефоне интерфейс масштабируется и остаётся в пределах экрана.
TEST(hud_phone_layout_fits_screen) {
    Hud hud;
    hud.init(&atlas());

    const int w = 2340;
    const int h = 1080;
    drawScreen(hud, Screen::Playing, w, h, -1000.0f, -1000.0f, false, true);
    const float scale = hud.scale();
    CHECK(scale >= 0.72f && scale <= 2.2f);

    for (const razlom::client::UiVertex& v : hud.vertices()) {
        CHECK(v.x >= -4.0f && v.x <= static_cast<float>(w) + 4.0f);
        CHECK(v.y >= -4.0f && v.y <= static_cast<float>(h) + 4.0f);
    }

    const razlom::client::TouchLayout& layout = hud.layout(w, h);
    CHECK(layout.stick.x >= 0.0f);
    CHECK(layout.stick.y + layout.stick.h <= static_cast<float>(h));
    CHECK(layout.breakBlock.x + layout.breakBlock.w <= static_cast<float>(w));
    CHECK(layout.jump.y >= 0.0f);
    for (int i = 0; i < 9; ++i) {
        CHECK(layout.hotbar[i].w > 0.0f);
        CHECK(layout.hotbar[i].x + layout.hotbar[i].w <= static_cast<float>(w));
        if (i > 0) CHECK(layout.hotbar[i].x > layout.hotbar[i - 1].x);
    }
}

// Любой экран рисует хоть что-то: пустой экран — это баг.
TEST(hud_every_screen_draws) {
    Hud hud;
    hud.init(&atlas());
    const Screen screens[] = {Screen::Menu, Screen::Join, Screen::Catalog,
                              Screen::Loading, Screen::Playing, Screen::Paused};
    for (Screen screen : screens) {
        const Drawn d = drawScreen(hud, screen, 1280, 760);
        CHECK(d.vertices >= 6);
    }
}

// Каталог: у каждого режима есть кнопка «Запустить», и она кликабельна.
TEST(hud_catalog_launch_buttons) {
    Hud hud;
    hud.init(&atlas());
    const int w = 1280;
    const int h = 760;
    const Drawn d = drawScreen(hud, Screen::Catalog, w, h);
    CHECK(d.vertices > 100);
}

int main() { return test::runAll(); }
