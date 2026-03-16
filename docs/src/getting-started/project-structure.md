# Project Structure

This chapter covers two things: how the Drift engine source code is organized, and how to structure your own game project that depends on Drift.

## Engine directory layout

```
drift/
  include/drift/          <-- public API headers (what you #include)
    components/            <-- ECS component types
    plugins/               <-- Plugin headers (header-only)
    resources/             <-- Resource types (singletons)
    particles/             <-- Particle system types
  src/                     <-- implementation files (not public)
    core/                  <-- App, AssetServer, Log
    ecs/                   <-- World, Commands, ScriptSystem
    renderer/              <-- RendererResource, RenderSnapshot
    input/                 <-- InputResource
    audio/                 <-- AudioResource
    physics/               <-- PhysicsResource
    sprite/                <-- SpriteResource
    font/                  <-- FontResource
    particles/             <-- ParticleResource (legacy), ParticleSystem (ECS-based)
    tilemap/               <-- TilemapResource
    ui/                    <-- UIResource
  examples/                <-- example programs
    hello_sprites/
    fireball/
    flappy_bird/
    asteroid_field/
    particle_storm/
  docs/                    <-- this documentation (mdBook)
  CMakeLists.txt           <-- build configuration
```

### `include/drift/` -- Public headers

This is the only directory you need to include in your project. Every `#include <drift/...>` path resolves here.

#### Core headers

| Header | Description |
|--------|-------------|
| `App.hpp` | The `App` class -- entry point, resource store, system scheduler, and main loop. This is the first header you include in any Drift program. |
| `Types.hpp` | Fundamental types used everywhere: `Vec2`, `Vec3`, `Rect`, `Color`, `ColorF`, `Transform2D`, `EntityId`, `ComponentId`, `Phase`, `Key`, `MouseButton`, `BodyType`, and more. |
| `Commands.hpp` | `Commands` deferred mutation buffer and `EntityCommands` fluent builder. Used to spawn/despawn entities and insert/remove components. |
| `Query.hpp` | `Query<...>` (read-only) and `QueryMut<...>` (read-write) typed query system parameters. Supports `iter()`, `iterWithEntity()`, `single()`, `isEmpty()`, and `contains()`. |
| `Resource.hpp` | `Resource` base class, `Res<T>` (read-only) and `ResMut<T>` (read-write) wrappers used as system parameters. |
| `Plugin.hpp` | `Plugin` and `PluginGroup` base classes for modular subsystem registration. |
| `System.hpp` | System traits, `AccessDescriptor`, and compile-time dependency extraction from system function signatures. |
| `AssetServer.hpp` | `AssetServer` resource with `load<Texture>()`, `load<Sound>()`, `load<Font>()` template API. |
| `World.hpp` | Low-level ECS world (wraps flecs). Typically accessed indirectly through `Query` and `Commands`. |
| `Events.hpp` | `Events<T>` double-buffered storage, `EventWriter<T>`, `EventReader<T>` system parameters. |
| `State.hpp` | `State<T>`, `NextState<T>` for finite state machines with `OnEnter`/`OnExit` system hooks. |
| `Bundle.hpp` | `Bundle<Ts...>` tuple-like helper for grouping components into reusable bundles. |
| `QueryTraits.hpp` | Query filter types: `With<T>`, `Without<T>`, `Optional<T>`, `Changed<T>`, `Added<T>`. |
| `Config.hpp` | `Config` struct for window and engine settings. |
| `Log.hpp` | Logging utilities. |
| `Math.hpp` | Additional math helpers. |
| `Handle.hpp` | `TextureHandle`, `SoundHandle`, `FontHandle` -- lightweight typed handles for assets. |
| `Entity.hpp` | Entity-related utilities. |
| `ComponentRegistry.hpp` | Maps C++ types to flecs `ComponentId` values. Used internally by `Commands` and `Query`. |

#### `include/drift/components/` -- Component types

Components are plain structs attached to entities. They carry data but no behavior.

| Header | Component | Description |
|--------|-----------|-------------|
| `Sprite.hpp` | `Sprite` | Texture handle, source rect, origin, tint, flip, z-order, visibility |
| `Camera.hpp` | `Camera` | Zoom, rotation, active flag. One active camera drives the renderer. |
| `Physics.hpp` | `RigidBody2D`, `BoxCollider2D`, `CircleCollider2D`, `Velocity2D` | Physics simulation components. `RigidBody2D` has type (Static/Dynamic/Kinematic), gravity scale, fixed rotation. |
| `Name.hpp` | `Name` | Debug label (64-char fixed buffer). Trivially copyable. |
| `Hierarchy.hpp` | `Parent`, `Children` | Parent-child entity relationships. `Parent{EntityId}` on child, `Children{ids[], count}` on parent. |
| `GlobalTransform.hpp` | `GlobalTransform2D` | Computed world-space transform, automatically propagated from hierarchy. |
| `ParticleEmitter.hpp` | `ParticleEmitter` | ECS-based particle emitter component with `EmitterConfig`. |
| `SpriteAnimator.hpp` | `SpriteAnimator` | Sprite sheet animation with `AnimationClip` definitions. |
| `CameraController.hpp` | `CameraShake`, `CameraFollow` | Camera shake (trigger with magnitude, auto-decay) and smooth follow (target entity, smoothing, dead zone). |
| `TrailRenderer.hpp` | `TrailRenderer` | Trail rendering with width, lifetime, color lerp, and minimum distance. |

#### `include/drift/plugins/` -- Plugins

Plugins are header-only. Each one registers resources and systems in its `build()` method.

| Header | Plugin | What it registers |
|--------|--------|-------------------|
| `DefaultPlugins.hpp` | `DefaultPlugins` | All plugins below as a group. Also provides `MinimalPlugins` (input + renderer only). |
| `RendererPlugin.hpp` | `RendererPlugin` | `RendererResource`, sprite rendering systems, GPU pipeline setup. |
| `InputPlugin.hpp` | `InputPlugin` | `InputResource` for keyboard, mouse, and gamepad state. |
| `AudioPlugin.hpp` | `AudioPlugin` | `AudioResource` for sound playback. |
| `PhysicsPlugin.hpp` | `PhysicsPlugin` | `PhysicsResource`, physics step system, body sync, collision events. |
| `SpritePlugin.hpp` | `SpritePlugin` | `SpriteResource`, sprite rendering extraction. |
| `FontPlugin.hpp` | `FontPlugin` | `FontResource` for text rendering. |
| `ParticlePlugin.hpp` | `ParticlePlugin` | `ParticleSystemResource`, ECS-based particle update and rendering. |
| `TilemapPlugin.hpp` | `TilemapPlugin` | `TilemapResource` for Tiled JSON map loading and rendering. |
| `UIPlugin.hpp` | `UIPlugin` | `UIResource` for ImGui-based UI. |
| `HierarchyPlugin.hpp` | `HierarchyPlugin` | Registers `GlobalTransform2D`, propagates transforms through Parent/Children hierarchy in PostUpdate. Despawn cascades to children. |
| `SpriteAnimationPlugin.hpp` | `SpriteAnimationPlugin` | Updates `Sprite::srcRect` from `SpriteAnimator` each frame. |
| `CameraPlugin.hpp` | `CameraPlugin` | `CameraFollow` and `CameraShake` systems in PostUpdate. |
| `TrailPlugin.hpp` | `TrailPlugin` | Trail update and rendering systems. |

#### `include/drift/resources/` -- Resource types

Resources are singleton objects stored in `App`. Systems access them through `Res<T>` (read-only) or `ResMut<T>` (read-write) parameters.

| Header | Resource | Description |
|--------|----------|-------------|
| `Time.hpp` | `Time` | `delta` (float, seconds), `elapsed` (double, seconds), `frame` (uint64_t) |
| `RendererResource.hpp` | `RendererResource` | GPU state, draw calls, `drawSpriteBatch()` for bulk rendering. |
| `InputResource.hpp` | `InputResource` | `keyHeld()`, `keyPressed()`, `keyReleased()`, `mousePosition()`, `mouseButtonHeld()`, `mouseWheelDelta()` |
| `AudioResource.hpp` | `AudioResource` | Sound playback and mixing. |
| `PhysicsResource.hpp` | `PhysicsResource` | Box2D world wrapper, body creation, raycasting, contact queries. |
| `SpriteResource.hpp` | `SpriteResource` | Sprite sheet and texture management internals. |
| `FontResource.hpp` | `FontResource` | Font atlas and text rendering internals. |
| `ParticleResource.hpp` | `ParticleResource` | Legacy handle-based particle API. |
| `ParticleSystemResource.hpp` | `ParticleSystemResource` | ECS-based particle system manager. |
| `TilemapResource.hpp` | `TilemapResource` | Tiled map loader and renderer. |
| `UIResource.hpp` | `UIResource` | ImGui integration and UI state. |

#### `include/drift/particles/` -- Particle system internals

| Header | Type | Description |
|--------|------|-------------|
| `EmitterConfig.hpp` | `EmitterConfig` | Full emitter configuration: emission shapes, bursts, drag, rotation, local/world space, pre-warm. |
| `ParticlePool.hpp` | `ParticlePool` | SoA (Structure of Arrays) compact storage with O(1) swap-remove. |

### `src/` -- Implementation files

The `src/` directory mirrors the subsystem layout. Each subdirectory contains the `.cpp` files for one subsystem:

- `src/core/` -- `App.cpp`, `AssetServer.cpp`, `Log.cpp`
- `src/ecs/` -- `World.cpp`, `Commands.cpp`, `ScriptSystem.cpp`
- `src/renderer/` -- `RendererResource.cpp`, `RenderSnapshot.cpp`
- `src/input/` -- `InputResource.cpp`
- `src/audio/` -- `AudioResource.cpp`
- `src/physics/` -- `PhysicsResource.cpp`
- `src/sprite/` -- `SpriteResource.cpp`
- `src/font/` -- `FontResource.cpp`
- `src/particles/` -- `ParticleResource.cpp` (legacy), `ParticleSystem.cpp` (ECS-based)
- `src/tilemap/` -- `TilemapResource.cpp`
- `src/ui/` -- `UIResource.cpp`

You should not need to read or modify these files when building a game. They are linked into the `drift` static library automatically.

### `examples/` -- Example programs

Each example is a single `main.cpp` file demonstrating different engine features:

| Example | Features demonstrated |
|---------|----------------------|
| `hello_sprites` | Commands, entity spawning, queries, camera, input, marker components, `withChildren`, `Optional<T>`, `Name` |
| `fireball` | Particle emitters, physics, sensor colliders |
| `flappy_bird` | State machines, collision events, sprite animation, UI |
| `asteroid_field` | Entity spawning/despawning, physics bodies, collision handling |
| `particle_storm` | Advanced particle system configuration, multiple emitters |

## Typical user project structure

When building your own game with Drift, a clean starting structure looks like this:

```
my_game/
  CMakeLists.txt
  main.cpp
  assets/
    sprites/
      player.png
      enemy.png
    sounds/
      jump.wav
    fonts/
      main.ttf
```

### `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.24)
project(my_game LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

Setting `DRIFT_BUILD_EXAMPLES OFF` avoids building the example binaries in your project.

### `main.cpp`

```cpp
#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/AssetServer.hpp>
#include <drift/Query.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/Time.hpp>

using namespace drift;

void setup(ResMut<AssetServer> assets, Commands& cmd) {
    auto tex = assets->load<Texture>("assets/sprites/player.png");

    cmd.spawn()
        .insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});

    cmd.spawn()
        .insert(Transform2D{}, Sprite{.texture = tex});
}

void update(Res<InputResource> input, Res<Time> time,
            QueryMut<Transform2D, With<Sprite>> sprites) {
    Vec2 dir{};
    if (input->keyHeld(Key::W)) dir.y -= 1.f;
    if (input->keyHeld(Key::S)) dir.y += 1.f;
    if (input->keyHeld(Key::A)) dir.x -= 1.f;
    if (input->keyHeld(Key::D)) dir.x += 1.f;

    sprites.iter([&](Transform2D& t) {
        t.position += dir.normalized() * 200.f * time->delta;
    });
}

int main() {
    App app;
    app.setConfig({.title = "My Game", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.startup<setup>();
    app.update<update>();
    return app.run();
}
```

### Assets directory

Place your assets in an `assets/` directory at the same level as the executable (or the project root if you run from there). Asset paths passed to `AssetServer::load()` are relative to the working directory. A common layout:

```
assets/
  sprites/       <-- PNG images for sprites and sprite sheets
  sounds/        <-- WAV files for sound effects
  fonts/         <-- TTF/OTF fonts
  maps/          <-- Tiled JSON map files
  particles/     <-- Particle texture assets
```

### Scaling up

As your project grows, consider splitting systems into separate files:

```
my_game/
  CMakeLists.txt
  src/
    main.cpp
    systems/
      player.hpp     <-- player movement system
      player.cpp
      enemies.hpp    <-- enemy AI systems
      enemies.cpp
      ui.hpp         <-- HUD and menu systems
      ui.cpp
    components/
      game.hpp       <-- game-specific components and resources
  assets/
    ...
```

Each system file defines one or more free functions with typed parameters. Register them all in `main.cpp`:

```cpp
#include "systems/player.hpp"
#include "systems/enemies.hpp"
#include "systems/ui.hpp"

int main() {
    App app;
    app.setConfig({.title = "My Game", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameState>();
    app.startup<setupGame>();
    app.update<movePlayer>("movePlayer");
    app.update<updateEnemies>("updateEnemies");
    app.update<updateUI>("updateUI");
    return app.run();
}
```

You can also write your own `Plugin` subclass to encapsulate setup for a subsystem:

```cpp
class EnemyPlugin : public Plugin {
public:
    const char* name() const override { return "EnemyPlugin"; }

    void build(App& app) override {
        app.initResource<EnemySpawnerState>();
        app.startup<setupEnemies>("setupEnemies");
        app.update<spawnWaves>("spawnWaves");
        app.update<updateEnemyAI>("updateEnemyAI");
    }
};

// In main:
app.addPlugin<EnemyPlugin>();
```

This keeps your `main.cpp` clean and makes it easy to toggle features on and off during development.
