cmake_minimum_required(VERSION 3.20)
project(Razlom VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8 /W3)
endif()

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

option(RAZLOM_BUILD_CLIENT "Собрать нативный клиент (sokol: D3D11 / GL / GLES3)" ON)
option(RAZLOM_BUILD_TESTS "Собрать автотесты и бенчмарк ядра" ON)

# ---------------------------------------------------------------- ядро
add_library(razlom_core STATIC
    src/core/fluids.cpp
    src/core/mesher.cpp
    src/core/session.cpp
    src/core/structure.cpp
    src/core/world.cpp
    src/core/worldgen.cpp
    src/net/client.cpp
    src/net/server.cpp
    src/net/socket.cpp
)
target_include_directories(razlom_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
target_link_libraries(razlom_core PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(razlom_core PUBLIC ws2_32)
endif()

# ---------------------------------------------------------------- выделенный сервер
add_executable(razlom_server src/server/main.cpp)
target_link_libraries(razlom_server PRIVATE razlom_core)
set_target_properties(razlom_server PROPERTIES OUTPUT_NAME "razlom_server")

# ---------------------------------------------------------------- тесты
if(RAZLOM_BUILD_TESTS AND NOT ANDROID)
    enable_testing()

    add_executable(razlom_tests tests/test_core.cpp tests/test_net.cpp)
    target_link_libraries(razlom_tests PRIVATE razlom_core)
    add_test(NAME razlom_tests COMMAND razlom_tests)

    add_executable(razlom_ui_tests tests/test_ui.cpp src/client/font.cpp src/client/hud.cpp)
    target_link_libraries(razlom_ui_tests PRIVATE razlom_core)
    target_include_directories(razlom_ui_tests PRIVATE ${RAZLOM_INCLUDES})

    # CPU-превью клиента (не требует GPU): PNG-подобные PPM-кадры для визуальной проверки
    add_executable(razlom_preview tools/preview.cpp src/client/font.cpp src/client/hud.cpp)
    target_link_libraries(razlom_preview PRIVATE razlom_core)
    target_include_directories(razlom_preview PRIVATE ${RAZLOM_INCLUDES})

    add_executable(razlom_bench tests/bench.cpp)
    target_link_libraries(razlom_bench PRIVATE razlom_core)
endif()

# ---------------------------------------------------------------- клиент
set(RAZLOM_CLIENT_SOURCES
    src/client/app.cpp
    src/client/font.cpp
    src/client/hud.cpp
    src/client/renderer.cpp
    src/client/main.cpp
    src/ui/sokol_impl.c
)

if(RAZLOM_BUILD_CLIENT)
    set(RAZLOM_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sokol
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts
    )

    if(ANDROID)
        add_library(razlom SHARED ${RAZLOM_CLIENT_SOURCES})
        target_include_directories(razlom PRIVATE ${RAZLOM_INCLUDES})
        target_link_libraries(razlom PRIVATE razlom_core android log EGL GLESv3)
        target_link_options(razlom PRIVATE
            "-Wl,-u,ANativeActivity_onCreate"
            "-Wl,-z,max-page-size=16384"
            "-Wl,--build-id=sha1"
        )
    elseif(WIN32)
        add_executable(Razlom WIN32 ${RAZLOM_CLIENT_SOURCES})
        target_include_directories(Razlom PRIVATE ${RAZLOM_INCLUDES})
        target_link_libraries(Razlom PRIVATE
            razlom_core d3d11 dxgi d3dcompiler ole32 gdi32 user32 kernel32 winmm
        )
        if(MINGW)
            target_link_options(Razlom PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    elseif(APPLE)
        add_executable(Razlom ${RAZLOM_CLIENT_SOURCES})
        target_include_directories(Razlom PRIVATE ${RAZLOM_INCLUDES})
        set_source_files_properties(src/ui/sokol_impl.c PROPERTIES COMPILE_FLAGS "-x objective-c")
        find_library(COCOA_LIBRARY Cocoa)
        find_library(QUARTZCORE_LIBRARY QuartzCore)
        find_library(METAL_LIBRARY Metal)
        find_library(METALKIT_LIBRARY MetalKit)
        target_link_libraries(Razlom PRIVATE razlom_core
            ${COCOA_LIBRARY} ${QUARTZCORE_LIBRARY} ${METAL_LIBRARY} ${METALKIT_LIBRARY})
    else()
        add_executable(Razlom ${RAZLOM_CLIENT_SOURCES})
        target_include_directories(Razlom PRIVATE ${RAZLOM_INCLUDES})
        find_package(OpenGL REQUIRED)
        find_package(X11 REQUIRED)
        target_link_libraries(Razlom PRIVATE
            razlom_core OpenGL::GL X11::X11 ${X11_Xi_LIB} ${X11_Xcursor_LIB} dl m
        )
    endif()
endif()
