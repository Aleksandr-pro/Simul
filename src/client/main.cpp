// РАЗЛОМ — точка входа клиента (sokol_app: Windows D3D11, Linux/macOS GL/Metal, Android GLES3).
#include <cstdio>
#include <string>

#include "razlom/client/app.hpp"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"

namespace {
razlom::client::App g_app;
std::string g_startAddress;
bool g_started = false;

void onInit() {
    sg_desc desc{};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    desc.buffer_pool_size = 4096;
    desc.image_pool_size = 64;
    desc.shader_pool_size = 32;
    desc.pipeline_pool_size = 32;
    desc.sampler_pool_size = 16;
    desc.view_pool_size = 64;
    desc.uniform_buffer_size = 512 * 1024;
    sg_setup(&desc);

    if (!g_app.init()) {
        std::printf("[разлом] не удалось запустить приложение\n");
        return;
    }
    g_started = true;
    if (!g_startAddress.empty()) g_app.openAddress(g_startAddress);
}

void onFrame() {
    if (!g_started) return;
    g_app.frame();
}

void onCleanup() {
    if (g_started) g_app.cleanup();
    sg_shutdown();
}

void onEvent(const sapp_event* event) {
    if (g_started) g_app.onEvent(event);
}

}  // namespace

sapp_desc sokol_main(int argc, char* argv[]) {
#ifndef __ANDROID__
    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') g_startAddress = argv[1];
#endif
    stm_setup();

    sapp_desc desc{};
    desc.init_cb = onInit;
    desc.frame_cb = onFrame;
    desc.cleanup_cb = onCleanup;
    desc.event_cb = onEvent;
    desc.width = 1280;
    desc.height = 760;
    desc.high_dpi = true;
    desc.sample_count = 1;
    desc.swap_interval = 1;
    desc.enable_clipboard = true;
    desc.clipboard_size = 1024;
    desc.window_title = "РАЗЛОМ — песочница нового поколения";
    return desc;
}
