// Единственная единица трансляции с реализациями sokol.
// Выбор графического бэкенда: Windows — D3D11, Android — GLES3, Apple — Metal, остальное — OpenGL core.
#if defined(_WIN32)
    #define SOKOL_D3D11
#elif defined(__ANDROID__)
    #define SOKOL_GLES3
#elif defined(__APPLE__)
    #define SOKOL_METAL
#else
    #define SOKOL_GLCORE
#endif

#define SOKOL_APP_IMPL
#define SOKOL_GFX_IMPL
#define SOKOL_GLUE_IMPL
#define SOKOL_TIME_IMPL
#define SOKOL_LOG_IMPL

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"
