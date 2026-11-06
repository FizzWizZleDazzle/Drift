# Project Structure

This chapter covers two things: how the Drift engine source code is organized, and how to structure your own game project that depends on Drift.

## Engine directory layout

```
drift/
  include/drift/          <-- public API headers (what you #include)
    components/            <-- ECS component types
    plugins/               <-- Plugin headers (header-only)
    resources/             <-- Resource types (singletons)
    effects/               <-- Post effect library
    particles/             <-- Particle system types
    gameplay/              <-- Gameplay toolkit: tweens, timers, input actions, save data, spatial hash, pathfinding
    math/                  <-- Vector helpers, intersection tests, random numbers, noise
  src/                     <-- implementation files (not public)
    core/                  <-- App, AssetServer, Log
    ecs/                   <-- World, Commands, ScriptSystem
    renderer/              <-- RendererResource, RenderSnapshot
    gfx/                   <-- graphics device interface and backends
      gl/                  <-- OpenGL 3.3 / WebGL2 backend
      wgpu/                <-- WebGPU backend (optional)
      shaders/             <-- built-in shaders and the shader prelude
    input/                 <-- InputResource
    audio/                 <-- AudioResource, mixer, decoders
    physics/               <-- PhysicsResource
    sprite/                <-- SpriteResource
    font/                  <-- FontResource
    particles/             <-- ParticleResource (legacy), ParticleSystem (ECS-based)
    tilemap/               <-- TilemapResource
    ui/                    <-- UIResource
    lighting/              <-- 2D lights, shadows and their shaders
    effects/               <-- post effect library and its shaders
    gameplay/              <-- Gameplay toolkit implementations
    math/                  <-- Noise
  examples/                <-- example programs
    hello_sprites/
    fireball/
    flappy_bird/
    asteroid_field/
    particle_storm/
    render_showcase/
    audio/
    lighting/
    text/
    toolkit/
  tests/                   <-- engine tests (ctest)
  cmake/                   <-- drift_add_shaders, drift_web_app, web page template
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
| `AssetServer.hpp` | `AssetServer` resource with `load<Texture>()`, `load<Sound>()`, `load<Font>()` template API, path lookup and hot reload hooks. |
| `Reflect.hpp` | Component reflection: describe a component's fields once for scenes and the inspector. |
| `Scene.hpp` | `SceneResource`: save, load and spawn JSON scenes and prefabs. |
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
| `AudioSource.hpp` | `AudioSource`, `AudioListener` | A sound played from an entity, optionally positional, and the entity it is heard from. |
| `Light2D.hpp` | `PointLight2D`, `SpotLight2D`, `LightOccluder2D` | 2D lights and shadow-casting occluders. Need `LightingPlugin`. |
| `Text.hpp` | `Text` | A string drawn at the entity's transform: font, size, color, alignment, wrapping, outline, shadow, layer. |

#### `include/drift/plugins/` -- Plugins

Each plugin registers resources and systems in its `build()` method. Most are header-only.
| `Tilemap.hpp` | `Tilemap` | Places a loaded Tiled or LDtk map in the world: draw order, render layer, culling camera, colliders, object spawning. |


Each plugin registers resources and systems in its `build()` method. Most are header-only; `TilemapPlugin` builds in `src/tilemap/`.

| Header | Plugin | What it registers |
|--------|--------|-------------------|
| `DefaultPlugins.hpp` | `DefaultPlugins` | All plugins below as a group. Also provides `MinimalPlugins` (input + renderer only). |
| `RendererPlugin.hpp` | `RendererPlugin` | `RendererResource` and the frame begin/end systems. |
| `InputPlugin.hpp` | `InputPlugin` | `InputResource` for keyboard, mouse, and gamepad state. |
| `AudioPlugin.hpp` | `AudioPlugin` | `AudioResource`, the `AudioSource`/`AudioListener` components and the system that plays and positions them. |
| `PhysicsPlugin.hpp` | `PhysicsPlugin` | `PhysicsResource`, physics step system, body sync, collision events. |
| `SpritePlugin.hpp` | `SpritePlugin` | `SpriteResource`, sprite rendering extraction. |
| `FontPlugin.hpp` | `FontPlugin` | `FontResource`, the `Text` component, and the system that draws `Text` entities. |
| `ParticlePlugin.hpp` | `ParticlePlugin` | `ParticleSystemResource`, ECS-based particle update and rendering. |
| `TilemapPlugin.hpp` | `TilemapPlugin` | `TilemapResource`, the `Tilemap` component, map drawing, colliders and object spawning. |
| `UIPlugin.hpp` | `UIPlugin` | `UIResource` for ImGui-based UI. |
| `HierarchyPlugin.hpp` | `HierarchyPlugin` | Registers `GlobalTransform2D`, propagates transforms through Parent/Children hierarchy in PostUpdate. Despawn cascades to children. |
| `SpriteAnimationPlugin.hpp` | `SpriteAnimationPlugin` | Updates `Sprite::srcRect` from `SpriteAnimator` each frame. |
| `CameraPlugin.hpp` | `CameraPlugin` | `CameraFollow` and `CameraShake` systems in PostUpdate. |
| `TrailPlugin.hpp` | `TrailPlugin` | Trail update and rendering systems. |
| `PostEffectsPlugin.hpp` | `PostEffectsPlugin` | `PostEffects` resource: the post effect library. |
| `LightingPlugin.hpp` | `LightingPlugin` | `LightingSettings`, light and occluder components, light maps per camera. Not in `DefaultPlugins`. |
| `TimerPlugin.hpp` | `TimerPlugin` | Ticks `Timer` components and sends `TimerFinished` events. |
| `InputActionsPlugin.hpp` | `InputActionsPlugin` | `InputActions` resource: named actions and axes over `InputResource`. |
| `SpatialPlugin.hpp` | `SpatialPlugin` | `SpatialIndex` resource rebuilt from entities with `SpatialBounds`. |
| `TweenPlugin.hpp` | `TweenPlugin` | `Tweens` resource, advanced in PostUpdate; `TweenFinished` events. |
| `YSortPlugin.hpp` | `YSortPlugin` | Sets `Sprite::zOrder` from y for entities with `YSort`. |
| `SavePlugin.hpp` | `SavePlugin` | `SaveData` opened on the per-user save directory. Not in `DefaultPlugins`; it needs your game's name. |
| `ScenePlugin.hpp` | `ScenePlugin` | `SceneResource` for JSON scenes and prefabs. |
| `HotReloadPlugin.hpp` | `HotReloadPlugin` | Reloads textures and scenes when their files change (desktop; in `DefaultPlugins` with `DRIFT_DEV`). |

#### `include/drift/resources/` -- Resource types

Resources are singleton objects stored in `App`. Systems access them through `Res<T>` (read-only) or `ResMut<T>` (read-write) parameters.

| Header | Resource | Description |
|--------|----------|-------------|
| `Time.hpp` | `Time` | `delta` (float, seconds), `elapsed` (double, seconds), `frame` (uint64_t) |
| `RendererResource.hpp` | `RendererResource` | Textures, sprites, shapes, cameras, render targets, shaders, post effects. |
| `InputResource.hpp` | `InputResource` | `keyHeld()`, `keyPressed()`, `keyReleased()`, `mousePosition()`, `mouseButtonHeld()`, `mouseWheelDelta()` |
| `AudioResource.hpp` | `AudioResource` | Sound loading (WAV, OGG, MP3), playback, mixer buses, music. |
| `PhysicsResource.hpp` | `PhysicsResource` | Box2D world wrapper, body creation, raycasting, contact queries. |
| `SpriteResource.hpp` | `SpriteResource` | Sprite sheet and texture management internals. |
| `FontResource.hpp` | `FontResource` | Font loading (TrueType, OpenType, BMFont, built-in font), text drawing and measuring. |
| `ParticleResource.hpp` | `ParticleResource` | Legacy handle-based particle API. |
| `ParticleSystemResource.hpp` | `ParticleSystemResource` | ECS-based particle system manager. |
| `TilemapResource.hpp` | `TilemapResource` | Tiled and LDtk loading, chunked map rendering, tile queries, merged colliders, autotiling. |
| `UIResource.hpp` | `UIResource` | ImGui integration and UI state. |

#### `include/drift/particles/` -- Particle system internals

| Header | Type | Description |
|--------|------|-------------|
| `EmitterConfig.hpp` | `EmitterConfig` | Full emitter configuration: emission shapes, bursts, drag, rotation, local/world space, pre-warm. |
| `ParticlePool.hpp` | `ParticlePool` | SoA (Structure of Arrays) compact storage with O(1) swap-remove. |

#### `include/drift/gameplay/` -- Gameplay toolkit

Each header stands alone; use the ones your game needs. See the Gameplay chapter.

| Header | Types | Description |
|--------|-------|-------------|
| `Easing.hpp` | `Ease`, `ease()` | Easing curves (Penner set, Back, Elastic, Bounce, Spring). |
| `Tween.hpp` | `Tweens`, `TweenBuilder`, `TweenFinished` | Animate transforms, tint, alpha, camera zoom, or any float. |
| `Timer.hpp` | `Timer`, `Cooldown`, `TimerFinished` | One-shot and repeating timers, cooldowns. |
| `InputActions.hpp` | `InputActions`, `InputBinding` | Named actions and axes, rebinding, JSON round trip. |
| `SaveData.hpp` | `SaveData` | Persistent JSON key-value store in the per-user directory. |
| `SpatialHash.hpp` | `SpatialHash` | Uniform-grid neighbour and area queries. |
| `Pathfinding.hpp` | `NavGrid`, `findPath` | Grid A* with weights and diagonal rules. |

#### `include/drift/math/` -- Math utilities

| Header | Types | Description |
|--------|-------|-------------|
| `Vector.hpp` | free functions | `lerp`, `clamp`, `cross`, `perp`, `rotate`, `angle`, `moveTowards`, `smoothDamp`, `approx`, and more. |
| `Intersect.hpp` | `Circle`, `Segment`, `RayHit` | Overlap and intersection tests between rects, circles, segments and rays. |
| `Random.hpp` | `Rng` | Seeded PCG32 generator: ranges, weighted pick, shuffle. |
| `Noise.hpp` | `Noise` | Seeded 2D value, Perlin and simplex noise with fBm. |

### `src/` -- Implementation files

The `src/` directory mirrors the subsystem layout. Each subdirectory contains the `.cpp` files for one subsystem:

- `src/core/` -- `App.cpp`, `AssetServer.cpp`, `Log.cpp`
- `src/ecs/` -- `World.cpp`, `Commands.cpp`, `ScriptSystem.cpp`
- `src/renderer/` -- `RendererResource.cpp`, `RenderSnapshot.cpp`
- `src/input/` -- `InputResource.cpp`
- `src/audio/` -- `AudioResource.cpp`, `Mixer.cpp` (voices, buses, music), `Decoder.cpp` (WAV, OGG, MP3)
- `src/physics/` -- `PhysicsResource.cpp`
- `src/sprite/` -- `SpriteResource.cpp`
- `src/font/` -- `FontResource.cpp`, `GlyphAtlas.cpp` (glyph texture pages), `BMFont.cpp` (`.fnt` parser), `default/` (the built-in font and its license)
- `src/particles/` -- `ParticleResource.cpp` (legacy), `ParticleSystem.cpp` (ECS-based)
- `src/tilemap/` -- `TilemapResource.cpp`, `TiledLoader.cpp`, `LdtkLoader.cpp`, `TilemapPlugin.cpp`
- `src/ui/` -- `UIResource.cpp`
- `src/lighting/` -- `Lighting.cpp` and the light shader
- `src/effects/` -- `PostEffects.cpp` and the effect shaders
- `src/gameplay/` -- `Tween.cpp`, `InputActions.cpp`, `SaveData.cpp`, `SpatialHash.cpp`, `Pathfinding.cpp`
- `src/math/` -- `Noise.cpp`
- `src/core/`: `App.cpp`, `AssetServer.cpp`, `Reflect.cpp`, `Log.cpp`
- `src/ecs/`: `World.cpp`, `Commands.cpp`, `ScriptSystem.cpp`
- `src/renderer/`: `RendererResource.cpp`, `RenderSnapshot.cpp`
- `src/input/`: `InputResource.cpp`
- `src/audio/`: `AudioResource.cpp`
- `src/physics/`: `PhysicsResource.cpp`
- `src/sprite/`: `SpriteResource.cpp`
- `src/font/`: `FontResource.cpp`
- `src/particles/`: `ParticleResource.cpp` (legacy), `ParticleSystem.cpp` (ECS-based)
- `src/tilemap/`: `TilemapResource.cpp`
- `src/ui/`: `UIResource.cpp`, `DevInspector.cpp`, `DevPerformance.cpp`
- `src/scene/`: `SceneResource.cpp`
- `src/hotreload/`: `HotReload.cpp`

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
| `audio` | Spatial audio sources, mixer buses, music crossfades and playlists |
| `toolkit` | Tweens, timers, input actions with rebinding, A* on a noise-generated grid, spatial queries, Y-sort, save data |
| `scene_editor` | JSON scenes, prefabs with overrides, the entity inspector, hot reload |
| `render_showcase` | Shapes, nine-slice, minimap render target, light map, post effect, HUD layer |
| `tilemap` | Tiled and LDtk levels, parallax, animated tiles, autotiling, merged colliders, object spawners |

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
  fonts/         <-- TTF/OTF fonts, BMFont .fnt files with their page PNGs
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
