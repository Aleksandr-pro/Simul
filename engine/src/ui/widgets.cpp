#include "simul/ui/widgets.hpp"
#include "simul/core/math_utils.hpp"
#include <cmath>
#include <cstdio>

namespace simul::ui {

void UiContext::begin_input_frame(float dt_sec, float max_scroll_y) {
    if (pointer.down && active_slider_id_ < 0) {
        const float total_move = std::fabs(pointer.y - pointer.press_start_y) +
                                 std::fabs(pointer.x - pointer.press_start_x);
        if (total_move > 10.0f) {
            pointer.is_scrolling = true;
        }
        if (pointer.is_scrolling && std::fabs(pointer.drag_dy) > 0.01f) {
            scroll_y -= pointer.drag_dy;
            scroll_vel_y = -pointer.drag_dy / std::max(0.004f, dt_sec);
        }
    } else {
        scroll_y += scroll_vel_y * dt_sec;
        scroll_vel_y *= std::pow(0.05f, dt_sec);
        if (std::fabs(scroll_vel_y) < 4.0f) scroll_vel_y = 0.0f;
    }

    if (std::fabs(pointer.wheel_dy) > 0.01f) {
        scroll_y -= pointer.wheel_dy * 38.0f;
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

bool UiContext::button(
    Renderer2D& r, float x, float y, float w, float h,
    std::string_view label, bool primary, bool disabled) {
    const bool hovered =
        pointer.x >= x && pointer.x <= (x + w) &&
        pointer.y >= y && pointer.y <= (y + h);

    const bool pressed_inside = hovered && pointer.down && !disabled && !pointer.is_scrolling;
    const bool clicked =
        hovered && pointer.just_released && !disabled && !pointer.is_scrolling && active_slider_id_ < 0;

    Color bg = primary ? Color::rgba(14, 132, 218) : Color::rgba(24, 34, 62);
    if (disabled) {
        bg = Color::rgba(20, 26, 44, 140);
    } else if (pressed_inside) {
        bg = primary ? Color::rgba(8, 102, 180) : Color::rgba(36, 52, 92);
    } else if (hovered) {
        bg = primary ? Color::rgba(28, 152, 240) : Color::rgba(30, 44, 78);
    }

    r.draw_rounded_rect(x, y, w, h, 9.0f, bg);
    if (!primary) {
        r.draw_rect_outline(x, y, w, h, 1.0f,
                            hovered && !disabled ? Color::rgba(56, 189, 248) : Color::rgba(71, 85, 105, 130));
    }

    const float font_sz = 14.0f;
    const float tw = r.measure_text(label, font_sz);
    const Color tc = disabled ? Color::rgba(100, 116, 139) : Color::rgba(248, 250, 252);
    r.draw_text(x + (w - tw) * 0.5f, y + (h - font_sz * 1.15f) * 0.5f, label, font_sz, tc);
    return clicked;
}

bool UiContext::tab_button(
    Renderer2D& r, float x, float y, float w, float h,
    std::string_view label, bool active) {
    const bool hovered =
        pointer.x >= x && pointer.x <= (x + w) &&
        pointer.y >= y && pointer.y <= (y + h);
    const bool clicked = hovered && pointer.just_released && !pointer.is_scrolling;

    const Color bg = active ? Color::rgba(30, 58, 138) : (hovered ? Color::rgba(20, 30, 56) : Color::rgba(15, 22, 41));
    r.draw_rounded_rect(x, y, w, h, 9.0f, bg);
    if (active) {
        r.draw_rect(x + 10.0f, y + h - 3.0f, w - 20.0f, 3.0f, Color::rgba(56, 189, 248));
    }

    const float font_sz = 13.5f;
    const float tw = r.measure_text(label, font_sz);
    r.draw_text(x + (w - tw) * 0.5f,
                y + (h - font_sz * 1.15f) * 0.5f,
                label,
                font_sz,
                active ? Color::rgba(255, 255, 255) : Color::rgba(148, 163, 184));
    return clicked;
}

bool UiContext::slider_float(
    Renderer2D& r, int id, float x, float y, float w,
    std::string_view label, float& value, float min_v, float max_v) {
    char val_buf[32];
    if (max_v <= 1.05f) {
        std::snprintf(val_buf, sizeof(val_buf), "%d%%", static_cast<int>( std::round(value * 100.0f) ));
    } else {
        std::snprintf(val_buf, sizeof(val_buf), "$%.0f", static_cast<double>(value));
    }

    r.draw_text(x, y, label, 13.0f, Color::rgba(203, 213, 225));
    const float vw = r.measure_text(val_buf, 13.0f);
    r.draw_text(x + w - vw, y, val_buf, 13.0f, Color::rgba(56, 189, 248));

    const float track_y = y + 22.0f;
    const float track_h = 8.0f;
    const bool hit_zone =
        pointer.x >= (x - 6.0f) && pointer.x <= (x + w + 6.0f) &&
        pointer.y >= (track_y - 10.0f) && pointer.y <= (track_y + track_h + 10.0f);

    if (pointer.just_pressed && hit_zone) {
        active_slider_id_ = id;
    }

    bool changed = false;
    if (active_slider_id_ == id && pointer.down) {
        const float t = core::clamp_val((pointer.x - x) / std::max(1.0f, w), 0.0f, 1.0f);
        value = min_v + (max_v - min_v) * t;
        changed = true;
    }

    const float ratio = core::clamp_val((value - min_v) / std::max(0.0001f, max_v - min_v), 0.0f, 1.0f);
    r.draw_rounded_rect(x, track_y, w, track_h, 4.0f, Color::rgba(30, 41, 59));
    if (ratio > 0.01f) {
        r.draw_rounded_rect(x, track_y, w * ratio, track_h, 4.0f, Color::rgba(56, 189, 248));
    }
    r.draw_circle(x + w * ratio, track_y + track_h * 0.5f, 7.5f, Color::rgba(255, 255, 255), 12);
    return changed;
}

} // namespace simul::ui
