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

# nlohmann/json (for Tiled JSON)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL3 flecs box2d json)

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
    # SDL3 + SDL_GPU backends
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
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

    # ECS
    src/ecs/World.cpp
    src/ecs/ScriptSystem.cpp
    src/ecs/Commands.cpp

    # Render snapshot
    src/renderer/RenderSnapshot.cpp

    # Asset server
    src/core/AssetServer.cpp

    # Subsystem resources
    src/renderer/RendererResource.cpp
    src/input/InputResource.cpp
    src/audio/AudioResource.cpp
    src/physics/PhysicsResource.cpp
    src/sprite/SpriteResource.cpp
    src/font/FontResource.cpp
    src/particles/ParticleResource.cpp
    src/particles/ParticleSystem.cpp
    src/tilemap/TilemapResource.cpp
    src/ui/UIResource.cpp
)

add_library(drift STATIC ${DRIFT_SOURCES})

target_include_directories(drift PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(drift PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${stb_SOURCE_DIR}
    ${cute_aseprite_SOURCE_DIR}
)

target_link_libraries(drift PUBLIC
    SDL3::SDL3-static
    flecs::flecs_static
    box2d
    imgui_lib
    nlohmann_json::nlohmann_json
)

if(DRIFT_DEV)
    target_compile_definitions(drift PUBLIC DRIFT_DEV=1)
endif()

# Platform libraries
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(drift PUBLIC m dl pthread)
endif()

# ---------- Examples ----------
if(DRIFT_BUILD_EXAMPLES)
    add_executable(hello_sprites examples/hello_sprites/main.cpp)
    target_link_libraries(hello_sprites PRIVATE drift)

    add_executable(fireball examples/fireball/main.cpp)
    target_link_libraries(fireball PRIVATE drift)

    add_executable(flappy_bird examples/flappy_bird/main.cpp)
    target_link_libraries(flappy_bird PRIVATE drift)
    option(DRIFT_FLAPPY_IMGUI_UI "Use ImGui for Flappy Bird UI instead of sprite overlays" OFF)
    if(DRIFT_FLAPPY_IMGUI_UI)
        target_compile_definitions(flappy_bird PRIVATE FLAPPY_IMGUI_UI)
    endif()

    add_executable(asteroid_field examples/asteroid_field/main.cpp)
    target_link_libraries(asteroid_field PRIVATE drift)

    add_executable(particle_storm examples/particle_storm/main.cpp)
    target_link_libraries(particle_storm PRIVATE drift)
endif()
