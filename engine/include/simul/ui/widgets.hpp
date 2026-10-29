#pragma once

#include "simul/ui/renderer_2d.hpp"
#include <string_view>

namespace simul::ui {

struct SafeAreaInsets {
    float top = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;
    float right = 0.0f;
};

struct PointerState {
    float x = 0.0f;
    float y = 0.0f;
    float press_start_x = 0.0f;
    float press_start_y = 0.0f;
    float drag_dy = 0.0f;
    bool down = false;
    bool just_pressed = false;
    bool just_released = false;
    bool is_scrolling = false;
    bool is_touch = false;
    float wheel_dy = 0.0f;
};

/**
 * Мобильно-десктопный нативный UI-контекст:
 * - защита от сквозных нажатий сквозь липкую шапку и нижний таб-бар,
 * - разделение горизонтального жеста слайдера и вертикального инерционного скролла,
 * - крупные touch-хитбоксы (>= 44-52 dp) под большой палец смартфона.
 */
class UiContext {
public:
    PointerState pointer;
    SafeAreaInsets safe_area;
    float scroll_y = 0.0f;
    float scroll_vel_y = 0.0f;

    void begin_input_frame(float dt_sec, float max_scroll_y, float viewport_top_y, float viewport_bottom_y);
    void end_input_frame() noexcept;

    void begin_scroll_region(float top_y, float bottom_y) noexcept;
    void end_scroll_region() noexcept;

    bool hit_rect(float x, float y, float w, float h) const noexcept;

    bool button(Renderer2D& r, float x, float y, float w, float h,
                std::string_view label, bool primary = false, bool disabled = false);

    bool chip_button(Renderer2D& r, float x, float y, float w, float h,
                     std::string_view label, bool active, Color accent = Color::rgba(56, 189, 248));

    bool card_option(Renderer2D& r, float x, float y, float w, float h,
                     std::string_view title, std::string_view subtitle,
                     bool selected, Color accent = Color::rgba(56, 189, 248), bool disabled = false);

    bool nav_tab_item(Renderer2D& r, int icon_id, float x, float y, float w, float h,
                      std::string_view label, bool active, bool has_badge = false,
                      Color badge_col = Color::rgba(244, 63, 94));

    bool slider_float(Renderer2D& r, int id, float x, float y, float w,
                      std::string_view label, float& value, float min_v, float max_v);

private:
    void draw_nav_icon(Renderer2D& r, int icon_id, float cx, float cy, Color col) const;

    int active_slider_id_ = -1;
    bool in_scroll_region_ = false;
    float scroll_top_y_ = 0.0f;
    float scroll_bottom_y_ = 10000.0f;
};

} // namespace simul::ui
