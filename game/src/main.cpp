// ============================================================================
// Нативная точка входа игры «ВИРАЛ» (sokol_main -> WinMain .exe / ANativeActivity .apk)
// ============================================================================

#include "viral_app.hpp"
#include "sokol_app.h"
#include "sokol_audio.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"
#include <memory>

namespace {

std::unique_ptr<simul::game::ViralGameApp> g_app;
std::uint64_t g_last_time = 0;

void audio_stream_cb(float* buffer, int num_frames, int num_channels, void* user_data) {
    auto* app = static_cast<simul::game::ViralGameApp*>(user_data);
    if (app) {
        app->sound().render_frames(buffer, num_frames, num_channels);
    }
}

void app_init_cb() {
    stm_setup();

    sg_desc gfx_desc{};
    gfx_desc.environment = sglue_environment();
    gfx_desc.logger.func = slog_func;
    sg_setup(&gfx_desc);

    g_app = std::make_unique<simul::game::ViralGameApp>();
    g_app->init();

    saudio_desc aud_desc{};
    aud_desc.sample_rate = 44100;
    aud_desc.num_channels = 2;
    aud_desc.stream_userdata_cb = audio_stream_cb;
    aud_desc.user_data = g_app.get();
    aud_desc.logger.func = slog_func;
    saudio_setup(&aud_desc);

    g_last_time = stm_now();
}

void app_frame_cb() {
    const double dt_sec = stm_sec(stm_laptime(&g_last_time));
    const float dpi = sapp_dpi_scale() > 0.1f ? sapp_dpi_scale() : 1.0f;
    const float logical_w = sapp_widthf() / dpi;
    const float logical_h = sapp_heightf() / dpi;

    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.03f, 0.05f, 0.11f, 1.0f};
    pass.swapchain = sglue_swapchain();

    sg_begin_pass(&pass);
    if (g_app) {
        g_app->update_and_render(static_cast<float>(dt_sec), logical_w, logical_h);
    }
    sg_end_pass();
    sg_commit();
}

void app_event_cb(const sapp_event* ev) {
    if (!g_app || !ev) return;
    const float dpi = sapp_dpi_scale() > 0.1f ? sapp_dpi_scale() : 1.0f;

    switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
                g_app->on_pointer_down(ev->mouse_x / dpi, ev->mouse_y / dpi);
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            g_app->on_pointer_move(ev->mouse_x / dpi, ev->mouse_y / dpi);
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
                g_app->on_pointer_up(ev->mouse_x / dpi, ev->mouse_y / dpi);
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            g_app->on_wheel(ev->scroll_y);
            break;
        case SAPP_EVENTTYPE_TOUCHES_BEGAN:
            if (ev->num_touches > 0) {
                g_app->on_pointer_down(ev->touches[0].pos_x / dpi, ev->touches[0].pos_y / dpi);
            }
            break;
        case SAPP_EVENTTYPE_TOUCHES_MOVED:
            if (ev->num_touches > 0) {
                g_app->on_pointer_move(ev->touches[0].pos_x / dpi, ev->touches[0].pos_y / dpi);
            }
            break;
        case SAPP_EVENTTYPE_TOUCHES_ENDED:
        case SAPP_EVENTTYPE_TOUCHES_CANCELLED:
            if (ev->num_touches > 0) {
                g_app->on_pointer_up(ev->touches[0].pos_x / dpi, ev->touches[0].pos_y / dpi);
            }
            break;
        default:
            break;
    }
}

void app_cleanup_cb() {
    saudio_shutdown();
    if (g_app) {
        g_app->shutdown();
        g_app.reset();
    }
    sg_shutdown();
}

} // namespace

sapp_desc sokol_main(int /*argc*/, char* /*argv*/[]) {
    sapp_desc desc{};
    desc.init_cb = app_init_cb;
    desc.frame_cb = app_frame_cb;
    desc.cleanup_cb = app_cleanup_cb;
    desc.event_cb = app_event_cb;
    desc.width = 480;
    desc.height = 880;
    desc.high_dpi = true;
    desc.sample_count = 4;
    desc.window_title = "ВИРАЛ — Симулятор блогера (Simul Native C++20)";
    desc.logger.func = slog_func;
    return desc;
}
