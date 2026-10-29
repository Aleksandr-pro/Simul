#pragma once

#include "simul/core/types.hpp"
#include "simul/ui/utf8_atlas.hpp"
#include <cstdint>
#include <string_view>
#include <vector>

namespace simul::ui {

struct Color {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;

    constexpr std::uint32_t pack_abgr() const noexcept {
        return static_cast<std::uint32_t>(r) |
               (static_cast<std::uint32_t>(g) << 8) |
               (static_cast<std::uint32_t>(b) << 16) |
               (static_cast<std::uint32_t>(a) << 24);
    }

    static constexpr Color rgba(std::uint8_t r_, std::uint8_t g_, std::uint8_t b_, std::uint8_t a_ = 255) noexcept {
        return {r_, g_, b_, a_};
    }

    constexpr Color with_alpha(std::uint8_t new_a) const noexcept {
        return {r, g, b, new_a};
    }
};

struct Vertex2D {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    std::uint32_t abgr = 0xFFFFFFFFu;
};

struct Particle2D {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float life = 0.0f;
    float max_life = 0.8f;
    float radius = 3.5f;
    Color color{255, 200, 60, 255};
};

/**
 * Нативный батч-рендерер GPU (Direct3D 11 / OpenGL ES 3.0 / Metal / OpenGL 3.3).
 * Отрисовывает весь игровой интерфейс, иллюстрацию студии, превью, кривые волн и кириллицу за 1 draw call!
 */
class Renderer2D {
public:
    static constexpr std::size_t kMaxVertices = 98'304;

    bool init();
    void shutdown();

    void begin_frame(float logical_width, float logical_height);

    void draw_rect(float x, float y, float w, float h, Color color);
    void draw_gradient_v(float x, float y, float w, float h, Color top, Color bottom);
    void draw_rounded_rect(float x, float y, float w, float h, float radius, Color color);
    void draw_rect_outline(float x, float y, float w, float h, float thickness, Color color);
    void draw_line(float x0, float y0, float x1, float y1, float thickness, Color color);
    void draw_circle(float cx, float cy, float radius, Color color, int segments = 16);
    void draw_triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color color);

    void draw_text(float x, float y, std::string_view utf8_text, float font_size_px, Color color);
    void draw_text_fit(float x, float y, float max_width, std::string_view utf8_text, float max_font_px, Color color);
    void draw_text_wrapped(
        float x, float y, float max_width, std::string_view utf8_text, float font_size_px, Color color, float* out_height = nullptr);
    float measure_text(std::string_view utf8_text, float font_size_px) const noexcept;
    float measure_text_wrapped(float max_width, std::string_view utf8_text, float font_size_px) const noexcept;

    /** Рисует живой график 48-часовых волн алгоритма или гистограмму удержания */
    void draw_wave_telemetry(
        float x, float y, float w, float h,
        const std::vector<core::VideoWavePoint>& timeline,
        const std::array<float, 10>& retention_curve,
        bool show_retention);

    void spawn_wave_burst(float cx, float cy, std::uint8_t wave_index);
    void step_and_draw_particles(float dt_sec);

    /** Отправляет накопленный вертексный батч на GPU (1 вызов sg_draw) */
    void flush_gpu_batch();

    std::size_t vertex_count() const noexcept { return vertices_.size(); }

private:
    void push_tri(const Vertex2D& a, const Vertex2D& b, const Vertex2D& c);
    void push_quad(float x0, float y0, float x1, float y1,
                   float u0, float v0, float u1, float v1,
                   std::uint32_t c_top, std::uint32_t c_bot);

    Utf8FontAtlas atlas_;
    std::vector<Vertex2D> vertices_;
    std::vector<Particle2D> particles_;
    float screen_w_ = 390.0f;
    float screen_h_ = 844.0f;

    std::uint32_t gpu_vbuf_id_ = 0;
    std::uint32_t gpu_img_id_ = 0;
    std::uint32_t gpu_view_id_ = 0;
    std::uint32_t gpu_smp_id_ = 0;
    std::uint32_t gpu_pip_id_ = 0;
    std::uint32_t gpu_shd_id_ = 0;
    bool initialized_ = false;
};

} // namespace simul::ui
