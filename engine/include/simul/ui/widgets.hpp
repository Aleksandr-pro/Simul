#pragma once

#include "simul/ui/renderer_2d.hpp"
#include <string_view>

namespace simul::ui {

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
    float wheel_dy = 0.0f;
};

/**
 * Тактильный нативный UI-контекст с поддержкой пальца (Android) и мыши (Windows/Linux/macOS),
 * инерционным скроллом, слайдерами и кнопками без WebView.
 */
class UiContext {
public:
    PointerState pointer;
    float scroll_y = 0.0f;
    float scroll_vel_y = 0.0f;

    void begin_input_frame(float dt_sec, float max_scroll_y);
    void end_input_frame() noexcept;

    bool button(Renderer2D& r, float x, float y, float w, float h,
                std::string_view label, bool primary = false, bool disabled = false);

    bool tab_button(Renderer2D& r, float x, float y, float w, float h,
                    std::string_view label, bool active);

    bool slider_float(Renderer2D& r, int id, float x, float y, float w,
                      std::string_view label, float& value, float min_v, float max_v);

private:
    int active_slider_id_ = -1;
};

} // namespace simul::ui
