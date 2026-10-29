#include "simul/ui/renderer_2d.hpp"
#include "sokol_gfx.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace simul::ui {

namespace {

#if defined(_WIN32) && !defined(SIMUL_HEADLESS_GFX)
constexpr const char* kVsSource = R"(
cbuffer params : register(b0) {
    float4 u_screen;
};
struct vs_in {
    float2 pos : TEXCOORD0;
    float2 uv  : TEXCOORD1;
    float4 col : TEXCOORD2;
};
struct vs_out {
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
    float4 pos : SV_Position;
};
vs_out main(vs_in inp) {
    vs_out outp;
    float2 ndc = (inp.pos / u_screen.xy) * 2.0 - 1.0;
    outp.pos = float4(ndc.x, -ndc.y, 0.0, 1.0);
    outp.uv = inp.uv;
    outp.col = inp.col;
    return outp;
}
)";

constexpr const char* kFsSource = R"(
Texture2D<float4> u_tex : register(t0);
SamplerState u_smp : register(s0);
struct vs_out {
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};
float4 main(vs_out inp) : SV_Target0 {
    return u_tex.Sample(u_smp, inp.uv) * inp.col;
}
)";
#elif defined(__ANDROID__) && !defined(SIMUL_HEADLESS_GFX)
constexpr const char* kVsSource =
    "#version 300 es\n"
    "uniform vec4 u_screen;\n"
    "layout(location=0) in vec2 pos;\n"
    "layout(location=1) in vec2 uv;\n"
    "layout(location=2) in vec4 col;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_col;\n"
    "void main() {\n"
    "  vec2 ndc = (pos / u_screen.xy) * 2.0 - 1.0;\n"
    "  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "  v_uv = uv;\n"
    "  v_col = col;\n"
    "}\n";

constexpr const char* kFsSource =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "in vec2 v_uv;\n"
    "in vec4 v_col;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "  frag_color = texture(u_tex, v_uv) * v_col;\n"
    "}\n";
#elif defined(__APPLE__) && !defined(SIMUL_HEADLESS_GFX)
constexpr const char* kVsSource = R"(
#include <metal_stdlib>
using namespace metal;
struct params_t { float4 u_screen; };
struct vs_in {
    float2 pos [[attribute(0)]];
    float2 uv  [[attribute(1)]];
    float4 col [[attribute(2)]];
};
struct vs_out {
    float4 pos [[position]];
    float2 uv;
    float4 col;
};
vertex vs_out _main(vs_in in [[stage_in]], constant params_t& params [[buffer(0)]]) {
    vs_out out;
    float2 ndc = (in.pos / params.u_screen.xy) * 2.0 - 1.0;
    out.pos = float4(ndc.x, -ndc.y, 0.0, 1.0);
    out.uv = in.uv;
    out.col = in.col;
    return out;
}
)";

constexpr const char* kFsSource = R"(
#include <metal_stdlib>
using namespace metal;
struct vs_out {
    float4 pos [[position]];
    float2 uv;
    float4 col;
};
fragment float4 _main(vs_out in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    return tex.sample(smp, in.uv) * in.col;
}
)";
#else
constexpr const char* kVsSource =
    "#version 330\n"
    "uniform vec4 u_screen;\n"
    "layout(location=0) in vec2 pos;\n"
    "layout(location=1) in vec2 uv;\n"
    "layout(location=2) in vec4 col;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_col;\n"
    "void main() {\n"
    "  vec2 ndc = (pos / u_screen.xy) * 2.0 - 1.0;\n"
    "  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "  v_uv = uv;\n"
    "  v_col = col;\n"
    "}\n";

constexpr const char* kFsSource =
    "#version 330\n"
    "uniform sampler2D u_tex;\n"
    "in vec2 v_uv;\n"
    "in vec4 v_col;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "  frag_color = texture(u_tex, v_uv) * v_col;\n"
    "}\n";
#endif

std::string compact_num(std::int64_t val) {
    char buf[32];
    const double abs_v = std::fabs(static_cast<double>(val));
    if (abs_v >= 1'000'000.0) {
        std::snprintf(buf, sizeof(buf), "%.2fM", static_cast<double>(val) / 1'000'000.0);
    } else if (abs_v >= 1'000.0) {
        std::snprintf(buf, sizeof(buf), "%.1fK", static_cast<double>(val) / 1'000'000.0 * 1000.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(val));
    }
    return buf;
}

Color wave_palette(std::uint8_t wave) noexcept {
    switch (wave) {
        case 0: return Color::rgba(148, 163, 184);
        case 1: return Color::rgba(56, 189, 248);
        case 2: return Color::rgba(129, 140, 248);
        case 3: return Color::rgba(250, 204, 21);
        default: return Color::rgba(244, 63, 94);
    }
}

} // namespace

bool Renderer2D::init() {
    if (!atlas_.build_from_embedded_ttf(56.0f)) {
        return false;
    }
    vertices_.reserve(kMaxVertices);

    sg_image_desc img_desc{};
    img_desc.width = Utf8FontAtlas::kAtlasWidth;
    img_desc.height = Utf8FontAtlas::kAtlasHeight;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.data.ptr = atlas_.rgba_pixels().data();
    img_desc.data.size = atlas_.rgba_pixels().size();
    img_desc.label = "simul_font_atlas_hd";
    const sg_image img = sg_make_image(&img_desc);
    gpu_img_id_ = img.id;

    sg_view_desc view_desc{};
    view_desc.texture.image = img;
    view_desc.label = "simul_font_atlas_view";
    gpu_view_id_ = sg_make_view(&view_desc).id;

    sg_sampler_desc smp_desc{};
    smp_desc.min_filter = SG_FILTER_LINEAR;
    smp_desc.mag_filter = SG_FILTER_LINEAR;
    smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.label = "simul_linear_sampler";
    gpu_smp_id_ = sg_make_sampler(&smp_desc).id;

    sg_buffer_desc vbuf_desc{};
    vbuf_desc.size = kMaxVertices * sizeof(Vertex2D);
    vbuf_desc.usage.vertex_buffer = true;
    vbuf_desc.usage.write_transient = true;
    vbuf_desc.label = "simul_ui_vbuf";
    gpu_vbuf_id_ = sg_make_buffer(&vbuf_desc).id;

    sg_shader_desc shd_desc{};
    shd_desc.vertex_func.source = kVsSource;
    shd_desc.fragment_func.source = kFsSource;
#if defined(_WIN32) && !defined(SIMUL_HEADLESS_GFX)
    shd_desc.vertex_func.entry = "main";
    shd_desc.fragment_func.entry = "main";
#endif
    shd_desc.attrs[0].glsl_name = "pos";
    shd_desc.attrs[0].hlsl_sem_name = "TEXCOORD";
    shd_desc.attrs[0].hlsl_sem_index = 0;

    shd_desc.attrs[1].glsl_name = "uv";
    shd_desc.attrs[1].hlsl_sem_name = "TEXCOORD";
    shd_desc.attrs[1].hlsl_sem_index = 1;

    shd_desc.attrs[2].glsl_name = "col";
    shd_desc.attrs[2].hlsl_sem_name = "TEXCOORD";
    shd_desc.attrs[2].hlsl_sem_index = 2;

    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = 16;
    shd_desc.uniform_blocks[0].hlsl_register_b_n = 0;
    shd_desc.uniform_blocks[0].msl_buffer_n = 0;
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "u_screen";

    shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd_desc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shd_desc.views[0].texture.hlsl_register_t_n = 0;
    shd_desc.views[0].texture.msl_texture_n = 0;

    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.samplers[0].hlsl_register_s_n = 0;
    shd_desc.samplers[0].msl_sampler_n = 0;

    shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[0].view_slot = 0;
    shd_desc.texture_sampler_pairs[0].sampler_slot = 0;
    shd_desc.texture_sampler_pairs[0].glsl_name = "u_tex";
    shd_desc.label = "simul_ui_shader";

    const sg_shader shd = sg_make_shader(&shd_desc);
    gpu_shd_id_ = shd.id;

    sg_pipeline_desc pip_desc{};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.layout.attrs[2].format = SG_VERTEXFORMAT_UBYTE4N;
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.label = "simul_ui_pipeline";
    gpu_pip_id_ = sg_make_pipeline(&pip_desc).id;

    initialized_ = true;
    return true;
}

void Renderer2D::shutdown() {
    if (!initialized_) return;
    sg_destroy_pipeline({gpu_pip_id_});
    sg_destroy_shader({gpu_shd_id_});
    sg_destroy_buffer({gpu_vbuf_id_});
    sg_destroy_sampler({gpu_smp_id_});
    sg_destroy_view({gpu_view_id_});
    sg_destroy_image({gpu_img_id_});
    initialized_ = false;
}

void Renderer2D::begin_frame(float logical_width, float logical_height) {
    screen_w_ = std::max(1.0f, logical_width);
    screen_h_ = std::max(1.0f, logical_height);
    vertices_.clear();
}

void Renderer2D::push_tri(const Vertex2D& a, const Vertex2D& b, const Vertex2D& c) {
    if (vertices_.size() + 3 > kMaxVertices) return;
    vertices_.push_back(a);
    vertices_.push_back(b);
    vertices_.push_back(c);
}

void Renderer2D::push_quad(
    float x0, float y0, float x1, float y1,
    float u0, float v0, float u1, float v1,
    std::uint32_t c_top, std::uint32_t c_bot) {
    if (vertices_.size() + 6 > kMaxVertices) return;
    const Vertex2D v00{x0, y0, u0, v0, c_top};
    const Vertex2D v10{x1, y0, u1, v0, c_top};
    const Vertex2D v11{x1, y1, u1, v1, c_bot};
    const Vertex2D v01{x0, y1, u0, v1, c_bot};
    vertices_.push_back(v00);
    vertices_.push_back(v10);
    vertices_.push_back(v11);
    vertices_.push_back(v00);
    vertices_.push_back(v11);
    vertices_.push_back(v01);
}

void Renderer2D::draw_rect(float x, float y, float w, float h, Color color) {
    if (w <= 0.0f || h <= 0.0f) return;
    const float wu = atlas_.white_u();
    const float wv = atlas_.white_v();
    const std::uint32_t c = color.pack_abgr();
    push_quad(x, y, x + w, y + h, wu, wv, wu, wv, c, c);
}

void Renderer2D::draw_gradient_v(float x, float y, float w, float h, Color top, Color bottom) {
    if (w <= 0.0f || h <= 0.0f) return;
    const float wu = atlas_.white_u();
    const float wv = atlas_.white_v();
    push_quad(x, y, x + w, y + h, wu, wv, wu, wv, top.pack_abgr(), bottom.pack_abgr());
}

void Renderer2D::draw_rounded_rect(float x, float y, float w, float h, float radius, Color color) {
    if (w <= 0.0f || h <= 0.0f) return;
    const float r = std::min({ std::max(0.0f, radius), w * 0.5f, h * 0.5f });
    if (r < 1.5f) {
        draw_rect(x, y, w, h, color);
        return;
    }

    draw_rect(x + r, y, w - 2.0f * r, h, color);
    draw_rect(x, y + r, r, h - 2.0f * r, color);
    draw_rect(x + w - r, y + r, r, h - 2.0f * r, color);

    const float wu = atlas_.white_u();
    const float wv = atlas_.white_v();
    const std::uint32_t col = color.pack_abgr();
    constexpr int kCornerSegs = 6;
    constexpr float kHalfPi = 1.57079632679f;

    const float corners[4][3] = {
        {x + r, y + r, 3.14159265f},
        {x + w - r, y + r, 4.71238898f},
        {x + w - r, y + h - r, 0.0f},
        {x + r, y + h - r, 1.57079633f},
    };

    for (const auto& cr : corners) {
        const Vertex2D center{cr[0], cr[1], wu, wv, col};
        for (int i = 0; i < kCornerSegs; ++i) {
            const float a0 = cr[2] + (static_cast<float>(i) / kCornerSegs) * kHalfPi;
            const float a1 = cr[2] + (static_cast<float>(i + 1) / kCornerSegs) * kHalfPi;
            const Vertex2D p0{cr[0] + std::cos(a0) * r, cr[1] + std::sin(a0) * r, wu, wv, col};
            const Vertex2D p1{cr[0] + std::cos(a1) * r, cr[1] + std::sin(a1) * r, wu, wv, col};
            push_tri(center, p0, p1);
        }
    }
}

void Renderer2D::draw_rect_outline(float x, float y, float w, float h, float thickness, Color color) {
    draw_rect(x, y, w, thickness, color);
    draw_rect(x, y + h - thickness, w, thickness, color);
    draw_rect(x, y + thickness, thickness, h - 2.0f * thickness, color);
    draw_rect(x + w - thickness, y + thickness, thickness, h - 2.0f * thickness, color);
}

void Renderer2D::draw_line(float x0, float y0, float x1, float y1, float thickness, Color color) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    const float nx = (-dy / len) * (thickness * 0.5f);
    const float ny = (dx / len) * (thickness * 0.5f);
    const float wu = atlas_.white_u();
    const float wv = atlas_.white_v();
    const std::uint32_t c = color.pack_abgr();

    const Vertex2D a{x0 + nx, y0 + ny, wu, wv, c};
    const Vertex2D b{x1 + nx, y1 + ny, wu, wv, c};
    const Vertex2D d{x1 - nx, y1 - ny, wu, wv, c};
    const Vertex2D e{x0 - nx, y0 - ny, wu, wv, c};
    push_tri(a, b, d);
    push_tri(a, d, e);
}

void Renderer2D::draw_circle(float cx, float cy, float radius, Color color, int segments) {
    const float wu = atlas_.white_u();
    const float wv = atlas_.white_v();
    const std::uint32_t c = color.pack_abgr();
    const Vertex2D center{cx, cy, wu, wv, c};
    constexpr float kTwoPi = 6.28318530718f;

    for (int i = 0; i < segments; ++i) {
        const float a0 = (static_cast<float>(i) / static_cast<float>(segments)) * kTwoPi;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * kTwoPi;
        const Vertex2D p0{cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, wu, wv, c};
        const Vertex2D p1{cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, wu, wv, c};
        push_tri(center, p0, p1);
    }
}

void Renderer2D::draw_triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color color) {
    const float wu = atlas_.white_u();
    const float wv = atlas_.white_v();
    const std::uint32_t c = color.pack_abgr();
    push_tri({x0, y0, wu, wv, c}, {x1, y1, wu, wv, c}, {x2, y2, wu, wv, c});
}

void Renderer2D::draw_text(float x, float y, std::string_view utf8_text, float font_size_px, Color color) {
    const float ratio = font_size_px / atlas_.base_pixel_height();
    const float baseline_y = y + atlas_.ascent_px() * ratio;
    const std::uint32_t c = color.pack_abgr();

    float pen_x = x;
    std::size_t pos = 0;
    while (pos < utf8_text.size()) {
        const std::uint32_t cp = Utf8FontAtlas::next_utf8_codepoint(utf8_text, pos);
        if (cp == 0) break;
        const auto* g = atlas_.find_glyph(cp);
        if (!g) continue;

        if (g->width > 0.0f && g->height > 0.0f) {
            const float gx0 = pen_x + g->xoff * ratio;
            const float gy0 = baseline_y + g->yoff * ratio;
            const float gx1 = gx0 + g->width * ratio;
            const float gy1 = gy0 + g->height * ratio;
            push_quad(gx0, gy0, gx1, gy1, g->u0, g->v0, g->u1, g->v1, c, c);
        }
        pen_x += g->xadvance * ratio;
    }
}

void Renderer2D::draw_text_fit(float x, float y, float max_width, std::string_view utf8_text, float max_font_px, Color color) {
    float sz = max_font_px;
    const float tw = measure_text(utf8_text, sz);
    if (tw > max_width && max_width > 12.0f) {
        sz = std::max(10.0f, sz * (max_width / tw));
    }
    draw_text(x, y + (max_font_px - sz) * 0.5f, utf8_text, sz, color);
}

void Renderer2D::draw_text_wrapped(
    float x, float y, float max_width, std::string_view utf8_text,
    float font_size_px, Color color, float* out_height) {
    const float line_step = font_size_px * 1.32f;
    float cur_y = y;

    std::size_t i = 0;
    std::string current_line;
    while (i < utf8_text.size()) {
        while (i < utf8_text.size() && utf8_text[i] == ' ') ++i;
        if (i >= utf8_text.size()) break;
        const std::size_t word_start = i;
        while (i < utf8_text.size() && utf8_text[i] != ' ' && utf8_text[i] != '\n') ++i;
        const std::string_view word = utf8_text.substr(word_start, i - word_start);

        std::string candidate = current_line.empty() ? std::string(word) : (current_line + " " + std::string(word));
        if (!current_line.empty() && measure_text(candidate, font_size_px) > max_width) {
            draw_text(x, cur_y, current_line, font_size_px, color);
            cur_y += line_step;
            current_line = std::string(word);
        } else {
            current_line = std::move(candidate);
        }

        if (i < utf8_text.size() && utf8_text[i] == '\n') {
            draw_text(x, cur_y, current_line, font_size_px, color);
            cur_y += line_step;
            current_line.clear();
            ++i;
        }
    }

    if (!current_line.empty()) {
        draw_text(x, cur_y, current_line, font_size_px, color);
        cur_y += line_step;
    }
    if (out_height) *out_height = cur_y - y;
}

float Renderer2D::measure_text(std::string_view utf8_text, float font_size_px) const noexcept {
    return atlas_.measure_text_width(utf8_text, font_size_px);
}

float Renderer2D::measure_text_wrapped(float max_width, std::string_view utf8_text, float font_size_px) const noexcept {
    const float line_step = font_size_px * 1.32f;
    int lines = 0;

    std::size_t i = 0;
    std::string current_line;
    while (i < utf8_text.size()) {
        while (i < utf8_text.size() && utf8_text[i] == ' ') ++i;
        if (i >= utf8_text.size()) break;
        const std::size_t word_start = i;
        while (i < utf8_text.size() && utf8_text[i] != ' ' && utf8_text[i] != '\n') ++i;
        const std::string_view word = utf8_text.substr(word_start, i - word_start);

        std::string candidate = current_line.empty() ? std::string(word) : (current_line + " " + std::string(word));
        if (!current_line.empty() && measure_text(candidate, font_size_px) > max_width) {
            ++lines;
            current_line = std::string(word);
        } else {
            current_line = std::move(candidate);
        }

        if (i < utf8_text.size() && utf8_text[i] == '\n') {
            ++lines;
            current_line.clear();
            ++i;
        }
    }
    if (!current_line.empty()) ++lines;
    return static_cast<float>(std::max(1, lines)) * line_step;
}

void Renderer2D::draw_wave_telemetry(
    float x, float y, float w, float h,
    const std::vector<core::VideoWavePoint>& timeline,
    const std::array<float, 10>& retention_curve,
    bool show_retention) {
    draw_rounded_rect(x, y, w, h, 14.0f, Color::rgba(11, 17, 34));
    draw_rect_outline(x, y, w, h, 1.0f, Color::rgba(56, 189, 248, 45));

    const float pad_l = 44.0f;
    const float pad_r = 14.0f;
    const float pad_t = 32.0f;
    const float pad_b = 26.0f;
    const float pw = std::max(40.0f, w - pad_l - pad_r);
    const float ph = std::max(40.0f, h - pad_t - pad_b);
    const float px = x + pad_l;
    const float py = y + pad_t;

    for (int i = 0; i <= 4; ++i) {
        const float gy = py + (ph * static_cast<float>(i)) / 4.0f;
        draw_rect(px, gy, pw, 1.0f, Color::rgba(148, 163, 184, 28));
    }

    if (show_retention) {
        draw_text_fit(x + 12.0f, y + 8.0f, w - 24.0f, "Удержание аудитории по децилям (10% - 100%)", 13.5f, Color::rgba(226, 232, 240));
        const float gap = w < 420.0f ? 4.0f : 6.0f;
        const float bw = (pw - gap * 9.0f) / 10.0f;
        for (int i = 0; i < 10; ++i) {
            const float val = std::clamp(retention_curve[static_cast<std::size_t>(i)], 0.0f, 1.0f);
            const float bh = val * ph;
            const float bx = px + static_cast<float>(i) * (bw + gap);
            const Color bc = val >= 0.55f ? Color::rgba(34, 197, 94)
                                          : (val >= 0.35f ? Color::rgba(56, 189, 248) : Color::rgba(244, 63, 94));
            draw_rounded_rect(bx, py + ph - bh, bw, bh, 4.0f, bc);
            if (w >= 420.0f || (i % 2 == 0) || i == 9) {
                const std::string lbl = std::to_string((i + 1) * 10) + "%";
                draw_text(bx, py + ph + 5.0f, lbl, 10.5f, Color::rgba(148, 163, 184));
            }
        }
        return;
    }

    if (timeline.empty()) {
        draw_text_wrapped(x + 16.0f, py + ph * 0.35f, w - 32.0f,
                          "Запустите публикацию ролика во вкладке «Студия», чтобы в реальном времени отслеживать 48-часовые волны рекомендаций.",
                          13.5f, Color::rgba(148, 163, 184));
        return;
    }

    std::int64_t max_v = 50;
    for (const auto& pt : timeline) {
        if (pt.views > max_v) max_v = pt.views;
    }
    const float max_vf = static_cast<float>(max_v) * 1.15f;

    for (int i = 0; i <= 2; ++i) {
        const auto val = static_cast<std::int64_t>(max_vf * (1.0f - static_cast<float>(i) * 0.5f));
        draw_text(x + 6.0f, py + (ph * static_cast<float>(i)) * 0.5f - 6.0f,
                  compact_num(val), 11.0f, Color::rgba(148, 163, 184));
    }

    const auto& latest = timeline.back();
    const Color main_col = wave_palette(latest.wave_index);
    const Color fill_top{main_col.r, main_col.g, main_col.b, 92};
    const Color fill_bot{main_col.r, main_col.g, main_col.b, 8};
    const float wu = atlas_.white_u();
    const float wv = atlas_.white_v();

    for (std::size_t i = 1; i < timeline.size(); ++i) {
        const auto& p0 = timeline[i - 1];
        const auto& p1 = timeline[i];
        const float x0 = px + (static_cast<float>(p0.hour_after_publish - 1) / 47.0f) * pw;
        const float y0 = py + ph - (static_cast<float>(p0.views) / max_vf) * ph;
        const float x1 = px + (static_cast<float>(p1.hour_after_publish - 1) / 47.0f) * pw;
        const float y1 = py + ph - (static_cast<float>(p1.views) / max_vf) * ph;

        const Vertex2D t0{x0, y0, wu, wv, fill_top.pack_abgr()};
        const Vertex2D t1{x1, y1, wu, wv, fill_top.pack_abgr()};
        const Vertex2D b1{x1, py + ph, wu, wv, fill_bot.pack_abgr()};
        const Vertex2D b0{x0, py + ph, wu, wv, fill_bot.pack_abgr()};
        push_tri(t0, t1, b1);
        push_tri(t0, b1, b0);

        draw_line(x0, y0, x1, y1, 2.8f, main_col);
    }

    const float hx = px + (static_cast<float>(latest.hour_after_publish - 1) / 47.0f) * pw;
    const float hy = py + ph - (static_cast<float>(latest.views) / max_vf) * ph;
    draw_circle(hx, hy, 7.0f, main_col.with_alpha(90));
    draw_circle(hx, hy, 4.5f, main_col);
    draw_circle(hx, hy, 2.2f, Color::rgba(255, 255, 255));

    char hdr[128];
    std::snprintf(hdr, sizeof(hdr), "Волна %u • Viral %.2f • Обл. %c (%d/48ч)",
                  static_cast<unsigned>(latest.wave_index),
                  static_cast<double>(latest.viral_score),
                  latest.active_variant,
                  latest.hour_after_publish);
    draw_text_fit(x + 12.0f, y + 8.0f, w - 24.0f, hdr, 13.5f, main_col);
}

void Renderer2D::spawn_wave_burst(float cx, float cy, std::uint8_t wave_index) {
    const Color c = wave_palette(wave_index);
    const int count = wave_index >= 4 ? 36 : 22;
    constexpr float kTwoPi = 6.28318530718f;

    for (int i = 0; i < count; ++i) {
        const float a = (kTwoPi * static_cast<float>(i)) / static_cast<float>(count);
        const float spd = 65.0f + static_cast<float>((i * 41) % 105);
        Particle2D p{};
        p.x = cx;
        p.y = cy;
        p.vx = std::cos(a) * spd;
        p.vy = std::sin(a) * spd - 35.0f;
        p.max_life = 0.85f;
        p.radius = 3.6f;
        p.color = c;
        particles_.push_back(p);
    }
}

void Renderer2D::step_and_draw_particles(float dt_sec) {
    if (particles_.empty()) return;
    std::vector<Particle2D> alive;
    alive.reserve(particles_.size());
    for (auto& p : particles_) {
        p.life += dt_sec;
        if (p.life >= p.max_life) continue;
        p.vy += 190.0f * dt_sec;
        p.x += p.vx * dt_sec;
        p.y += p.vy * dt_sec;
        const float alpha = 1.0f - (p.life / p.max_life);
        Color col = p.color;
        col.a = static_cast<std::uint8_t>(alpha * 255.0f);
        draw_circle(p.x, p.y, p.radius, col, 8);
        alive.push_back(p);
    }
    particles_ = std::move(alive);
}

void Renderer2D::flush_gpu_batch() {
    if (!initialized_ || vertices_.empty()) return;

    sg_write_buffer_desc wb{};
    wb.src.data.ptr = vertices_.data();
    wb.src.data.size = vertices_.size() * sizeof(Vertex2D);
    wb.dst.buffer = {gpu_vbuf_id_};
    sg_write_buffer_transient(&wb);

    sg_apply_pipeline({gpu_pip_id_});

    sg_bindings bind{};
    bind.vertex_buffers[0] = {gpu_vbuf_id_};
    bind.views[0] = {gpu_view_id_};
    bind.samplers[0] = {gpu_smp_id_};
    sg_apply_bindings(&bind);

    const float vs_uniform[4] = {screen_w_, screen_h_, 0.0f, 0.0f};
    sg_range u_range{vs_uniform, sizeof(vs_uniform)};
    sg_apply_uniforms(0, &u_range);

    sg_draw(0, static_cast<int>(vertices_.size()), 1);
}

} // namespace simul::ui
