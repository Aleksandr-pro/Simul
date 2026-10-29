#include "simul/ui/widgets.hpp"
#include "simul/core/math_utils.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace simul::ui {

void UiContext::begin_input_frame(
    float dt_sec, float max_scroll_y, float viewport_top_y, float viewport_bottom_y) {
    const bool press_in_scroll_zone =
        pointer.press_start_y >= viewport_top_y && pointer.press_start_y <= viewport_bottom_y;

    if (pointer.down && active_slider_id_ < 0 && press_in_scroll_zone) {
        const float adx = std::fabs(pointer.x - pointer.press_start_x);
        const float ady = std::fabs(pointer.y - pointer.press_start_y);

        if (!pointer.is_scrolling && ady > 10.0f && ady > adx * 1.1f) {
            pointer.is_scrolling = true;
        }
        if (pointer.is_scrolling && std::fabs(pointer.drag_dy) > 0.01f) {
            scroll_y -= pointer.drag_dy;
            const float inst_vel = -pointer.drag_dy / std::max(0.005f, dt_sec);
            scroll_vel_y = scroll_vel_y * 0.45f + inst_vel * 0.55f;
        } else if (pointer.is_scrolling) {
            scroll_vel_y *= 0.80f;
        }
    } else if (!pointer.down) {
        scroll_y += scroll_vel_y * dt_sec;
        scroll_vel_y *= std::pow(0.035f, dt_sec);
        if (std::fabs(scroll_vel_y) < 5.0f) scroll_vel_y = 0.0f;
    }

    if (std::fabs(pointer.wheel_dy) > 0.01f) {
        scroll_y -= pointer.wheel_dy * 46.0f;
        scroll_vel_y = 0.0f;
    }

    scroll_y = core::clamp_val(scroll_y, 0.0f, std::max(0.0f, max_scroll_y));
    if (!pointer.down) {
        active_slider_id_ = -1;
    }
}

void UiContext::end_input_frame() noexcept {
    pointer.just_pressed = false;
    pointer.just_released = false;
    pointer.drag_dy = 0.0f;
    pointer.wheel_dy = 0.0f;
    if (!pointer.down) {
        pointer.is_scrolling = false;
    }
}

void UiContext::begin_scroll_region(float top_y, float bottom_y) noexcept {
    in_scroll_region_ = true;
    scroll_top_y_ = top_y;
    scroll_bottom_y_ = bottom_y;
}

void UiContext::end_scroll_region() noexcept {
    in_scroll_region_ = false;
}

bool UiContext::hit_rect(float x, float y, float w, float h) const noexcept {
    if (in_scroll_region_) {
        if (pointer.y < scroll_top_y_ || pointer.y > scroll_bottom_y_) {
            return false;
        }
    }
    return pointer.x >= x && pointer.x <= (x + w) &&
           pointer.y >= y && pointer.y <= (y + h);
}

bool UiContext::button(
    Renderer2D& r, float x, float y, float w, float h,
    std::string_view label, bool primary, bool disabled) {
    const bool inside = hit_rect(x, y, w, h);
    const bool hovered = inside && (!pointer.is_touch || pointer.down);
    const bool pressed_inside = inside && pointer.down && !disabled && !pointer.is_scrolling;
    const bool clicked =
        inside && pointer.just_released && !disabled && !pointer.is_scrolling && active_slider_id_ < 0;

    Color bg = primary ? Color::rgba(14, 138, 228) : Color::rgba(22, 33, 62);
    if (disabled) {
        bg = Color::rgba(19, 25, 43, 145);
    } else if (pressed_inside) {
        bg = primary ? Color::rgba(8, 102, 184) : Color::rgba(38, 56, 98);
    } else if (hovered) {
        bg = primary ? Color::rgba(28, 158, 246) : Color::rgba(29, 44, 80);
    }

    const float radius = std::min(12.0f, h * 0.34f);
    r.draw_rounded_rect(x, y, w, h, radius, bg);
    if (primary && !disabled) {
        r.draw_rect_outline(x, y, w, h, 1.0f, Color::rgba(125, 211, 252, 95));
    } else {
        r.draw_rect_outline(x, y, w, h, 1.0f,
                            hovered && !disabled ? Color::rgba(56, 189, 248) : Color::rgba(71, 85, 105, 135));
    }

    float font_sz = primary ? 15.0f : 14.0f;
    float tw = r.measure_text(label, font_sz);
    if (tw > w - 18.0f && w > 28.0f) {
        font_sz = std::max(10.5f, font_sz * ((w - 18.0f) / tw));
        tw = r.measure_text(label, font_sz);
    }
    const Color tc = disabled ? Color::rgba(100, 116, 139) : Color::rgba(248, 250, 252);
    r.draw_text(x + (w - tw) * 0.5f, y + (h - font_sz * 1.18f) * 0.5f, label, font_sz, tc);
    return clicked;
}

bool UiContext::chip_button(
    Renderer2D& r, float x, float y, float w, float h,
    std::string_view label, bool active, Color accent) {
    const bool inside = hit_rect(x, y, w, h);
    const bool hovered = inside && (!pointer.is_touch || pointer.down);
    const bool clicked = inside && pointer.just_released && !pointer.is_scrolling && active_slider_id_ < 0;

    const Color bg = active
                         ? accent.with_alpha(48)
                         : (hovered ? Color::rgba(28, 41, 74) : Color::rgba(16, 24, 46));
    r.draw_rounded_rect(x, y, w, h, h * 0.36f, bg);
    r.draw_rect_outline(x, y, w, h, active ? 1.5f : 1.0f,
                        active ? accent : Color::rgba(71, 85, 105, 120));

    float font_sz = 13.5f;
    float tw = r.measure_text(label, font_sz);
    if (tw > w - 14.0f && w > 24.0f) {
        font_sz = std::max(10.0f, font_sz * ((w - 14.0f) / tw));
        tw = r.measure_text(label, font_sz);
    }
    r.draw_text(x + (w - tw) * 0.5f,
                y + (h - font_sz * 1.18f) * 0.5f,
                label,
                font_sz,
                active ? Color::rgba(255, 255, 255) : Color::rgba(186, 199, 218));
    return clicked;
}

bool UiContext::card_option(
    Renderer2D& r, float x, float y, float w, float h,
    std::string_view title, std::string_view subtitle,
    bool selected, Color accent, bool disabled) {
    const bool inside = hit_rect(x, y, w, h);
    const bool pressed = inside && pointer.down && !disabled && !pointer.is_scrolling;
    const bool clicked = inside && pointer.just_released && !disabled && !pointer.is_scrolling && active_slider_id_ < 0;

    Color bg = selected ? Color::rgba(22, 42, 84) : Color::rgba(16, 25, 48);
    if (pressed) bg = Color::rgba(30, 52, 98);
    if (disabled) bg = Color::rgba(15, 21, 38, 150);

    r.draw_rounded_rect(x, y, w, h, 12.0f, bg);
    r.draw_rect_outline(x, y, w, h, selected ? 1.6f : 1.0f,
                        selected ? accent : Color::rgba(64, 80, 112, 125));
    if (selected) {
        r.draw_rect(x, y + 8.0f, 4.0f, h - 16.0f, accent);
    }

    r.draw_text_fit(x + 14.0f, y + 9.0f, w - 28.0f, title, 14.0f,
                    disabled ? Color::rgba(100, 116, 139) : Color::rgba(248, 250, 252));
    if (!subtitle.empty()) {
        r.draw_text_wrapped(x + 14.0f, y + 29.0f, w - 28.0f, subtitle, 12.0f,
                            selected ? accent : Color::rgba(148, 163, 184));
    }
    return clicked;
}

void UiContext::draw_nav_icon(Renderer2D& r, int icon_id, float cx, float cy, Color col) const {
    switch (icon_id) {
        case 0: { // Студия — камера / кнопка Play в рамке
            r.draw_rounded_rect(cx - 10.0f, cy - 7.0f, 20.0f, 14.0f, 3.5f, col.with_alpha(45));
            r.draw_rect_outline(cx - 10.0f, cy - 7.0f, 20.0f, 14.0f, 1.6f, col);
            r.draw_triangle(cx - 3.0f, cy - 4.0f, cx + 5.0f, cy, cx - 3.0f, cy + 4.0f, col);
            break;
        }
        case 1: { // Алгоритм — восходящая кривая волн
            r.draw_line(cx - 9.0f, cy + 5.0f, cx - 3.0f, cy + 1.0f, 2.0f, col);
            r.draw_line(cx - 3.0f, cy + 1.0f, cx + 2.0f, cy + 3.0f, 2.0f, col);
            r.draw_line(cx + 2.0f, cy + 3.0f, cx + 9.0f, cy - 6.0f, 2.2f, col);
            r.draw_circle(cx + 9.0f, cy - 6.0f, 2.6f, col, 10);
            break;
        }
        case 2: { // Аудитория — силуэты людей
            r.draw_circle(cx - 4.0f, cy - 3.5f, 3.3f, col, 12);
            r.draw_rounded_rect(cx - 9.5f, cy + 1.5f, 11.0f, 5.5f, 2.8f, col);
            r.draw_circle(cx + 5.0f, cy - 2.5f, 2.7f, col.with_alpha(190), 10);
            r.draw_rounded_rect(cx + 1.0f, cy + 2.2f, 8.5f, 4.8f, 2.2f, col.with_alpha(190));
            break;
        }
        case 3: { // Карьера — портфель / молния
            r.draw_rounded_rect(cx - 9.0f, cy - 4.5f, 18.0f, 12.0f, 2.5f, col.with_alpha(40));
            r.draw_rect_outline(cx - 9.0f, cy - 4.5f, 18.0f, 12.0f, 1.6f, col);
            r.draw_rect_outline(cx - 4.0f, cy - 7.5f, 8.0f, 3.5f, 1.4f, col);
            r.draw_rect(cx - 2.5f, cy + 0.5f, 5.0f, 2.2f, col);
            break;
        }
        default: { // Мир — кубок / глобус лиги
            r.draw_circle(cx, cy, 8.0f, col.with_alpha(38), 16);
            r.draw_line(cx - 8.0f, cy, cx + 8.0f, cy, 1.4f, col);
            r.draw_line(cx, cy - 8.0f, cx, cy + 8.0f, 1.4f, col);
            r.draw_rect_outline(cx - 5.5f, cy - 5.5f, 11.0f, 11.0f, 1.3f, col.with_alpha(180));
            break;
        }
    }
}

bool UiContext::nav_tab_item(
    Renderer2D& r, int icon_id, float x, float y, float w, float h,
    std::string_view label, bool active, bool has_badge, Color badge_col) {
    const bool inside = pointer.x >= x && pointer.x <= (x + w) &&
                        pointer.y >= y && pointer.y <= (y + h);
    const bool clicked = inside && pointer.just_released && !pointer.is_scrolling && active_slider_id_ < 0;

    if (active) {
        r.draw_rounded_rect(x + 3.0f, y + 3.0f, w - 6.0f, h - 6.0f, 12.0f, Color::rgba(30, 58, 138, 175));
        r.draw_rounded_rect(x + w * 0.25f, y + 2.0f, w * 0.50f, 3.0f, 1.5f, Color::rgba(56, 189, 248));
    }

    const Color ic = active ? Color::rgba(56, 189, 248) : Color::rgba(148, 163, 184);
    const float icon_cy = y + h * 0.36f;
    draw_nav_icon(r, icon_id, x + w * 0.5f, icon_cy, ic);

    if (has_badge) {
        r.draw_circle(x + w * 0.5f + 11.0f, icon_cy - 6.0f, 4.2f, badge_col, 10);
    }

    float font_sz = 11.5f;
    float tw = r.measure_text(label, font_sz);
    if (tw > w - 6.0f && w > 16.0f) {
        font_sz = std::max(9.5f, font_sz * ((w - 6.0f) / tw));
        tw = r.measure_text(label, font_sz);
    }
    r.draw_text(x + (w - tw) * 0.5f,
                y + h - font_sz * 1.38f,
                label,
                font_sz,
                active ? Color::rgba(248, 250, 252) : Color::rgba(148, 163, 184));
    return clicked;
}

bool UiContext::slider_float(
    Renderer2D& r, int id, float x, float y, float w,
    std::string_view label, float& value, float min_v, float max_v) {
    char val_buf[32];
    if (max_v <= 1.05f) {
        std::snprintf(val_buf, sizeof(val_buf), "%d%%", static_cast<int>(std::round(value * 100.0f)));
    } else {
        std::snprintf(val_buf, sizeof(val_buf), "$%.0f", static_cast<double>(value));
    }

    const float vw = r.measure_text(val_buf, 13.5f);
    r.draw_text_fit(x, y, w - vw - 10.0f, label, 13.5f, Color::rgba(226, 232, 240));
    r.draw_text(x + w - vw, y, val_buf, 13.5f, Color::rgba(56, 189, 248));

    const float track_y = y + 24.0f;
    const float track_h = 10.0f;
    const bool in_clip = !in_scroll_region_ || (pointer.y >= scroll_top_y_ && pointer.y <= scroll_bottom_y_);
    const bool hit_zone =
        in_clip &&
        pointer.x >= (x - 10.0f) && pointer.x <= (x + w + 10.0f) &&
        pointer.y >= (track_y - 16.0f) && pointer.y <= (track_y + track_h + 16.0f);

    if (pointer.just_pressed && hit_zone) {
        active_slider_id_ = id;
        pointer.is_scrolling = false;
    }

    bool changed = false;
    if (active_slider_id_ == id && pointer.down) {
        const float t = core::clamp_val((pointer.x - x) / std::max(1.0f, w), 0.0f, 1.0f);
        value = min_v + (max_v - min_v) * t;
        changed = true;
    }

    const float ratio = core::clamp_val((value - min_v) / std::max(0.0001f, max_v - min_v), 0.0f, 1.0f);
    r.draw_rounded_rect(x, track_y, w, track_h, 5.0f, Color::rgba(30, 41, 59));
    if (ratio > 0.01f) {
        r.draw_rounded_rect(x, track_y, w * ratio, track_h, 5.0f, Color::rgba(14, 165, 233));
    }
    const float thumb_x = x + w * ratio;
    const float thumb_y = track_y + track_h * 0.5f;
    if (active_slider_id_ == id) {
        r.draw_circle(thumb_x, thumb_y, 14.0f, Color::rgba(56, 189, 248, 75), 16);
    }
    r.draw_circle(thumb_x, thumb_y, 9.5f, Color::rgba(255, 255, 255), 14);
    r.draw_circle(thumb_x, thumb_y, 4.5f, Color::rgba(14, 165, 233), 10);
    return changed;
}

} // namespace simul::ui
