# Drift

A lightweight 2D game engine written in C++20, built on a Bevy-inspired Entity Component System (ECS) architecture.

## Features

- **Bevy-style ECS** -- Typed systems with automatic dependency injection via `Res<T>`, `ResMut<T>`, `Query<T>`, and `Commands`
- **Plugin architecture** -- Modular subsystems (rendering, physics, audio, input, etc.) registered as plugins
- **SDL3 + GPU backend** -- Hardware-accelerated 2D rendering with SDL3's GPU API
- **Box2D physics** -- Full 2D rigid body physics with collision events and sensors
- **Particle system** -- Rich ECS-based particle emitters with configurable shapes, forces, and blend modes
- **Sprite animation** -- Spritesheet-based animation clips with Aseprite import support
- **Tilemap rendering** -- Tiled JSON map loading and rendering
- **Parallel scheduler** -- Automatic multi-threaded system dispatch based on dependency analysis
- **ImGui integration** -- Built-in developer overlay via Dear ImGui

## Quick Start

```bash
git clone https://github.com/FizzWizZleDazzle/Drift.git
cd drift
cmake -B build
cmake --build build
./build/hello_sprites
```

### Prerequisites

- C++20 compiler (GCC 12+, Clang 15+, MSVC 2022+)
- CMake 3.24+
- Git

All other dependencies are fetched automatically via CMake FetchContent.

## Hello World

```cpp
#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/AssetServer.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/plugins/DefaultPlugins.hpp>

using namespace drift;

void setup(ResMut<AssetServer> assets, Commands& cmd) {
    auto tex = assets->load<Texture>("assets/player.png");
    cmd.spawn().insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});
    cmd.spawn().insert(Transform2D{}, Sprite{.texture = tex});
}

int main() {
    App app;
    app.setConfig({.title = "My Game", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.startup<setup>();
    return app.run();
}
```

## Using Drift in Your Project

```cmake
cmake_minimum_required(VERSION 3.24)
project(my_game LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(
    drift
    GIT_REPOSITORY https://github.com/FizzWizZleDazzle/Drift.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
set(DRIFT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(drift)

add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE drift)
```

## Examples

| Example | Description |
|---------|-------------|
| `hello_sprites` | Entity spawning, queries, camera, input, hierarchy |
| `fireball` | Particle emitters, physics, sensor colliders |
| `flappy_bird` | State machines, collision events, sprite animation, UI |
| `asteroid_field` | Physics bodies, collision handling, trail renderers, camera shake |
| `particle_storm` | Advanced particle configuration, multiple emitters, emission shapes |

Build examples with `cmake -B build -DDRIFT_BUILD_EXAMPLES=ON && cmake --build build`.

## Technology

| Layer | Library |
|-------|---------|
| ECS | [flecs](https://github.com/SanderMertens/flecs) v4.0.4 |
| Windowing & Rendering | [SDL3](https://libsdl.org) GPU API |
| Physics | [Box2D](https://box2d.org) v3.0 |
| Debug UI | [Dear ImGui](https://github.com/ocornut/imgui) |
| Fonts | stb_truetype |
| Images | stb_image |
| Spritesheets | cute_aseprite |
| Tilemaps | nlohmann/json (Tiled format) |

## Documentation

Full documentation is available in the [`docs/`](docs/) directory. Build locally with:

```bash
mdbook serve docs
```

## License

See [LICENSE](LICENSE) for details.
