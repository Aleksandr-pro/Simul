#include "studio_scene.hpp"
#include "simul/core/math_utils.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace simul::game {

namespace {

constexpr const char* kStickerTexts[4] = {
    "",
    "ПРОВЕРКА!",
    "БЕЗ ШАНСОВ?!",
    "ШОК ФИНАЛ!",
};

} // namespace

void apply_thumbnail_design_to_variant(
    const ThumbnailDesign& design,
    core::NicheId niche,
    core::PackagingVariant& out_variant) noexcept {
    // Базовый визуальный контраст от цветовой палитры
    static constexpr float kPaletteContrast[4] = {0.82f, 0.90f, 0.76f, 0.85f};
    // Базовая интрига от композиции кадра
    static constexpr float kFocalCuriosity[4] = {0.80f, 0.88f, 0.86f, 0.78f};
    // Добавка от стикера-плашки
    static constexpr float kStickerCuriosity[4] = {-0.04f, 0.06f, 0.10f, 0.12f};
    static constexpr float kStickerClickbait[4] = {0.0f, 0.03f, 0.08f, 0.22f};

    const int pal = std::clamp(design.palette_idx, 0, 3);
    const int foc = std::clamp(design.focal_idx, 0, 3);
    const int stk = std::clamp(design.sticker_idx, 0, 3);

    // Бонус попадания стиля обложки во вкусы конкретной ниши
    float niche_match = 0.0f;
    if (niche == core::NicheId::Tech && (foc == 1 || foc == 3)) niche_match = 0.07f;
    if (niche == core::NicheId::Gaming && (pal == 0 || pal == 1)) niche_match = 0.07f;
    if (niche == core::NicheId::Edutainment && (pal == 2 || foc == 3)) niche_match = 0.08f;
    if (niche == core::NicheId::Entertainment && (foc == 0 || foc == 1)) niche_match = 0.08f;
    if (niche == core::NicheId::Lifestyle && (pal == 2 || pal == 3)) niche_match = 0.06f;

    out_variant.visual_contrast = core::clamp_val(kPaletteContrast[pal] + niche_match, 0.20f, 0.99f);
    out_variant.curiosity_gap = core::clamp_val(
        kFocalCuriosity[foc] + kStickerCuriosity[stk] + niche_match * 0.5f, 0.20f, 0.99f);
    out_variant.clickbait_level = core::clamp_val(
        design.clickbait + kStickerClickbait[stk], 0.0f, 0.96f);
}

void draw_creator_studio_room(
    ui::Renderer2D& r,
    float x, float y, float w, float h,
    float anim_time_sec,
    std::uint8_t equipment_tier,
    std::size_t team_count,
    float creator_energy,
    bool is_live_airing) {
    using ui::Color;

    // Задняя стена комнаты (от скромной спальни до неонового павильона)
    const Color wall_top = equipment_tier >= 3 ? Color::rgba(19, 17, 48) : Color::rgba(13, 20, 38);
    const Color wall_bot = equipment_tier >= 3 ? Color::rgba(11, 18, 42) : Color::rgba(9, 14, 28);
    r.draw_rounded_rect(x, y, w, h, 16.0f, wall_bot);
    r.draw_gradient_v(x + 2.0f, y + 2.0f, w - 4.0f, h * 0.72f, wall_top, wall_bot);
    r.draw_rect_outline(x, y, w, h, 1.2f,
                        equipment_tier >= 4 ? Color::rgba(168, 85, 247, 110) : Color::rgba(56, 189, 248, 70));

    // Пол студии
    const float floor_y = y + h * 0.74f;
    r.draw_rounded_rect(x + 2.0f, floor_y, w - 4.0f, h - (floor_y - y) - 2.0f, 14.0f, Color::rgba(15, 23, 42));
    r.draw_rect(x + 2.0f, floor_y, w - 4.0f, 2.0f, Color::rgba(56, 189, 248, 55));

    // Ночное окно города слева
    const float win_x = x + 16.0f;
    const float win_y = y + 16.0f;
    const float win_w = std::min(78.0f, w * 0.22f);
    const float win_h = h * 0.44f;
    r.draw_rounded_rect(win_x, win_y, win_w, win_h, 6.0f, Color::rgba(6, 11, 26));
    r.draw_rect_outline(win_x, win_y, win_w, win_h, 1.5f, Color::rgba(51, 65, 85));
    // Силуэты небоскрёбов и мерцающие огни в окне
    r.draw_rect(win_x + 6.0f, win_y + win_h - 24.0f, 14.0f, 22.0f, Color::rgba(17, 26, 48));
    r.draw_rect(win_x + 23.0f, win_y + win_h - 34.0f, 18.0f, 32.0f, Color::rgba(22, 34, 62));
    r.draw_rect(win_x + 44.0f, win_y + win_h - 20.0f, 16.0f, 18.0f, Color::rgba(17, 26, 48));
    const float twinkle = 0.55f + 0.45f * std::sin(anim_time_sec * 2.6f);
    r.draw_circle(win_x + 14.0f, win_y + 12.0f, 1.6f, Color::rgba(250, 204, 21, static_cast<std::uint8_t>(220 * twinkle)), 8);
    r.draw_circle(win_x + win_w - 14.0f, win_y + 16.0f, 1.5f, Color::rgba(125, 211, 252, 200), 8);

    // Неоновая RGB-лента и акустические панели с Уровня 2+
    if (equipment_tier >= 2) {
        const float glow_a = 120.0f + 55.0f * std::sin(anim_time_sec * 3.1f);
        r.draw_rect(x + 14.0f, y + 7.0f, w - 28.0f, 3.0f,
                    equipment_tier >= 4
                        ? Color::rgba(236, 72, 153, static_cast<std::uint8_t>(glow_a))
                        : Color::rgba(56, 189, 248, static_cast<std::uint8_t>(glow_a)));

        // Акустические панели на стене справа
        const float px = x + w - 82.0f;
        for (int i = 0; i < 3; ++i) {
            r.draw_rounded_rect(px + static_cast<float>(i) * 22.0f, y + 20.0f + static_cast<float>(i % 2) * 8.0f,
                                18.0f, 24.0f, 4.0f, Color::rgba(30, 41, 68));
        }
    }

    // Если есть нанятые сотрудники — рисуем рабочее место монтажёра справа!
    if (team_count > 0) {
        const float tx = x + w - 74.0f;
        const float ty = floor_y - 22.0f;
        const float bob_t = std::sin(anim_time_sec * 3.4f + 1.2f) * 1.4f;
        r.draw_circle(tx + 24.0f, ty - 14.0f + bob_t, 7.5f, Color::rgba(253, 224, 195), 12);
        r.draw_rounded_rect(tx + 15.0f, ty - 6.0f + bob_t, 18.0f, 18.0f, 5.0f, Color::rgba(99, 102, 241));
        r.draw_rounded_rect(tx + 6.0f, ty + 6.0f, 42.0f, 5.0f, 2.0f, Color::rgba(51, 65, 85));
        r.draw_rounded_rect(tx + 26.0f, ty - 6.0f, 18.0f, 12.0f, 2.5f, Color::rgba(14, 165, 233));
    }

    // Центральный стол и персонаж-блогер
    const float cx = x + w * 0.50f - (team_count > 0 ? 14.0f : 0.0f);
    const float desk_y = floor_y - 16.0f;
    const float bob = std::sin(anim_time_sec * (is_live_airing ? 5.5f : 2.8f)) * 1.8f;

    // Спинка кресла (геймерское кресло с Ур. 2+)
    if (equipment_tier >= 2) {
        r.draw_rounded_rect(cx - 16.0f, desk_y - 38.0f, 32.0f, 38.0f, 9.0f, Color::rgba(30, 58, 138));
        r.draw_rounded_rect(cx - 12.0f, desk_y - 34.0f, 24.0f, 30.0f, 6.0f, Color::rgba(17, 24, 39));
    }

    // Персонаж (торс + голова + наушники с Ур. 2+)
    const Color shirt_col = creator_energy > 35.0f ? Color::rgba(14, 165, 233) : Color::rgba(244, 63, 94);
    r.draw_rounded_rect(cx - 13.0f, desk_y - 24.0f + bob, 26.0f, 24.0f, 7.0f, shirt_col);
    r.draw_circle(cx, desk_y - 34.0f + bob, 9.5f, Color::rgba(254, 215, 170), 14);
    if (equipment_tier >= 2) {
        r.draw_rect(cx - 11.0f, desk_y - 37.0f + bob, 3.5f, 8.0f, Color::rgba(56, 189, 248));
        r.draw_rect(cx + 7.5f, desk_y - 37.0f + bob, 3.5f, 8.0f, Color::rgba(56, 189, 248));
    }

    // Столешница и ножки
    const float desk_w = std::min(156.0f, w * 0.46f);
    r.draw_rounded_rect(cx - desk_w * 0.5f, desk_y, desk_w, 7.0f, 3.0f,
                        equipment_tier >= 3 ? Color::rgba(56, 72, 108) : Color::rgba(71, 55, 42));
    r.draw_rect(cx - desk_w * 0.42f, desk_y + 7.0f, 5.0f, 16.0f, Color::rgba(30, 41, 59));
    r.draw_rect(cx + desk_w * 0.42f - 5.0f, desk_y + 7.0f, 5.0f, 16.0f, Color::rgba(30, 41, 59));

    // Мониторы на столе (1 ноутбук на Ур.1 -> 2 широких монитора с таймлайном на Ур.3+)
    if (equipment_tier <= 2) {
        r.draw_rounded_rect(cx - 20.0f, desk_y - 20.0f, 40.0f, 20.0f, 3.0f, Color::rgba(15, 23, 42));
        r.draw_rect_outline(cx - 20.0f, desk_y - 20.0f, 40.0f, 20.0f, 1.4f, Color::rgba(56, 189, 248));
        const float bar_w = 10.0f + 18.0f * (0.5f + 0.5f * std::sin(anim_time_sec * 4.0f));
        r.draw_rect(cx - 15.0f, desk_y - 14.0f, bar_w, 3.0f, Color::rgba(56, 189, 248));
        r.draw_rect(cx - 15.0f, desk_y - 8.0f, 24.0f, 3.0f, Color::rgba(129, 140, 248));
    } else {
        r.draw_rounded_rect(cx - 42.0f, desk_y - 23.0f, 40.0f, 22.0f, 3.0f, Color::rgba(12, 19, 36));
        r.draw_rect_outline(cx - 42.0f, desk_y - 23.0f, 40.0f, 22.0f, 1.4f, Color::rgba(56, 189, 248));
        r.draw_rounded_rect(cx + 2.0f, desk_y - 23.0f, 40.0f, 22.0f, 3.0f, Color::rgba(12, 19, 36));
        r.draw_rect_outline(cx + 2.0f, desk_y - 23.0f, 40.0f, 22.0f, 1.4f, Color::rgba(168, 85, 247));
        // Дорожки таймлайна на экранах
        r.draw_rect(cx - 37.0f, desk_y - 17.0f, 28.0f, 3.0f, Color::rgba(34, 197, 94));
        r.draw_rect(cx - 37.0f, desk_y - 11.0f, 22.0f, 3.0f, Color::rgba(56, 189, 248));
        r.draw_rect(cx + 7.0f, desk_y - 16.0f, 28.0f, 8.0f, Color::rgba(236, 72, 153, 180));
    }

    // Кольцевая LED-лампа (Ур.2+) и кинокамера на штативе (Ур.4+)
    if (equipment_tier >= 2) {
        const float lx = cx - desk_w * 0.44f;
        const float ly = desk_y - 30.0f;
        r.draw_line(lx, desk_y, lx, ly + 8.0f, 2.2f, Color::rgba(100, 116, 139));
        r.draw_circle(lx, ly, 11.0f, Color::rgba(56, 189, 248, 55), 16);
        r.draw_circle(lx, ly, 7.5f, Color::rgba(224, 242, 254), 14);
        r.draw_circle(lx, ly, 4.2f, wall_top, 12);
    }
    if (equipment_tier >= 4) {
        const float cam_x = cx + desk_w * 0.48f + 10.0f;
        const float cam_y = desk_y - 22.0f;
        r.draw_line(cam_x, cam_y + 8.0f, cam_x - 8.0f, floor_y + 10.0f, 2.2f, Color::rgba(148, 163, 184));
        r.draw_line(cam_x, cam_y + 8.0f, cam_x + 8.0f, floor_y + 10.0f, 2.2f, Color::rgba(148, 163, 184));
        r.draw_rounded_rect(cam_x - 10.0f, cam_y - 6.0f, 20.0f, 13.0f, 3.0f, Color::rgba(30, 41, 59));
        r.draw_circle(cam_x - 11.0f, cam_y + 0.5f, 4.0f, Color::rgba(56, 189, 248), 10);
    }

    // Нижняя инфо-строка прямо поверх пола студии
    char studio_lbl[128];
    static constexpr const char* kTierNames[6] = {
        "Спальня с ноутбуком",
        "Спальня с ноутбуком",
        "Домашний стримерский угол",
        "Неоновая студия (2 экрана)",
        "Продакшн-павильон 4K",
        "Медиа-комплекс ВИРАЛ",
    };
    const int ti = std::clamp<int>(equipment_tier, 1, 5);
    std::snprintf(studio_lbl, sizeof(studio_lbl), "Студия Ур.%u: %s • Команда: %zu",
                  static_cast<unsigned>(equipment_tier), kTierNames[ti], team_count);
    r.draw_text_fit(x + 12.0f, h + y - 22.0f, w - 24.0f, studio_lbl, 12.0f, Color::rgba(203, 213, 225));

    // Индикатор REC / Эфир в правом верхнем углу
    if (is_live_airing) {
        const bool blink = std::fmod(anim_time_sec * 2.5f, 1.0f) < 0.6f;
        r.draw_rounded_rect(x + w - 108.0f, y + 10.0f, 96.0f, 22.0f, 11.0f, Color::rgba(127, 29, 29, 220));
        if (blink) r.draw_circle(x + w - 95.0f, y + 21.0f, 4.0f, Color::rgba(248, 113, 113), 10);
        r.draw_text(x + w - 85.0f, y + 13.0f, "В ЭФИРЕ 48Ч", 11.0f, Color::rgba(254, 226, 226));
    }
}

void draw_thumbnail_canvas(
    ui::Renderer2D& r,
    float x, float y, float w,
    const ThumbnailDesign& design,
    std::string_view video_title,
    std::string_view channel_name,
    char variant_letter,
    float predicted_ctr) {
    using ui::Color;

    const float thumb_h = std::round(w * 0.50f); // компактный 2:1 / 16:9 смартфонный холст
    static constexpr Color kTopCols[4] = {
        Color::rgba(15, 32, 92),
        Color::rgba(124, 25, 29),
        Color::rgba(10, 62, 54),
        Color::rgba(76, 29, 115),
    };
    static constexpr Color kBotCols[4] = {
        Color::rgba(8, 92, 148),
        Color::rgba(194, 65, 12),
        Color::rgba(17, 24, 39),
        Color::rgba(161, 98, 7),
    };

    const int pal = std::clamp(design.palette_idx, 0, 3);
    const int foc = std::clamp(design.focal_idx, 0, 3);
    const int stk = std::clamp(design.sticker_idx, 0, 3);

    r.draw_rounded_rect(x, y, w, thumb_h + 58.0f, 14.0f, Color::rgba(11, 17, 34));
    r.draw_gradient_v(x + 2.0f, y + 2.0f, w - 4.0f, thumb_h, kTopCols[pal], kBotCols[pal]);
    r.draw_rect_outline(x, y, w, thumb_h + 58.0f, 1.2f, Color::rgba(56, 189, 248, 80));

    // Фокусная композиция в центре превью
    const float cx = x + w * 0.5f;
    const float cy = y + thumb_h * 0.52f;
    if (foc == 0) {
        // Крупный план персонажа с эмоцией
        r.draw_circle(cx + w * 0.18f, cy, thumb_h * 0.34f, Color::rgba(255, 255, 255, 28), 18);
        r.draw_circle(cx + w * 0.18f, cy - 8.0f, 20.0f, Color::rgba(254, 215, 170), 16);
        r.draw_rounded_rect(cx + w * 0.18f - 24.0f, cy + 12.0f, 48.0f, 28.0f, 9.0f, Color::rgba(56, 189, 248));
        r.draw_circle(cx + w * 0.18f, cy - 4.0f, 4.5f, Color::rgba(127, 29, 29), 10); // удивлённый рот "О"
    } else if (foc == 1) {
        // Сравнение ДО / ПОСЛЕ ($5 vs $500)
        r.draw_rect(cx - 2.0f, y + 8.0f, 4.0f, thumb_h - 16.0f, Color::rgba(255, 255, 255, 210));
        r.draw_rounded_rect(cx - 20.0f, cy - 13.0f, 40.0f, 26.0f, 8.0f, Color::rgba(250, 204, 21));
        r.draw_text(cx - 11.0f, cy - 8.0f, "VS", 14.5f, Color::rgba(15, 23, 42));
    } else if (foc == 2) {
        // Красная стрелка и секретный объект
        r.draw_circle(cx + w * 0.20f, cy, 24.0f, Color::rgba(244, 63, 94, 80), 16);
        r.draw_rect_outline(cx + w * 0.20f - 20.0f, cy - 20.0f, 40.0f, 40.0f, 2.5f, Color::rgba(250, 204, 21));
        r.draw_line(cx - 36.0f, cy - 18.0f, cx + w * 0.20f - 24.0f, cy - 4.0f, 5.0f, Color::rgba(244, 63, 94));
        r.draw_triangle(cx + w * 0.20f - 22.0f, cy - 12.0f,
                        cx + w * 0.20f - 12.0f, cy - 1.0f,
                        cx + w * 0.20f - 28.0f, cy + 4.0f,
                        Color::rgba(244, 63, 94));
    } else {
        // Взрывной график роста
        r.draw_line(x + 26.0f, y + thumb_h - 22.0f, cx - 10.0f, cy + 8.0f, 4.5f, Color::rgba(56, 189, 248));
        r.draw_line(cx - 10.0f, cy + 8.0f, cx + 18.0f, cy + 16.0f, 4.5f, Color::rgba(56, 189, 248));
        r.draw_line(cx + 18.0f, cy + 16.0f, x + w - 34.0f, y + 24.0f, 5.0f, Color::rgba(34, 197, 94));
        r.draw_circle(x + w - 34.0f, y + 24.0f, 7.0f, Color::rgba(34, 197, 94), 12);
    }

    // Бейдж варианта A / B и прогноз CTR
    char badge[64];
    std::snprintf(badge, sizeof(badge), "Обложка %c • CTR ~%.1f%%",
                  variant_letter, static_cast<double>(predicted_ctr * 100.0f));
    r.draw_rounded_rect(x + 10.0f, y + 10.0f, 152.0f, 24.0f, 7.0f, Color::rgba(9, 14, 28, 215));
    r.draw_text(x + 18.0f, y + 14.0f, badge, 12.0f, Color::rgba(56, 189, 248));

    // Яркая текстовая плашка на самой обложке
    if (stk > 0) {
        const Color plate_bg = stk == 3 ? Color::rgba(225, 29, 72) : Color::rgba(250, 204, 21);
        const Color plate_fg = stk == 3 ? Color::rgba(255, 255, 255) : Color::rgba(15, 23, 42);
        const float pw = r.measure_text(kStickerTexts[stk], 15.5f) + 22.0f;
        r.draw_rounded_rect(x + 12.0f, y + thumb_h - 38.0f, pw, 28.0f, 6.0f, plate_bg);
        r.draw_text(x + 23.0f, y + thumb_h - 33.0f, kStickerTexts[stk], 15.5f, plate_fg);
    }

    // Таймкод в правом нижнем углу превью
    r.draw_rounded_rect(x + w - 54.0f, y + thumb_h - 26.0f, 44.0f, 19.0f, 4.0f, Color::rgba(5, 8, 18, 225));
    r.draw_text(x + w - 48.0f, y + thumb_h - 24.0f, "12:48", 11.0f, Color::rgba(248, 250, 252));

    // Нижняя строка предпросмотра в мобильной ленте (аватарка + заголовок + канал)
    r.draw_circle(x + 22.0f, y + thumb_h + 29.0f, 13.0f, Color::rgba(30, 64, 175), 14);
    r.draw_triangle(x + 19.0f, y + thumb_h + 24.0f, x + 27.0f, y + thumb_h + 29.0f, x + 19.0f, y + thumb_h + 34.0f,
                    Color::rgba(56, 189, 248));
    r.draw_text_fit(x + 42.0f, y + thumb_h + 11.0f, w - 54.0f, video_title, 13.5f, Color::rgba(248, 250, 252));
    r.draw_text_fit(x + 42.0f, y + thumb_h + 32.0f, w - 54.0f,
                    std::string(channel_name) + " • Предпросмотр в ленте смартфона",
                    11.5f, Color::rgba(148, 163, 184));
}

} // namespace simul::game
