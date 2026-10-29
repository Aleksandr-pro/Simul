cmake_minimum_required(VERSION 3.20)
project(SimulNative VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Статический CRT на MSVC для автономного однофайлового .exe без внешних DLL
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8 /W3)
endif()

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

option(SIMUL_BUILD_GAME "Сборка графического нативного клиента игры ВИРАЛ (.exe / .so / bin)" ON)
option(SIMUL_BUILD_TESTS "Сборка автотестов и стресс-бенчмарка C++20" ON)

# 1. Чистое детерминированное ядро симуляции и облачной синхронизации
add_library(simul_core STATIC
    engine/src/core/audience_pool.cpp
    engine/src/core/recommender.cpp
    engine/src/core/minigames.cpp
    engine/src/core/narrative.cpp
    engine/src/core/binary_save.cpp
    engine/src/core/engine.cpp
    engine/src/cloud/world_sync.cpp
    engine/src/audio/synth.cpp
)

target_include_directories(simul_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/include
)

if(WIN32)
    target_link_libraries(simul_core PUBLIC winhttp)
endif()

# 2. Нативный графический клиент игры «ВИРАЛ» (Direct3D 11 .exe / Android GLES3 .apk / Desktop GL / Metal)
if(SIMUL_BUILD_GAME)
    set(SIMUL_UI_SOURCES
        engine/src/ui/sokol_impl.c
        engine/src/ui/utf8_atlas.cpp
        engine/src/ui/renderer_2d.cpp
        engine/src/ui/widgets.cpp
        game/src/studio_scene.cpp
        game/src/viral_app.cpp
        game/src/main.cpp
    )

    if(ANDROID)
        add_library(simul_viral SHARED ${SIMUL_UI_SOURCES})
        target_link_libraries(simul_viral PRIVATE
            simul_core
            android
            log
            EGL
            GLESv3
            aaudio
        )
        target_link_options(simul_viral PRIVATE
            "-Wl,-u,ANativeActivity_onCreate"
            "-Wl,-z,max-page-size=16384"
            "-Wl,--build-id=sha1"
        )
    elseif(WIN32)
        add_executable(ViralSimul WIN32 ${SIMUL_UI_SOURCES})
        target_link_libraries(ViralSimul PRIVATE
            simul_core
            d3d11
            dxgi
            d3dcompiler
            ole32
            gdi32
            user32
            kernel32
        )
        if(MINGW)
            target_link_options(ViralSimul PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    elseif(APPLE)
        add_executable(ViralSimul ${SIMUL_UI_SOURCES})
        set_source_files_properties(engine/src/ui/sokol_impl.c PROPERTIES COMPILE_FLAGS "-x objective-c")
        target_link_libraries(ViralSimul PRIVATE
            simul_core
            "-framework Cocoa"
            "-framework QuartzCore"
            "-framework Metal"
            "-framework MetalKit"
            "-framework AudioToolbox"
        )
    else()
        add_executable(ViralSimul ${SIMUL_UI_SOURCES})
        target_link_libraries(ViralSimul PRIVATE
            simul_core
            GL
            X11
            Xi
            Xcursor
            dl
            pthread
            asound
            m
        )
    endif()

    set(SIMUL_APP_TARGET ViralSimul)
    if(ANDROID)
        set(SIMUL_APP_TARGET simul_viral)
    endif()

    target_include_directories(${SIMUL_APP_TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sokol
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts
        ${CMAKE_CURRENT_SOURCE_DIR}/game/src
    )
endif()

# 3. Автотесты и бенчмарк 100 000 SoA-агентов (Headless GPU Dummy Backend)
if(SIMUL_BUILD_TESTS AND NOT ANDROID)
    enable_testing()

    add_executable(simul_tests
        tests/test_engine.cpp
        engine/src/ui/sokol_impl.c
        engine/src/ui/utf8_atlas.cpp
        engine/src/ui/renderer_2d.cpp
        engine/src/ui/widgets.cpp
        game/src/studio_scene.cpp
        game/src/viral_app.cpp
    )
    target_compile_definitions(simul_tests PRIVATE SIMUL_HEADLESS_GFX=1)
    target_include_directories(simul_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sokol
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts
        ${CMAKE_CURRENT_SOURCE_DIR}/game/src
    )
    target_link_libraries(simul_tests PRIVATE simul_core)
    add_test(NAME simul_tests COMMAND simul_tests)

    add_executable(simul_bench tests/bench_100k_agents.cpp)
    target_link_libraries(simul_bench PRIVATE simul_core)
endif()
