#include "razlom/client/hud.hpp"

#include <algorithm>
#include <cmath>

#include "razlom/core/blocks.hpp"

namespace razlom::client {
namespace {

// палитра
constexpr uint8_t kBgR = 12, kBgG = 15, kBgB = 24;
constexpr uint8_t kPanelR = 22, kPanelG = 27, kPanelB = 40;
constexpr uint8_t kAccentR = 255, kAccentG = 199, kAccentB = 87;
constexpr uint8_t kAccent2R = 76, kAccent2G = 201, kAccent2B = 240;
constexpr uint8_t kTextR = 236, kTextG = 241, kTextB = 249;
constexpr uint8_t kMutedR = 146, kMutedG = 158, kMutedB = 178;
constexpr uint8_t kDangerR = 240, kDangerG = 105, kDangerB = 90;

std::string formatInt(int value) { return std::to_string(value); }

}  // namespace

bool Hud::init(const FontAtlas* font) {
    font_ = font;
    return font_ != nullptr && font_->ready();
}

float Hud::measure(const std::string& utf8, float scale) const {
    if (!font_) return 0.0f;
    return font_->measure(utf8) * scale;
}

void Hud::rect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const float u = FontAtlas::kWhiteU;
    const float v = FontAtlas::kWhiteV;
    const UiVertex quad[4] = {
        {x, y, u, v, r, g, b, a},
        {x + w, y, u, v, r, g, b, a},
        {x + w, y + h, u, v, r, g, b, a},
        {x, y + h, u, v, r, g, b, a},
    };
    for (int i : {0, 1, 2, 0, 2, 3}) verts_.push_back(quad[i]);
}

void Hud::rectOutline(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                      float thickness) {
    const float t = thickness;
    rect(x, y, w, t, r, g, b, a);
    rect(x, y + h - t, w, t, r, g, b, a);
    rect(x, y, t, h, r, g, b, a);
    rect(x + w - t, y, t, h, r, g, b, a);
}

void Hud::text(float x, float y, const std::string& utf8, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale) {
    if (!font_) return;
    float penX = x;
    float penY = y;
    const float lineHeight = font_->lineHeight() * scale;
    const float ascent = font_->ascent() * scale;
    for (uint32_t cp : FontAtlas::decodeUtf8(utf8)) {
        if (cp == '\n') {
            penX = x;
            penY += lineHeight;
            continue;
        }
        const Glyph* glyph = font_->glyph(cp);
        if (!glyph) continue;
        if (glyph->width > 0.0f && glyph->height > 0.0f) {
            const float gx = penX + glyph->xoff * scale;
            const float gy = penY + ascent + glyph->yoff * scale;
            const float gw = glyph->width * scale;
            const float gh = glyph->height * scale;
            const UiVertex quad[4] = {
                {gx, gy, glyph->u0, glyph->v0, r, g, b, a},
                {gx + gw, gy, glyph->u1, glyph->v0, r, g, b, a},
                {gx + gw, gy + gh, glyph->u1, glyph->v1, r, g, b, a},
                {gx, gy + gh, glyph->u0, glyph->v1, r, g, b, a},
            };
            for (int i : {0, 1, 2, 0, 2, 3}) verts_.push_back(quad[i]);
        }
        penX += glyph->advance * scale;
    }
}

void Hud::textCentered(float cx, float y, const std::string& utf8, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                       float scale) {
    text(cx - measure(utf8, scale) * 0.5f, y, utf8, r, g, b, a, scale);
}

bool Hud::button(const HudInput& input, Rect r, const std::string& label, bool primary, bool enabled) {
    const bool hovered = enabled && r.contains(input.mouseX, input.mouseY);
    const uint8_t fillA = static_cast<uint8_t>(primary ? 235 : 200);
    uint8_t fill[3] = {static_cast<uint8_t>(kPanelR + (hovered ? 24 : 0)), static_cast<uint8_t>(kPanelG + (hovered ? 26 : 0)),
                       static_cast<uint8_t>(kPanelB + (hovered ? 34 : 0))};
    if (primary) {
        fill[0] = static_cast<uint8_t>(kAccentR - (hovered ? 0 : 26));
        fill[1] = static_cast<uint8_t>(kAccentG - (hovered ? 0 : 22));
        fill[2] = static_cast<uint8_t>(kAccentB - (hovered ? 0 : 18));
    }
    if (!enabled) {
        fill[0] = 52;
        fill[1] = 56;
        fill[2] = 66;
    }
    rect(r.x, r.y, r.w, r.h, fill[0], fill[1], fill[2], fillA);
    rectOutline(r.x, r.y, r.w, r.h, primary ? 255 : 120, primary ? 226 : 132, primary ? 150 : 162,
                static_cast<uint8_t>(enabled ? 150 : 60), 2.0f * scale_ + 0.5f);

    const float textScale = 0.82f * scale_;
    const uint8_t tr = primary ? 26 : kTextR;
    const uint8_t tg = primary ? 22 : kTextG;
    const uint8_t tb = primary ? 16 : kTextB;
    textCentered(r.cx(), r.y + (r.h - font_->lineHeight() * textScale) * 0.5f, label, tr, tg, tb,
                 static_cast<uint8_t>(enabled ? 255 : 140), textScale);

    ++buttonIndex_;
    if (hovered && input.pressed) return true;
    return false;
}

const TouchLayout& Hud::layout(int width, int height) {
    if (touchLayoutValid_ && touchLayoutW_ == width && touchLayoutH_ == height) return touch_;
    touchLayoutW_ = width;
    touchLayoutH_ = height;
    touchLayoutValid_ = true;

    const float s = scale_;
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float btn = 92.0f * s;

    touch_.stick = Rect{28.0f * s, h - 28.0f * s - 190.0f * s, 190.0f * s, 190.0f * s};
    touch_.jump = Rect{w - btn - 22.0f * s, h - btn - 26.0f * s, btn, btn};
    touch_.breakBlock = Rect{w - btn * 2.0f - 34.0f * s, h - btn - 78.0f * s, btn, btn};
    touch_.placeBlock = Rect{w - btn * 2.0f - 34.0f * s, h - btn * 2.0f - 130.0f * s, btn, btn};
    touch_.explode = Rect{w - btn * 3.0f - 46.0f * s, h - btn - 26.0f * s, btn, btn};
    touch_.fly = Rect{w - btn * 3.0f - 46.0f * s, h - btn * 2.0f - 78.0f * s, btn, btn};

    const float slotSize = 52.0f * s;
    const float gap = 6.0f * s;
    const float totalW = 9.0f * slotSize + 8.0f * gap;
    const float startX = (w - totalW) * 0.5f;
    const float y = h - slotSize - 16.0f * s;
    for (int i = 0; i < 9; ++i) {
        touch_.hotbar[i] = Rect{startX + static_cast<float>(i) * (slotSize + gap), y, slotSize, slotSize};
    }
    return touch_;
}

void Hud::draw(const HudModel& model, const HudInput& input, HudResult& result) {
    verts_.clear();
    result = HudResult{};
    width_ = input.width;
    height_ = input.height;
    scale_ = clampf(static_cast<float>(input.height) / 900.0f, 0.72f, 2.2f);
    hoveredButton_ = -1;
    buttonIndex_ = 0;

    switch (model.screen) {
        case Screen::Menu: drawMenu(model, input, result); break;
        case Screen::Join: drawJoin(model, input, result); break;
        case Screen::Catalog: drawCatalog(model, input, result); break;
        case Screen::Loading: drawLoading(model); break;
        case Screen::Playing: drawPlaying(model, input, result); break;
        case Screen::Paused:
            drawPlaying(model, input, result);
            drawPaused(model, input, result);
            break;
    }
}

void Hud::drawMenu(const HudModel& model, const HudInput& input, HudResult& result) {
    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);

    // фон: мягкие полосы
    rect(0.0f, 0.0f, w, h, kBgR, kBgG, kBgB, 255);
    rect(0.0f, 0.0f, w, h * 0.34f, 30, 38, 58, 255);
    rect(0.0f, h * 0.34f, w, 3.0f * scale_, kAccentR, kAccentG, kAccentB, 200);

    const float titleScale = 2.6f * scale_;
    textCentered(w * 0.5f, h * 0.16f, model.brand, 255, 255, 255, 255, titleScale);
    textCentered(w * 0.5f, h * 0.16f + font_->lineHeight() * titleScale + 8.0f * scale_, model.tagline,
                 kMutedR, kMutedG, kMutedB, 255, 0.8f * scale_);

    const float bw = 400.0f * scale_;
    const float bh = 66.0f * scale_;
    const float bx = (w - bw) * 0.5f;
    float by = h * 0.44f;

    if (button(input, Rect{bx, by, bw, bh}, "Создать мир", true)) result.action = HudAction::Host;
    by += bh + 14.0f * scale_;
    if (button(input, Rect{bx, by, bw, bh}, "Присоединиться по ссылке", false)) result.action = HudAction::OpenJoin;
    by += bh + 14.0f * scale_;
    if (button(input, Rect{bx, by, bw, bh}, "Каталог режимов", false)) result.action = HudAction::Catalog;

    by += bh + 34.0f * scale_;
    textCentered(w * 0.5f, by, "Мир создаётся одной кнопкой — друзья заходят по ссылке", kMutedR, kMutedG, kMutedB, 220,
                 0.66f * scale_);
    textCentered(w * 0.5f, by + 26.0f * scale_,
                 "WASD — идти • мышь — смотреть • ЛКМ — ломать • ПКМ — ставить • ТНТ — взрыв", kMutedR, kMutedG,
                 kMutedB, 170, 0.6f * scale_);

    if (!model.error.empty()) {
        textCentered(w * 0.5f, h - 70.0f * scale_, model.error, kDangerR, kDangerG, kDangerB, 255, 0.7f * scale_);
    }
    textCentered(w * 0.5f, h - 34.0f * scale_, "прототип v0.1 • нативный C++20 + sokol", kMutedR, kMutedG, kMutedB, 140,
                 0.58f * scale_);
}

void Hud::drawJoin(const HudModel& model, const HudInput& input, HudResult& result) {
    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);
    rect(0.0f, 0.0f, w, h, kBgR, kBgG, kBgB, 255);
    rect(0.0f, 0.0f, w, h * 0.22f, 30, 38, 58, 255);

    textCentered(w * 0.5f, h * 0.09f, "Подключиться", 255, 255, 255, 255, 1.7f * scale_);

    const float bw = 620.0f * scale_;
    const float bx = (w - bw) * 0.5f;
    float by = h * 0.3f;

    text(bx, by - 30.0f * scale_, "Ссылка или адрес хоста", kMutedR, kMutedG, kMutedB, 255, 0.66f * scale_);
    const Rect field{bx, by, bw, 66.0f * scale_};
    rect(field.x, field.y, field.w, field.h, kPanelR, kPanelG, kPanelB, 255);
    rectOutline(field.x, field.y, field.w, field.h, kAccent2R, kAccent2G, kAccent2B, 210, 2.0f);
    const std::string shown = model.joinAddress.empty() ? "127.0.0.1:47800" : model.joinAddress;
    text(field.x + 18.0f * scale_, field.y + 16.0f * scale_, shown,
         model.joinAddress.empty() ? kMutedR : kTextR, model.joinAddress.empty() ? kMutedG : kTextG,
         model.joinAddress.empty() ? kMutedB : kTextB, 255, 0.8f * scale_);

    by += field.h + 26.0f * scale_;
    if (button(input, Rect{bx, by, bw * 0.62f - 8.0f * scale_, 62.0f * scale_}, "Войти", true)) {
        result.action = HudAction::JoinSubmit;
    }
    if (button(input, Rect{bx + bw * 0.62f + 8.0f * scale_, by, bw * 0.38f - 8.0f * scale_, 62.0f * scale_},
               "Назад", false)) {
        result.action = HudAction::Back;
    }

    if (!model.status.empty()) {
        textCentered(w * 0.5f, by + 92.0f * scale_, model.status, kAccent2R, kAccent2G, kAccent2B, 255,
                     0.72f * scale_);
    }
    if (!model.error.empty()) {
        textCentered(w * 0.5f, by + 128.0f * scale_, model.error, kDangerR, kDangerG, kDangerB, 255, 0.72f * scale_);
    }
    textCentered(w * 0.5f, h - 50.0f * scale_, "Хост видит свою ссылку в меню паузы (ESC) и в верхней панели",
                 kMutedR, kMutedG, kMutedB, 160, 0.62f * scale_);
}

void Hud::drawCatalog(const HudModel& model, const HudInput& input, HudResult& result) {
    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);
    rect(0.0f, 0.0f, w, h, kBgR, kBgG, kBgB, 255);
    rect(0.0f, 0.0f, w, h * 0.2f, 30, 38, 58, 255);

    textCentered(w * 0.5f, h * 0.07f, "Каталог режимов", 255, 255, 255, 255, 1.6f * scale_);
    textCentered(w * 0.5f, h * 0.07f + 62.0f * scale_,
                 "игра внутри игры: правила собираются из блоков логики", kMutedR, kMutedG, kMutedB, 255,
                 0.7f * scale_);

    struct Card {
        const char* title;
        const char* text;
        const char* badge;
        bool playable;
        HudAction action;
    };
    const Card cards[3] = {
        {"Песочница", "креатив, полёт, всё ломается и падает", "играть", true, HudAction::ModeSandbox},
        {"Последний выживший", "PvP, здоровье, респавн, победитель один", "играть", true, HudAction::ModeLastStanding},
        {"Защита базы", "волны, ядро, магазин — редактор режимов", "скоро", false, HudAction::None},
    };

    const float cardW = std::min(360.0f * scale_, (w - 120.0f * scale_) / 3.0f - 20.0f * scale_);
    const float cardH = 330.0f * scale_;
    const float gap = 22.0f * scale_;
    const float totalW = cardW * 3.0f + gap * 2.0f;
    const float startX = (w - totalW) * 0.5f;
    const float top = h * 0.3f;

    for (int i = 0; i < 3; ++i) {
        const Rect card{startX + static_cast<float>(i) * (cardW + gap), top, cardW, cardH};
        rect(card.x, card.y, card.w, card.h, kPanelR, kPanelG, kPanelB, 235);
        rectOutline(card.x, card.y, card.w, card.h, cards[i].playable ? kAccentR : 90,
                    cards[i].playable ? kAccentG : 96, cards[i].playable ? kAccentB : 110, 200, 2.0f);
        text(card.x + 18.0f * scale_, card.y + 22.0f * scale_, cards[i].title, kTextR, kTextG, kTextB, 255,
             0.8f * scale_);
        text(card.x + 18.0f * scale_, card.y + 70.0f * scale_, cards[i].text, kMutedR, kMutedG, kMutedB, 255,
             0.64f * scale_);
        rect(card.x + 18.0f * scale_, card.y + cardH - 82.0f * scale_, 84.0f * scale_, 26.0f * scale_,
             cards[i].playable ? 44 : 70, cards[i].playable ? 74 : 74, cards[i].playable ? 58 : 82, 200);
        text(card.x + 26.0f * scale_, card.y + cardH - 78.0f * scale_, cards[i].badge,
             cards[i].playable ? 150 : 150, cards[i].playable ? 220 : 160, cards[i].playable ? 160 : 175, 255,
             0.6f * scale_);

        const Rect cardButton{card.x + 18.0f * scale_, card.y + cardH - 44.0f * scale_,
                              card.w - 36.0f * scale_, 30.0f * scale_};
        if (cards[i].playable) {
            if (button(input, cardButton, "Запустить", true)) result.action = cards[i].action;
        }
    }

    if (button(input, Rect{(w - 240.0f * scale_) * 0.5f, h - 96.0f * scale_, 240.0f * scale_, 58.0f * scale_},
               "Назад", false)) {
        result.action = HudAction::Back;
    }
}

void Hud::drawLoading(const HudModel& model) {
    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);
    rect(0.0f, 0.0f, w, h, kBgR, kBgG, kBgB, 255);
    textCentered(w * 0.5f, h * 0.38f, "Собираем мир", 255, 255, 255, 255, 1.8f * scale_);
    textCentered(w * 0.5f, h * 0.38f + 70.0f * scale_, model.status, kMutedR, kMutedG, kMutedB, 255,
                 0.74f * scale_);

    const float bw = 520.0f * scale_;
    const float bx = (w - bw) * 0.5f;
    const float by = h * 0.56f;
    rect(bx, by, bw, 16.0f * scale_, kPanelR, kPanelG, kPanelB, 255);
    rect(bx, by, bw * clampf(model.loadingProgress, 0.0f, 1.0f), 16.0f * scale_, kAccentR, kAccentG, kAccentB, 255);
    textCentered(w * 0.5f, by + 30.0f * scale_,
                 formatInt(static_cast<int>(model.loadingProgress * 100.0f)) + "%", kMutedR, kMutedG, kMutedB, 255,
                 0.66f * scale_);
}

void Hud::drawPlaying(const HudModel& model, const HudInput& input, HudResult& result) {
    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);
    const float s = scale_;
    const TouchLayout& tl = layout(width_, height_);

    // --- прицел ---
    if (!model.dead) {
        const float cx = w * 0.5f;
        const float cy = h * 0.5f;
        rect(cx - 10.0f * s, cy - 1.5f * s, 20.0f * s, 3.0f * s, 255, 255, 255, 200);
        rect(cx - 1.5f * s, cy - 10.0f * s, 3.0f * s, 20.0f * s, 255, 255, 255, 200);
    }

    // --- верхняя левая панель ---
    const float panelW = 372.0f * s;
    rect(14.0f * s, 14.0f * s, panelW, 96.0f * s, kPanelR, kPanelG, kPanelB, 205);
    rect(14.0f * s, 14.0f * s, 4.0f * s, 96.0f * s, kAccentR, kAccentG, kAccentB, 255);
    text(30.0f * s, 22.0f * s, model.modeName, kTextR, kTextG, kTextB, 255, 0.72f * s);
    text(30.0f * s, 60.0f * s, "Комната " + model.roomCode + "  •  " + std::to_string(model.playersOnline) + "/" +
                                   std::to_string(model.maxPlayers) + " игроков",
         kMutedR, kMutedG, kMutedB, 255, 0.6f * s);
    const std::string net = (model.ping >= 0 ? std::to_string(model.ping) + " мс" : std::string("—")) + "  •  " +
                            std::to_string(model.fps) + " FPS";
    text(30.0f * s, 84.0f * s, net, kAccent2R, kAccent2G, kAccent2B, 220, 0.56f * s);

    // --- ссылка для друзей ---
    if (!model.roomLink.empty()) {
        const std::string link = "Друзьям: " + model.roomLink;
        const float linkW = measure(link, 0.56f * s) + 28.0f * s;
        rect(14.0f * s, 120.0f * s, linkW, 34.0f * s, 18, 22, 34, 190);
        text(28.0f * s, 127.0f * s, link, kAccent2R, kAccent2G, kAccent2B, 235, 0.56f * s);
    }

    // --- игроки ---
    const float listW = 250.0f * s;
    const float listX = w - listW - 14.0f * s;
    const float listH = 30.0f * s + static_cast<float>(model.players.size()) * 28.0f * s;
    rect(listX, 14.0f * s, listW, listH, kPanelR, kPanelG, kPanelB, 200);
    text(listX + 14.0f * s, 20.0f * s, "В мире", kMutedR, kMutedG, kMutedB, 255, 0.58f * s);
    float rowY = 50.0f * s;
    for (const HudPlayerRow& row : model.players) {
        const bool alive = row.alive;
        const uint8_t rr = row.self ? kAccentR : (alive ? kTextR : 150);
        const uint8_t gg = row.self ? kAccentG : (alive ? kTextG : 90);
        const uint8_t bb = row.self ? kAccentB : (alive ? kTextB : 100);
        text(listX + 14.0f * s, rowY, row.name, rr, gg, bb, 255, 0.62f * s);
        if (!alive) {
            text(listX + listW - 62.0f * s, rowY, "выбыл", kDangerR, kDangerG, kDangerB, 220, 0.54f * s);
        } else if (!model.creative) {
            const float hpW = 62.0f * s;
            rect(listX + listW - hpW - 14.0f * s, rowY + 8.0f * s, hpW, 8.0f * s, 40, 44, 56, 220);
            rect(listX + listW - hpW - 14.0f * s, rowY + 8.0f * s,
                 hpW * clampf(static_cast<float>(row.health) / 100.0f, 0.0f, 1.0f), 8.0f * s,
                 row.health > 50 ? 110 : 220, row.health > 50 ? 210 : 120, 110, 240);
        }
        rowY += 28.0f * s;
    }

    // --- журнал ---
    float logY = h - 220.0f * s;
    for (const std::string& line : model.log) {
        rect(14.0f * s, logY - 3.0f * s, measure(line, 0.6f * s) + 18.0f * s, 27.0f * s, 10, 12, 18, 150);
        text(22.0f * s, logY, line, 225, 232, 244, 230, 0.6f * s);
        logY += 30.0f * s;
    }

    // --- здоровье ---
    if (!model.creative) {
        const float hpW = 260.0f * s;
        const float hpX = (w - hpW) * 0.5f;
        const float hpY = h - 116.0f * s;
        rect(hpX, hpY, hpW, 14.0f * s, 24, 26, 34, 220);
        const float frac = clampf(static_cast<float>(model.health) / 100.0f, 0.0f, 1.0f);
        rect(hpX, hpY, hpW * frac, 14.0f * s, static_cast<uint8_t>(240 - 120 * frac), static_cast<uint8_t>(90 + 120 * frac),
             110, 245);
        rectOutline(hpX, hpY, hpW, 14.0f * s, 255, 255, 255, 50, 1.0f);
    }

    // --- горячая панель ---
    for (int i = 0; i < 9 && i < static_cast<int>(model.hotbar.size()); ++i) {
        const Rect slot = tl.hotbar[i];
        const bool selected = i == model.selectedSlot;
        rect(slot.x, slot.y, slot.w, slot.h, selected ? 44 : 20, selected ? 48 : 24, selected ? 58 : 34, 225);
        if (selected) rectOutline(slot.x, slot.y, slot.w, slot.h, kAccentR, kAccentG, kAccentB, 255, 2.5f);
        const BlockDef& def = blockDef(model.hotbar[static_cast<size_t>(i)].block);
        const float pad = 9.0f * s;
        rect(slot.x + pad, slot.y + pad, slot.w - pad * 2.0f, slot.h - pad * 2.0f, def.r, def.g, def.b, 240);
        text(slot.x + 4.0f * s, slot.y + 2.0f * s, std::to_string(i + 1), 255, 255, 255, 160, 0.42f * s);
    }

    // --- тач-кнопки ---
    if (model.showTouch) drawTouchControls(model, input);

    // --- вы мертвы ---
    if (model.dead) {
        rect(0.0f, 0.0f, w, h, 120, 16, 16, 60);
        textCentered(w * 0.5f, h * 0.38f, "ВЫ ВЫБЫЛИ", 255, 200, 200, 255, 1.10f * s);
        if (button(input, Rect{(w - 260.0f * s) * 0.5f, h * 0.5f, 260.0f * s, 60.0f * s}, "Возродиться", true)) {
            result.action = HudAction::Respawn;
        }
    }

    // --- всплывающее уведомление ---
    if (model.toastTimer > 0.0f && !model.toast.empty()) {
        const float tw = measure(model.toast, 0.72f * s) + 44.0f * s;
        rect((w - tw) * 0.5f, 140.0f * s, tw, 44.0f * s, 20, 24, 36, static_cast<uint8_t>(clampf(model.toastTimer * 200.0f, 0.0f, 215.0f)));
        textCentered(w * 0.5f, 150.0f * s, model.toast, 255, 236, 200, 255, 0.72f * s);
    }
}

void Hud::drawTouchControls(const HudModel& model, const HudInput& input) {
    (void)model;
    const float s = scale_;
    const TouchLayout& tl = layout(width_, height_);

    auto padButton = [&](const Rect& r, const std::string& label, uint8_t cr, uint8_t cg, uint8_t cb) {
        const bool active = r.contains(input.mouseX, input.mouseY) && input.down;
        rect(r.x, r.y, r.w, r.h, static_cast<uint8_t>(cr / 3 + (active ? 30 : 0)),
             static_cast<uint8_t>(cg / 3 + (active ? 30 : 0)), static_cast<uint8_t>(cb / 3 + (active ? 34 : 0)), 175);
        rectOutline(r.x, r.y, r.w, r.h, cr, cg, cb, static_cast<uint8_t>(active ? 255 : 170), 2.0f * s);
        const float ts = 0.5f * s;
        textCentered(r.cx(), r.y + (r.h - font_->lineHeight() * ts) * 0.5f, label, 240, 245, 255, 235, ts);
    };

    // стик
    const Rect& stick = tl.stick;
    rect(stick.x, stick.y, stick.w, stick.h, 18, 22, 32, 120);
    rectOutline(stick.x, stick.y, stick.w, stick.h, 120, 140, 170, 120, 2.0f * s);
    const float knob = 74.0f * s;
    rect(stick.cx() - knob * 0.5f, stick.cy() - knob * 0.5f, knob, knob, 90, 110, 145, 150);

    padButton(tl.jump, "ПРЫГ", 90, 200, 255);
    padButton(tl.breakBlock, "ЛОМ", 255, 150, 90);
    padButton(tl.placeBlock, "СТАВ", 120, 220, 150);
    padButton(tl.explode, "ТНТ", 240, 100, 90);
    padButton(tl.fly, "ПОЛЁТ", 180, 160, 255);
}

void Hud::drawPaused(const HudModel& model, const HudInput& input, HudResult& result) {
    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);
    rect(0.0f, 0.0f, w, h, 8, 10, 16, 190);

    const float pw = std::min(560.0f * scale_, w - 60.0f * scale_);
    const float ph = 520.0f * scale_;
    const float px = (w - pw) * 0.5f;
    const float py = (h - ph) * 0.5f;
    rect(px, py, pw, ph, kPanelR, kPanelG, kPanelB, 245);
    rectOutline(px, py, pw, ph, kAccentR, kAccentG, kAccentB, 220, 2.0f);

    textCentered(px + pw * 0.5f, py + 22.0f * scale_, "ПАУЗА", 255, 255, 255, 255, 1.2f * scale_);

    const float bw = pw - 56.0f * scale_;
    float by = py + 96.0f * scale_;
    if (button(input, Rect{px + 28.0f * scale_, by, bw, 58.0f * scale_}, "Продолжить", true)) {
        result.action = HudAction::Resume;
    }
    by += 70.0f * scale_;
    if (button(input, Rect{px + 28.0f * scale_, by, bw, 52.0f * scale_}, "Скопировать ссылку для друзей", false)) {
        result.action = HudAction::CopyLink;
    }
    by += 64.0f * scale_;

    text(px + 28.0f * scale_, by, "Режим мира", kMutedR, kMutedG, kMutedB, 255, 0.62f * scale_);
    by += 26.0f * scale_;
    const float third = (bw - 16.0f * scale_) / 3.0f;
    if (button(input, Rect{px + 28.0f * scale_, by, third, 48.0f * scale_}, "Песочница", false)) {
        result.action = HudAction::ModeSandbox;
    }
    if (button(input, Rect{px + 28.0f * scale_ + third + 8.0f * scale_, by, third, 48.0f * scale_},
               "Выживший", false)) {
        result.action = HudAction::ModeLastStanding;
    }
    if (button(input, Rect{px + 28.0f * scale_ + (third + 8.0f * scale_) * 2.0f, by, third, 48.0f * scale_},
               "База", false)) {
        result.action = HudAction::ModeBaseDefense;
    }

    by += 78.0f * scale_;
    if (button(input, Rect{px + 28.0f * scale_, by, bw, 52.0f * scale_}, "Выйти в главное меню", false)) {
        result.action = HudAction::QuitToMenu;
    }

    textCentered(px + pw * 0.5f, py + ph - 44.0f * scale_,
                 model.roomLink.empty() ? std::string("—") : model.roomLink, kAccent2R, kAccent2G, kAccent2B, 200,
                 0.56f * scale_);
}

}  // namespace razlom::client
