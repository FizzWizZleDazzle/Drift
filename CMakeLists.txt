cmake_minimum_required(VERSION 3.24)
project(drift VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DRIFT_DEV "Enable dev overlay and tools" ON)
option(DRIFT_BUILD_EXAMPLES "Build example programs" ON)
option(DRIFT_BUILD_TESTS "Build engine tests" ${PROJECT_IS_TOP_LEVEL})
option(DRIFT_BACKEND_GL "Build the OpenGL / WebGL2 renderer backend" ON)
option(DRIFT_BUILD_CANDELA "Build drift, the program that runs Candela games (bindings/candela; needs Rust)" OFF)
option(DRIFT_BACKEND_WEBGPU "Build the WebGPU renderer backend (wgpu-native on desktop, emdawnwebgpu on the web)" OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DriftShaders.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DriftWeb.cmake)

# ---------- Dependencies via FetchContent ----------
include(FetchContent)

# SDL3
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)

# flecs
FetchContent_Declare(
    flecs
    GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
    GIT_TAG        v4.0.4
    GIT_SHALLOW    TRUE
)

# Box2D 3.x
FetchContent_Declare(
    box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG        v3.0.0
    GIT_SHALLOW    TRUE
)
set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)

# Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
    GIT_SHALLOW    TRUE
)

# stb (stb_image, stb_truetype)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# cute_aseprite
FetchContent_Declare(
    cute_aseprite
    GIT_REPOSITORY https://github.com/RandyGaul/cute_headers.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# dr_libs (dr_wav, dr_mp3 for audio decoding)
FetchContent_Declare(
    dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG        35b5c392cc15456736c440a68dbe073178bbffe0
)

# nlohmann/json (for Tiled JSON)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

# toml++ - header-only, reads drift.toml
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(SDL3 flecs box2d json tomlplusplus)

# stb - header-only, just need the path
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

# cute_aseprite - header-only
FetchContent_GetProperties(cute_aseprite)
if(NOT cute_aseprite_POPULATED)
    FetchContent_Populate(cute_aseprite)
endif()

# dr_libs - header-only
FetchContent_GetProperties(dr_libs)
if(NOT dr_libs_POPULATED)
    FetchContent_Populate(dr_libs)
endif()

# imgui - build as a static library
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC SDL3::SDL3-static)

# ---------- Drift Engine Library ----------
set(DRIFT_SOURCES
    # Core
    src/core/App.cpp
    src/core/Log.cpp
    src/core/Settings.cpp

    # ECS
    src/ecs/World.cpp
    src/ecs/ScriptSystem.cpp
    src/ecs/Commands.cpp
    src/ecs/Hierarchy.cpp

    # Render snapshot
    src/renderer/RenderSnapshot.cpp

    # Graphics device seam
    src/gfx/Registry.cpp

    # Asset server
    src/core/AssetServer.cpp
    src/core/Reflect.cpp

    # Subsystem resources
    src/renderer/RendererResource.cpp
    src/renderer/Image.cpp
    src/input/InputResource.cpp
    src/audio/AudioResource.cpp
    src/audio/Decoder.cpp
    src/audio/Mixer.cpp
    src/physics/PhysicsResource.cpp
    src/sprite/SpriteResource.cpp
    src/font/FontResource.cpp
    src/font/GlyphAtlas.cpp
    src/font/BMFont.cpp
    src/particles/ParticleResource.cpp
    src/particles/ParticleSystem.cpp
    src/tilemap/TilemapResource.cpp
    src/tilemap/TilemapCommon.cpp
    src/tilemap/TiledLoader.cpp
    src/tilemap/LdtkLoader.cpp
    src/tilemap/TilemapPlugin.cpp
    src/ui/UIResource.cpp
    src/ui/DevInspector.cpp
    src/ui/DevPerformance.cpp

    # Scenes, prefabs and hot reload
    src/scene/SceneResource.cpp
    src/hotreload/HotReload.cpp

    # Optional units
    src/lighting/Lighting.cpp
    src/effects/PostEffects.cpp
)

# Gameplay toolkit: tweens, timers, input actions, save data, spatial hash,
# pathfinding, random numbers, noise and procedural generation. Each unit
# stands alone; the core does not use them.
set(DRIFT_GAMEPLAY_SOURCES
    src/gameplay/Tween.cpp
    src/gameplay/InputActions.cpp
    src/gameplay/SaveData.cpp
    src/gameplay/SpatialHash.cpp
    src/gameplay/Pathfinding.cpp
    src/math/Random.cpp
    src/math/Noise.cpp
    src/procgen/Grid.cpp
    src/procgen/Generators.cpp
    src/procgen/Wfc.cpp
    src/procgen/TilemapFill.cpp
)
# Seeded randomness, noise, pathfinding and the generators must give the
# same numbers on every platform; fused multiply-add would round
# differently where the CPU has it.
set_source_files_properties(
    src/math/Random.cpp src/math/Noise.cpp src/gameplay/Pathfinding.cpp
    src/procgen/Grid.cpp src/procgen/Generators.cpp src/procgen/Wfc.cpp
    PROPERTIES COMPILE_OPTIONS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ffp-contract=off>")

# ---------- Graphics backends ----------
# Each backend adds its sources and the name of its register function.
# The generated file below calls every enabled one; nothing else in the
# engine names a backend.
set(DRIFT_GFX_BACKENDS "")
set(DRIFT_GFX_BACKEND_SOURCES "")
set(DRIFT_GFX_BACKEND_LIBS "")

if(DRIFT_BACKEND_GL)
    list(APPEND DRIFT_GFX_BACKENDS gl)
    list(APPEND DRIFT_GFX_BACKEND_SOURCES
        src/gfx/gl/GLDevice.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    if(EMSCRIPTEN)
        set_source_files_properties(${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
            PROPERTIES COMPILE_DEFINITIONS IMGUI_IMPL_OPENGL_ES3)
    endif()
endif()

if(DRIFT_BACKEND_WEBGPU)
    include(src/gfx/wgpu/WebGPU.cmake)
endif()

if(NOT DRIFT_GFX_BACKENDS)
    message(FATAL_ERROR "Enable at least one graphics backend (DRIFT_BACKEND_GL, DRIFT_BACKEND_WEBGPU)")
endif()

set(DRIFT_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/drift_generated)
set(_decls "")
set(_calls "")
foreach(_b IN LISTS DRIFT_GFX_BACKENDS)
    string(APPEND _decls "void registerBackend_${_b}();\n")
    string(APPEND _calls "    registerBackend_${_b}();\n")
endforeach()
file(CONFIGURE OUTPUT ${DRIFT_GEN_DIR}/GfxBackends.gen.cpp CONTENT
"// Generated by CMake from DRIFT_GFX_BACKENDS.\n#include \"gfx/Device.hpp\"\nnamespace drift::gfx {\n${_decls}void registerBuiltinBackends() {\n${_calls}}\n}\n" @ONLY)

# Shaders embedded as string constants. Each unit that ships shaders calls
# this with its own header name and namespace.
function(drift_embed_shaders header ns)
    set(_body "")
    foreach(_f IN LISTS ARGN)
        file(READ ${CMAKE_CURRENT_SOURCE_DIR}/${_f} _src)
        get_filename_component(_name ${_f} NAME)
        string(REPLACE "." "_" _name ${_name})
        string(APPEND _body "inline constexpr const char* ${_name} = R\"DRIFT_SHADER(${_src})DRIFT_SHADER\";\n")
    endforeach()
    file(CONFIGURE OUTPUT ${DRIFT_GEN_DIR}/${header} CONTENT
"// Generated by CMake.\n#pragma once\nnamespace ${ns} {\n${_body}}\n" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ARGN})
endfunction()

drift_embed_shaders(drift_shaders.gen.hpp drift::gfx::shaders
    src/gfx/shaders/prelude.wgsl
    src/gfx/shaders/prelude.vert.glsl
    src/gfx/shaders/prelude.frag.glsl
    src/gfx/shaders/sprite.wgsl
    src/gfx/shaders/sprite.frag.glsl
)

drift_embed_shaders(lighting_shaders.gen.hpp drift::lighting::shaders
    src/lighting/shaders/light.wgsl
    src/lighting/shaders/light.frag.glsl
)

drift_embed_shaders(effects_shaders.gen.hpp drift::effects::shaders
    src/effects/shaders/bloom_prefilter.wgsl
    src/effects/shaders/bloom_prefilter.frag.glsl
    src/effects/shaders/bloom_down.wgsl
    src/effects/shaders/bloom_down.frag.glsl
    src/effects/shaders/bloom_up.wgsl
    src/effects/shaders/bloom_up.frag.glsl
    src/effects/shaders/bloom_composite.wgsl
    src/effects/shaders/bloom_composite.frag.glsl
    src/effects/shaders/blur.wgsl
    src/effects/shaders/blur.frag.glsl
    src/effects/shaders/vignette.wgsl
    src/effects/shaders/vignette.frag.glsl
    src/effects/shaders/crt.wgsl
    src/effects/shaders/crt.frag.glsl
    src/effects/shaders/color_grading.wgsl
    src/effects/shaders/color_grading.frag.glsl
    src/effects/shaders/pixelate.wgsl
    src/effects/shaders/pixelate.frag.glsl
    src/effects/shaders/tone.wgsl
    src/effects/shaders/tone.frag.glsl
    src/effects/shaders/fade.wgsl
    src/effects/shaders/fade.frag.glsl
)

# Built-in default font (Noto Sans subset, SIL OFL 1.1; see src/font/default),
# embedded as a byte array so text works with no asset files.
set(DRIFT_DEFAULT_FONT src/font/default/NotoSans-Regular-Subset.ttf)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/${DRIFT_DEFAULT_FONT} _font_hex HEX)
string(LENGTH "${_font_hex}" _font_hex_len)
math(EXPR _font_size "${_font_hex_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _font_bytes "${_font_hex}")
file(CONFIGURE OUTPUT ${DRIFT_GEN_DIR}/DefaultFont.gen.cpp CONTENT
"// Generated by CMake from ${DRIFT_DEFAULT_FONT}.\n#include <cstddef>\nnamespace drift::fontdata {\nextern const unsigned char kDefaultFont[] = {${_font_bytes}};\nextern const size_t kDefaultFontSize = ${_font_size};\n}\n" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DRIFT_DEFAULT_FONT})

add_library(drift STATIC ${DRIFT_SOURCES} ${DRIFT_GAMEPLAY_SOURCES} ${DRIFT_GFX_BACKEND_SOURCES}
    ${DRIFT_GEN_DIR}/GfxBackends.gen.cpp
    ${DRIFT_GEN_DIR}/DefaultFont.gen.cpp)

target_include_directories(drift PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(drift PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DRIFT_GEN_DIR}
    ${stb_SOURCE_DIR}
    ${cute_aseprite_SOURCE_DIR}
    ${dr_libs_SOURCE_DIR}
)

target_link_libraries(drift PUBLIC
    SDL3::SDL3-static
    flecs::flecs_static
    box2d
    imgui_lib
    nlohmann_json::nlohmann_json
    ${DRIFT_GFX_BACKEND_LIBS}
)
target_link_libraries(drift PRIVATE tomlplusplus::tomlplusplus)

if(DRIFT_DEV)
    target_compile_definitions(drift PUBLIC DRIFT_DEV=1)
endif()

# Browser build: WebGL2, growable memory, no threads.
if(EMSCRIPTEN)
    target_link_options(drift PUBLIC
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=1048576
    )
endif()

# Platform libraries
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(drift PUBLIC m dl pthread)
endif()

# ---------- Examples ----------
if(DRIFT_BUILD_EXAMPLES)
    add_executable(hello_sprites examples/hello_sprites/main.cpp)
    target_link_libraries(hello_sprites PRIVATE drift)
    drift_web_app(hello_sprites PROJECT examples/hello_sprites)

    add_executable(fireball examples/fireball/main.cpp)
    target_link_libraries(fireball PRIVATE drift)
    drift_web_app(fireball PROJECT examples/fireball)

    add_executable(flappy_bird examples/flappy_bird/main.cpp)
    target_link_libraries(flappy_bird PRIVATE drift)
    drift_web_app(flappy_bird PROJECT examples/flappy_bird)
    option(DRIFT_FLAPPY_IMGUI_UI "Use ImGui for Flappy Bird UI instead of sprite overlays" OFF)
    if(DRIFT_FLAPPY_IMGUI_UI)
        target_compile_definitions(flappy_bird PRIVATE FLAPPY_IMGUI_UI)
    endif()

    add_executable(asteroid_field examples/asteroid_field/main.cpp)
    target_link_libraries(asteroid_field PRIVATE drift)
    drift_web_app(asteroid_field PROJECT examples/asteroid_field)

    add_executable(particle_storm examples/particle_storm/main.cpp)
    target_link_libraries(particle_storm PRIVATE drift)
    drift_web_app(particle_storm PROJECT examples/particle_storm)

    add_executable(render_showcase examples/render_showcase/main.cpp)
    target_link_libraries(render_showcase PRIVATE drift)
    # The WGSL sprite shader is translated for GL when naga is installed.
    find_program(DRIFT_NAGA naga HINTS $ENV{HOME}/.cargo/bin)
    if(DRIFT_NAGA)
        drift_add_shaders(render_showcase
            OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders/render_showcase
            SHADERS examples/render_showcase/shaders/wobble.wgsl)
        drift_web_app(render_showcase PROJECT examples/render_showcase
            PRELOAD ${CMAKE_CURRENT_BINARY_DIR}/shaders/render_showcase@/shaders/render_showcase)
    else()
        drift_web_app(render_showcase PROJECT examples/render_showcase)
    endif()

    add_executable(audio examples/audio/main.cpp)
    target_link_libraries(audio PRIVATE drift)
    drift_web_app(audio PROJECT examples/audio)

    add_executable(lighting examples/lighting/main.cpp)
    target_link_libraries(lighting PRIVATE drift)
    drift_web_app(lighting PROJECT examples/lighting)

    add_executable(text examples/text/main.cpp)
    target_link_libraries(text PRIVATE drift)
    drift_web_app(text PROJECT examples/text)

    add_executable(toolkit examples/toolkit/main.cpp)
    target_link_libraries(toolkit PRIVATE drift)
    drift_web_app(toolkit PROJECT examples/toolkit)

    add_executable(scene_editor examples/scene_editor/main.cpp)
    target_link_libraries(scene_editor PRIVATE drift)
    drift_web_app(scene_editor PROJECT examples/scene_editor)

    add_executable(tilemap examples/tilemap/main.cpp)
    target_link_libraries(tilemap PRIVATE drift)
    drift_web_app(tilemap PROJECT examples/tilemap)

    add_executable(procgen examples/procgen/main.cpp)
    target_link_libraries(procgen PRIVATE drift)
    drift_web_app(procgen PROJECT examples/procgen)
endif()

# ---------- Tests ----------
if(DRIFT_BUILD_TESTS AND NOT EMSCRIPTEN)
    enable_testing()
    add_executable(drift_audio_tests tests/audio_tests.cpp)
    target_link_libraries(drift_audio_tests PRIVATE drift)
    target_include_directories(drift_audio_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(drift_audio_tests PRIVATE
        DRIFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME audio COMMAND drift_audio_tests)

    add_executable(drift_tests tests/main.cpp tests/test_gameplay.cpp tests/test_settings.cpp tests/test_procgen.cpp
        tests/test_procanim.cpp)
    target_link_libraries(drift_tests PRIVATE drift)
    add_test(NAME drift_tests COMMAND drift_tests)

    # App-level tests open a window, through SDL's offscreen driver.
    add_executable(drift_app_tests tests/main.cpp tests/test_app.cpp)
    target_link_libraries(drift_app_tests PRIVATE drift)
    add_test(NAME drift_app_tests COMMAND drift_app_tests)
    # A scheduler that hangs fails the test instead of the whole run.
    set_tests_properties(drift_app_tests PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;SDL_AUDIO_DRIVER=dummy"
        TIMEOUT 120)
elseif(DRIFT_BUILD_TESTS)
    # The procedural generation tests under Node: seeded randomness, noise
    # and the generators must match the desktop's golden values on the web
    # too. (The other suites write files and catch exceptions, which this
    # build does not enable.)
    enable_testing()
    add_executable(drift_procgen_tests tests/main.cpp tests/test_procgen.cpp)
    target_link_libraries(drift_procgen_tests PRIVATE drift)
    target_link_options(drift_procgen_tests PRIVATE -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1)
    add_test(NAME drift_procgen_tests COMMAND drift_procgen_tests)
    # The hierarchy and procedural animation math, as the browser runs it.
    add_executable(drift_procanim_tests tests/main.cpp tests/test_procanim.cpp)
    target_link_libraries(drift_procanim_tests PRIVATE drift)
    target_link_options(drift_procanim_tests PRIVATE -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1)
    add_test(NAME drift_procanim_tests COMMAND drift_procanim_tests)
endif()

# ---------- Candela package ----------
if(DRIFT_BUILD_CANDELA)
    add_subdirectory(bindings/candela)
endif()
