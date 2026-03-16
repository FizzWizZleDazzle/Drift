# Introduction

**Drift** is a lightweight 2D game engine written in C++20, built on a Bevy-inspired Entity Component System (ECS) architecture. It provides a typed, data-driven API for building games and interactive applications.

## Key Features

- **Bevy-style ECS** — Typed systems with automatic dependency injection via `Res<T>`, `ResMut<T>`, `Query<T>`, and `Commands`
- **Plugin architecture** — Modular subsystems (rendering, physics, audio, input, etc.) registered as plugins
- **SDL3 + GPU backend** — Hardware-accelerated 2D rendering with SDL3's GPU API
- **Box2D physics** — Full 2D rigid body physics with collision events and sensors
- **Particle system** — Rich ECS-based particle emitters with configurable shapes, forces, and blend modes
- **Sprite animation** — Spritesheet-based animation clips with Aseprite import support
- **Tilemap rendering** — Tiled JSON map loading and rendering
- **ImGui integration** — Built-in debug UI via Dear ImGui
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
| Windowing & Rendering | [SDL3](https://libsdl.org) GPU API |
| Physics | [Box2D](https://box2d.org) v3.0 |
| Debug UI | [Dear ImGui](https://github.com/ocornut/imgui) |
| Fonts | stb_truetype |
| Images | stb_image |
| Spritesheets | cute_aseprite |
| Tilemaps | nlohmann/json (Tiled format) |
