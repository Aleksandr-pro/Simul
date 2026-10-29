// ============================================================================
// Нативная точка входа игры «ВИРАЛ» (sokol_main -> WinMain .exe / ANativeActivity .apk)
// С точным расчётом физических dp-пикселей экрана смартфона, safe-area под камеру
// и автосохранением прогресса при сворачивании приложения!
// ============================================================================

#include "viral_app.hpp"
#include "sokol_app.h"
#include "sokol_audio.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"

#if defined(__ANDROID__)
#include <android/configuration.h>
#include <android/native_activity.h>
#endif

#include <algorithm>
#include <memory>

namespace {

std::unique_ptr<simul::game::ViralGameApp> g_app;
std::uint64_t g_last_time = 0;
#if defined(__ANDROID__)
float g_android_density = 0.0f;
#endif

float query_effective_ui_scale() {
#if defined(__ANDROID__)
    const float fb_w = sapp_widthf() > 1.0f ? sapp_widthf() : 1080.0f;
    const float fb_h = sapp_heightf() > 1.0f ? sapp_heightf() : 2400.0f;

    if (g_android_density <= 0.1f) {
        const auto* act = static_cast<const ANativeActivity*>(sapp_android_get_native_activity());
        if (act && act->assetManager) {
            AConfiguration* cfg = AConfiguration_new();
            AConfiguration_fromAssetManager(cfg, act->assetManager);
            const int32_t dpi = AConfiguration_getDensity(cfg);
            AConfiguration_delete(cfg);
            if (dpi > 80 && dpi < 1200) {
                g_android_density = static_cast<float>(dpi) / 160.0f;
            }
        }
        if (g_android_density <= 0.1f) {
            g_android_density = std::min(fb_w, fb_h) / 390.0f;
        }
    }

    // В портретном режиме смартфона гарантируем комфортную мобильную сетку 360..425 dp!
    if (fb_h >= fb_w) {
        return std::clamp(g_android_density, fb_w / 425.0f, fb_w / 360.0f);
    }
    return std::max(1.0f, g_android_density);
#else
    return sapp_dpi_scale() > 0.1f ? sapp_dpi_scale() : 1.0f;
#endif
}

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

#if defined(__ANDROID__)
    const auto* act = static_cast<const ANativeActivity*>(sapp_android_get_native_activity());
    if (act && act->internalDataPath) {
        g_app->set_storage_directory(act->internalDataPath);
    }
    // Безопасные отступы под вырез селфи-камеры сверху и жестовую полосу Android снизу
    g_app->set_safe_area(38.0f, 18.0f, 0.0f, 0.0f);
#endif

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
    const double dt_sec = std::min(0.1, stm_sec(stm_laptime(&g_last_time)));
    const float ui_scale = query_effective_ui_scale();
    const float logical_w = sapp_widthf() / ui_scale;
    const float logical_h = sapp_heightf() / ui_scale;

#if defined(__ANDROID__)
    if (g_app) {
        // Адаптируем safe-area при повороте или складных экранах
        if (logical_h > logical_w * 1.25f) {
            g_app->set_safe_area(38.0f, 18.0f, 0.0f, 0.0f);
        } else {
            g_app->set_safe_area(12.0f, 12.0f, 24.0f, 24.0f);
        }
    }
#endif

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
    const float ui_scale = query_effective_ui_scale();

    switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
                g_app->on_pointer_down(ev->mouse_x / ui_scale, ev->mouse_y / ui_scale, false);
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            g_app->on_pointer_move(ev->mouse_x / ui_scale, ev->mouse_y / ui_scale, false);
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
                g_app->on_pointer_up(ev->mouse_x / ui_scale, ev->mouse_y / ui_scale, false);
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            g_app->on_wheel(ev->scroll_y);
            break;
        case SAPP_EVENTTYPE_TOUCHES_BEGAN:
            if (ev->num_touches > 0) {
                g_app->on_pointer_down(ev->touches[0].pos_x / ui_scale, ev->touches[0].pos_y / ui_scale, true);
            }
            break;
        case SAPP_EVENTTYPE_TOUCHES_MOVED:
            if (ev->num_touches > 0) {
                g_app->on_pointer_move(ev->touches[0].pos_x / ui_scale, ev->touches[0].pos_y / ui_scale, true);
            }
            break;
        case SAPP_EVENTTYPE_TOUCHES_ENDED:
        case SAPP_EVENTTYPE_TOUCHES_CANCELLED:
            if (ev->num_touches > 0) {
                g_app->on_pointer_up(ev->touches[0].pos_x / ui_scale, ev->touches[0].pos_y / ui_scale, true);
            }
            break;
        case SAPP_EVENTTYPE_SUSPENDED:
        case SAPP_EVENTTYPE_QUIT_REQUESTED:
            g_app->save_autosave();
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
    desc.width = 430;
    desc.height = 880;
    desc.high_dpi = true;
    desc.sample_count = 4;
    desc.window_title = "ВИРАЛ — Симулятор блогера (Simul Native C++20)";
    desc.logger.func = slog_func;
    return desc;
}
