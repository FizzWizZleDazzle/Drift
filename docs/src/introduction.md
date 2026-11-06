# Introduction

**Drift** is a lightweight 2D game engine written in C++20, built on a Bevy-inspired Entity Component System (ECS) architecture. It provides a typed, data-driven API for building games and interactive applications.

## Key Features

- **Bevy-style ECS** — Typed systems with automatic dependency injection via `Res<T>`, `ResMut<T>`, `Query<T>`, and `Commands`
- **Plugin architecture** — Modular subsystems (rendering, physics, audio, input, etc.) registered as plugins
- **2D renderer**: batched sprites and shapes, render layers, multiple cameras, render targets, custom shaders and post effects, pixel-perfect scaling. OpenGL 3.3 on desktop, WebGL2 in the browser, and an optional WebGPU backend for both
- **2D lighting**: point and spot lights, ambient light, hard shadows from occluders
- **Post effect library**: bloom, color grading, CRT, vignette, blur, pixelate, grayscale, sepia, screen fades
- **Box2D physics** — Full 2D rigid body physics with collision events and sensors
- **Particle system** — Rich ECS-based particle emitters with configurable shapes, forces, and blend modes
- **Text**: UTF-8 text with kerning, alignment, wrapping, outlines and shadows; a built-in font, TrueType/OpenType and BMFont pixel fonts, and a `Text` component
- **Sprite animation** — Spritesheet-based animation clips with Aseprite import support
- **Tilemaps**: Tiled and LDtk levels with chunked rendering, parallax, animated tiles, autotiling, merged Box2D colliders, and object spawning
- **Audio**: WAV, OGG and MP3 playback, mixer buses with fades, music crossfades and playlists, 2D spatial sound sources
- **Gameplay toolkit**: tweens and easing, timers, rebindable input actions, save data, grid A* pathfinding, spatial hash queries, Y-sorting, seeded random numbers and noise
- **ImGui integration** — Built-in debug UI via Dear ImGui
- **Scenes and prefabs**: Save and load entities as JSON through component reflection; spawn prefabs with per-copy overrides
- **Dev tools**: In-game entity inspector, performance panel, physics debug drawing, and hot reload of textures and scenes
- **Parallel scheduler** — Automatic multi-threaded system dispatch based on dependency analysis

## Architecture Overview

Drift follows the same architectural principles as [Bevy](https://bevyengine.org):

1. **Entities** are lightweight IDs (`EntityId` = `uint64_t`)
2. **Components** are plain structs attached to entities
3. **Systems** are functions that operate on components via queries and resources
4. **Resources** are singleton data shared across systems
5. **Plugins** bundle related resources and systems together

```cpp
#include <drift/App.hpp>
#include <drift/plugins/DefaultPlugins.hpp>

using namespace drift;

void hello(Res<Time> time) {
    // Runs every frame
}

int main() {
    App app;
    app.setConfig({.title = "My Game", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.update<hello>();
    return app.run();
}
```

## Under the Hood

| Layer | Technology |
|-------|-----------|
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
