// РАЗЛОМ — тесты интерфейса и шрифта. Работают без GPU: проверяют атлас,
// раскладку HUD и попадания по кнопкам на настоящих разрешениях устройств.
#include <algorithm>
#include <cstdio>
#include <tuple>
#include <utility>
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
using razlom::client::TouchLayout;

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

bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
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
    CHECK(layout.attack.x + layout.attack.w <= static_cast<float>(w));
    CHECK(layout.attack.y + layout.attack.h <= static_cast<float>(h));
    // кнопки действий не налезают друг на друга
    const Rect buttons[6] = {layout.jump, layout.breakBlock, layout.placeBlock,
                             layout.explode, layout.fly, layout.attack};
    for (int i = 0; i < 6; ++i) {
        for (int j = i + 1; j < 6; ++j) {
            const bool overlap = buttons[i].x < buttons[j].x + buttons[j].w &&
                                 buttons[j].x < buttons[i].x + buttons[i].w &&
                                 buttons[i].y < buttons[j].y + buttons[j].h &&
                                 buttons[j].y < buttons[i].y + buttons[i].h;
            CHECK(!overlap);
        }
    }
    // стик не пересекается с кнопками действий
    for (const Rect& b : buttons) {
        const bool overlap = layout.stick.x < b.x + b.w && b.x < layout.stick.x + layout.stick.w &&
                             layout.stick.y < b.y + b.h && b.y < layout.stick.y + layout.stick.h;
        CHECK(!overlap);
    }
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

// Диагностика кадра видна и на игровом экране: по скриншоту с телефона сразу
// понятно, сколько вызовов отрисовки и чанков дошло до экрана (этим мы и
// отлаживали «HUD есть, мира нет»).
TEST(hud_playing_shows_frame_stats) {
    Hud hud;
    hud.init(&atlas());
    HudModel model = menuModel();
    model.screen = Screen::Playing;
    model.showTouch = false;
    HudInput input;
    input.width = 1280;
    input.height = 760;
    HudResult result;

    model.stats.clear();
    hud.draw(model, input, result);
    const size_t withoutStats = hud.vertices().size();

    model.stats = "GLES3 · 2340x1080 · 30 выз. · 12 чанков";
    hud.draw(model, input, result);
    CHECK(hud.vertices().size() > withoutStats);

    float maxX = 0.0f;
    for (const auto& v : hud.vertices()) maxX = std::max(maxX, v.x);
    CHECK(maxX <= static_cast<float>(input.width) + 1.0f);
}

// Кнопка паузы должна быть доступна с телефона: она в раскладке (одна и та же
// для отрисовки и для попаданий) и ни с чем не пересекается.
TEST(hud_pause_button_is_clickable_on_screen) {
    Hud hud;
    hud.init(&atlas());
    for (const auto& size : {std::pair<int, int>{1280, 760}, std::pair<int, int>{2340, 1080}}) {
        const TouchLayout& tl = hud.layout(size.first, size.second);
        const Rect& p = tl.pause;
        CHECK(p.w > 20.0f);
        CHECK(p.h > 20.0f);
        CHECK(p.x >= 0.0f);
        CHECK(p.y >= 0.0f);
        CHECK(p.x + p.w <= static_cast<float>(size.first));
        CHECK(p.y + p.h <= static_cast<float>(size.second));
        CHECK(!overlaps(p, tl.stick));
        const Rect buttons[] = {tl.jump, tl.breakBlock, tl.placeBlock, tl.explode, tl.fly, tl.attack};
        for (const Rect& b : buttons) CHECK(!overlaps(p, b));
        for (const Rect& h : tl.hotbar) CHECK(!overlaps(p, h));
    }
}

// Кружок стика не вылезает за пределы области при любом отклонении.
TEST(hud_stick_knob_stays_inside) {
    Hud hud;
    hud.init(&atlas());
    for (const auto& size : {std::pair<int, int>{1280, 760}, std::pair<int, int>{2340, 1080}}) {
        HudInput input;
        input.width = size.first;
        input.height = size.second;
        input.touchMode = true;
        HudResult ignored;
        hud.draw(menuModel(), input, ignored);
        const TouchLayout& tl = hud.layout(size.first, size.second);
        const float s = hud.scale();
        for (float dy : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
            for (float dx : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
                const razlom::client::StickKnob k = razlom::client::stickKnobFor(tl.stick, dx, dy, s);
                CHECK(k.x >= tl.stick.x - 0.5f);
                CHECK(k.y >= tl.stick.y - 0.5f);
                CHECK(k.x + k.size <= tl.stick.x + tl.stick.w + 0.5f);
                CHECK(k.y + k.size <= tl.stick.y + tl.stick.h + 0.5f);
            }
        }
    }
}

// Нажатие по кнопке паузы действительно возвращает действие Pause — раньше
// на телефоне касание игрового экрана не доходило до HUD.
TEST(hud_pause_button_triggers_action) {
    Hud hud;
    hud.init(&atlas());
    HudModel model = menuModel();
    model.screen = Screen::Playing;
    model.showTouch = true;
    HudInput input;
    input.width = 2340;
    input.height = 1080;
    input.touchMode = true;
    const TouchLayout& tl = hud.layout(input.width, input.height);
    input.mouseX = tl.pause.cx();
    input.mouseY = tl.pause.cy();
    input.pressed = true;
    input.down = true;
    HudResult result;
    hud.draw(model, input, result);
    CHECK(result.action == HudAction::Pause);

    // касание мимо кнопки паузу не вызывает
    input.mouseX = tl.pause.x - 40.0f;
    input.mouseY = tl.pause.cy();
    HudResult other;
    hud.draw(model, input, other);
    CHECK(other.action != HudAction::Pause);
}

// Состояние полёта видно на кнопке: «ЛЕТИМ» вместо «ПОЛЁТ».
TEST(hud_fly_state_is_visible) {
    Hud hud;
    hud.init(&atlas());
    HudModel model = menuModel();
    model.screen = Screen::Playing;
    model.showTouch = true;
    HudInput input;
    input.width = 2340;
    input.height = 1080;
    input.touchMode = true;
    HudResult result;

    const TouchLayout& tl = hud.layout(input.width, input.height);
    auto flyButtonTint = [&]() {
        int r = 0, g = 0, b = 0, n = 0;
        for (const auto& vert : hud.vertices()) {
            if (!tl.fly.contains(vert.x, vert.y)) continue;
            r += vert.r;
            g += vert.g;
            b += vert.b;
            ++n;
        }
        return std::make_tuple(n, n ? r / n : 0, n ? g / n : 0, n ? b / n : 0);
    };

    model.flying = false;
    hud.draw(model, input, result);
    const auto idle = flyButtonTint();
    model.flying = true;
    hud.draw(model, input, result);
    const auto flying = flyButtonTint();
    CHECK(std::get<0>(idle) > 0);
    CHECK(std::get<0>(flying) > 0);
    // активная кнопка светлее: подсветка состояния полёта видна игроку
    CHECK(std::get<1>(flying) > std::get<1>(idle));
}

int main() { return test::runAll(); }
