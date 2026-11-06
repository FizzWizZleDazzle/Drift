# Drift

A 2D game engine written in C++20, built on a Bevy-inspired Entity Component System (ECS). The same game runs on desktop and in the browser.

## Features

- **Bevy-style ECS** -- Typed systems with automatic dependency injection via `Res<T>`, `ResMut<T>`, `Query<T>`, and `Commands`
- **Plugin architecture** -- Modular subsystems (rendering, physics, audio, input, etc.) registered as plugins
- **2D renderer**: batched sprites and shapes, render layers, multiple cameras, render targets, custom shaders and post effects, pixel-perfect scaling
- **Desktop and web**: OpenGL 3.3 on desktop, WebGL2 in the browser through Emscripten, and an optional WebGPU backend for both
- **2D lighting**: point and spot lights, ambient light, hard shadows from occluders
- **Post effect library**: bloom, color grading, CRT, vignette, blur, pixelate, grayscale, sepia, screen fades
- **Box2D physics** -- Full 2D rigid body physics with collision events and sensors
- **Particle system** -- Rich ECS-based particle emitters with configurable shapes, forces, and blend modes
- **Text**: UTF-8 text with kerning, alignment, wrapping, outlines and shadows; a built-in font, TrueType/OpenType and BMFont pixel fonts, and a `Text` component
- **Sprite animation** -- Spritesheet-based animation clips with Aseprite import support
- **Tilemaps**: Tiled and LDtk levels with chunked rendering, parallax, animated tiles, autotiling, merged Box2D colliders, and object spawning
- **Audio**: WAV, OGG and MP3 playback, mixer buses with fades, music crossfades and playlists, 2D spatial sound sources
- **Gameplay toolkit**: tweens and easing, timers, rebindable input actions, save data, grid A* pathfinding, spatial hash queries, Y-sorting, seeded random numbers and noise
- **Parallel scheduler** -- Automatic multi-threaded system dispatch based on dependency analysis
- **ImGui integration** -- Built-in developer overlay via Dear ImGui
- **Scenes and prefabs**: Save and load entities as JSON through component reflection; spawn prefabs with per-copy overrides
- **Dev tools**: In-game entity inspector, performance panel, physics debug drawing, and hot reload of textures and scenes

## Quick Start

```bash
git clone https://github.com/FizzWizZleDazzle/Drift.git
cd drift
cmake -B build
cmake --build build
cd examples/hello_sprites && ../../build/hello_sprites
```

For the browser, build with Emscripten and serve the output:

```bash
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
cd build-web && python3 -m http.server 8000   # open http://localhost:8000/hello_sprites.html
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
| `render_showcase` | Shapes, nine-slice, minimap render target, light map, post effect, HUD layer |
| `audio` | Spatial sound sources you drag around, bus volume controls, music crossfades and playlists |
| `lighting` | Colored lights, spot light, shadows from walls and colliders, every library post effect |
| `text` | Built-in font, alignment, wrapping, UTF-8, outline and shadow, bitmap fonts, Text entities |
| `toolkit` | Tweens, timers, input actions with rebinding, A* pathfinding, spatial queries, Y-sort, save data |
| `scene_editor` | JSON scenes, prefabs with overrides, the entity inspector, hot reload |
| `tilemap` | Tiled and LDtk levels side by side: parallax, animated tiles, autotiling, merged colliders, object spawners |

Build examples with `cmake -B build -DDRIFT_BUILD_EXAMPLES=ON && cmake --build build`. Run the unit tests with `ctest --test-dir build`.

## Technology

| Layer | Library |
|-------|---------|
| ECS | [flecs](https://github.com/SanderMertens/flecs) v4.0.4 |
| Windowing, input, audio | [SDL3](https://libsdl.org) |
| Rendering | OpenGL 3.3 / WebGL2, optional WebGPU ([wgpu-native](https://github.com/gfx-rs/wgpu-native) / browser) |
| Physics | [Box2D](https://box2d.org) v3.0 |
| Debug UI | [Dear ImGui](https://github.com/ocornut/imgui) |
| Fonts | stb_truetype; built-in [Noto Sans](https://github.com/notofonts/latin-greek-cyrillic) subset (SIL OFL 1.1) |
| Images | stb_image |
| Audio decoding | stb_vorbis, dr_wav, dr_mp3 |
| Spritesheets | cute_aseprite |
| Tilemaps | nlohmann/json (Tiled and LDtk formats) |

## Documentation

Full documentation is available in the [`docs/`](docs/) directory. Build locally with:

```bash
mdbook serve docs
```

## License

See [LICENSE](LICENSE) for details.
