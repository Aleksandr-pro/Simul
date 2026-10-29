// ============================================================================
// Единая точка компиляции нативных системных бэкендов Sokol и stb_truetype
// ============================================================================

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#define SOKOL_IMPL

#if defined(SIMUL_HEADLESS_GFX)
    #define SOKOL_DUMMY_BACKEND
    #define SOKOL_NO_ENTRY
#elif defined(_WIN32)
    #define SOKOL_D3D11
#elif defined(__ANDROID__)
    #define SOKOL_GLES3
#elif defined(__APPLE__)
    #define SOKOL_METAL
#else
    #define SOKOL_GLCORE
#endif

#if !defined(SIMUL_HEADLESS_GFX)
    #include "sokol_app.h"
#endif

#include "sokol_gfx.h"
#include "sokol_audio.h"
#include "sokol_time.h"
#include "sokol_log.h"

#if !defined(SIMUL_HEADLESS_GFX)
    #include "sokol_glue.h"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
