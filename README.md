# Drift

A 2D game engine for C++20 that runs the same game on desktop and in the browser.

Drift is for developers who want to write 2D games in code, with the parts most games need already in the box: rendering, lighting, physics, tilemaps, text, audio, input, scenes, and in-game dev tools. Game logic lives in plain functions on an Entity Component System; the engine schedules them across threads.

## Quick Start

Requires a C++20 compiler (GCC 12+, Clang 15+, MSVC 2022+), CMake 3.24+ and Git. Every other dependency is fetched during configure.

```bash
git clone https://github.com/FizzWizZleDazzle/Drift.git
cd Drift
cmake -B build
cmake --build build
cd examples/hello_sprites && ../../build/hello_sprites
```

For the browser, build with [Emscripten](https://emscripten.org) and serve the output:

```bash
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
cd build-web && python3 -m http.server 8000   # open http://localhost:8000/hello_sprites.html
```

A minimal game:

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
    cmd.spawn().insert(Transform2D{}, Camera{.active = true});
    cmd.spawn().insert(Transform2D{}, Sprite{.texture = tex});
}

int main(int argc, char** argv) {
    App app;
    app.args(argc, argv);   // settings come from drift.toml beside the game
    app.addPlugins<DefaultPlugins>();
    app.startup<setup>();
    return app.run();
}
```

with a `drift.toml` next to it:

```toml
[project]
name = "My Game"

[window]
width = 1280
height = 720
```

To use Drift from your own project, pull it in with `FetchContent` and link `drift`; call `drift_web_app(my_game)` to make the same target build for the web. See [Installation](docs/src/getting-started/installation.md).

[Releases](https://github.com/FizzWizZleDazzle/Drift/releases) carry a source archive to pin your build to, and the examples ready to run on Linux, macOS, Windows and in the browser.

## What You Get

- **Rendering**: batched sprites and shapes (also as rectangle and circle components), render layers with a HUD layer, multiple cameras (split screen, minimaps), render targets, custom shaders written once in WGSL, pixel-perfect scaling. OpenGL 3.3 on desktop, WebGL2 in the browser, and an optional WebGPU backend for both.
- **Lighting and effects**: point and spot lights with hard shadows, and one-line post effects such as bloom, CRT, color grading and screen fades.
- **Physics**: Box2D rigid bodies, colliders, sensors and collision events as ECS components.
- **Tilemaps**: Tiled and LDtk levels with parallax, animated tiles, autotiling and generated colliders.
- **Text**: UTF-8 text with wrapping, alignment, outlines and shadows; a built-in font plus TrueType and bitmap fonts.
- **Audio**: WAV, OGG and MP3, mixer buses, music crossfades and playlists, positional sound.
- **Sprites and particles**: spritesheet and Aseprite animation, configurable particle emitters, trails.
- **Gameplay toolkit**: tweens, timers, behavior components (lifetime, oscillation, follow), rebindable input actions for keyboard, mouse and gamepad, save data, A* pathfinding and flow fields, steering behaviors, spatial queries, Y-sorting, seeded random numbers and noise.
- **Procedural generation**: cellular-automata caves, BSP dungeons, random walks, wave function collapse, Poisson-disk sampling and weighted tables, the same for a seed on the desktop and in the browser, and written straight into tilemaps.
- **Candela scripting**: write a game in [candela](https://candela.lumenfx.dev) instead of C++, as components and systems declared with attributes, on the desktop and in the browser.
- **One settings file**: `drift.toml` holds the window, graphics backend, physics, audio, lighting and sections of your own game. Environment variables and `--set` flags override it for one run, and dev builds apply edits while the game runs.
- **Scenes and dev tools**: JSON scenes and prefabs through component reflection, hot reload, and an in-game inspector, performance panel and physics debug view.

## Examples

Each example runs from its own folder (`cd examples/<name> && ../../build/<name>`) and builds for the web as `<name>.html`.

| Example | Shows |
|---------|-------|
| `hello_sprites` | Entity spawning, queries, camera, input, hierarchy |
| `fireball` | Particle emitters, physics, sensor colliders |
| `flappy_bird` | State machines, collision events, sprite animation, UI |
| `asteroid_field` | Physics bodies, collision handling, trail renderers, camera shake |
| `particle_storm` | Advanced particle configuration, multiple emitters, emission shapes |
| `render_showcase` | Shapes, nine-slice, minimap render target, light map, post effect, HUD layer |
| `lighting` | Colored lights, spot light, shadows from walls and colliders, every library post effect |
| `tilemap` | Tiled and LDtk levels: parallax, animated tiles, autotiling, colliders, object spawners |
| `procgen` | Generated caves, dungeons, tunnels and wave-function-collapse ruins in a tilemap; Poisson-disk pickups, weighted spawns, flow-field chasers |
| `text` | Built-in font, alignment, wrapping, UTF-8, outline and shadow, bitmap fonts |
| `audio` | Positional sound sources, bus volume controls, music crossfades and playlists |
| `toolkit` | Tweens, timers, input actions with rebinding, A* pathfinding, spatial queries, save data |
| `scene_editor` | JSON scenes, prefabs with overrides, the entity inspector, hot reload |
| `flappy_bird`, `coin_dash` (candela) | Complete games in candela. Run with `drift run` from `bindings/candela/examples/<name>`; see `bindings/candela/README.md` |

Run the engine tests with `ctest --test-dir build`.

## Limitations

Drift is developed and tested on Linux; Windows and macOS builds are not yet verified. The web build runs every system on one thread, and web save data does not yet survive a page reload. Physics edits made through the inspector or a scene reload do not rebuild existing Box2D bodies.

## Documentation

The full guide and API reference are in [`docs/`](docs/), an [mdBook](https://rust-lang.github.io/mdBook/). Read it locally with `mdbook serve docs`.

Drift is built on [flecs](https://github.com/SanderMertens/flecs), [SDL3](https://libsdl.org), [Box2D](https://box2d.org), [Dear ImGui](https://github.com/ocornut/imgui), the [stb](https://github.com/nothings/stb) libraries, [dr_libs](https://github.com/mackron/dr_libs), [cute_aseprite](https://github.com/RandyGaul/cute_headers), [nlohmann/json](https://github.com/nlohmann/json), and optionally [wgpu-native](https://github.com/gfx-rs/wgpu-native). The built-in font is a [Noto Sans](https://github.com/notofonts/latin-greek-cyrillic) subset under the SIL Open Font License.
