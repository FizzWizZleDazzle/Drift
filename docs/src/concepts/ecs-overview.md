# ECS Overview

Drift uses an Entity Component System (ECS) architecture built on top of [flecs](https://github.com/SanderMertens/flecs) v4. The engine wraps flecs with a typed, Bevy-style API so you rarely interact with flecs directly. Entities are lightweight IDs, components are plain C++ structs, and systems are free functions that operate on queries and resources.

## Entities

An entity is a unique 64-bit identifier. It has no data or behavior on its own -- it is just an ID that components are attached to.

```cpp
using EntityId = uint64_t;
constexpr EntityId InvalidEntityId = 0;
```

Entities are created through `Commands` (the deferred mutation API) or directly through the `World`. In systems, you always use `Commands`:

```cpp
void spawnPlayer(Commands& cmd) {
    EntityId player = cmd.spawn()
        .insert(Transform2D{.position = {100.f, 200.f}},
                Sprite{.zOrder = 1.f});
    // 'player' now holds the new entity's ID
}
```

You can store an `EntityId` in a resource or component to reference it later:

```cpp
struct GameState : public Resource {
    EntityId player = InvalidEntityId;
    EntityId camera = InvalidEntityId;
};
```

`InvalidEntityId` (0) serves as the null/sentinel value. You can check whether an ID is valid:

```cpp
if (game->player != InvalidEntityId) {
    cmd.entity(game->player).insert(Transform2D{.position = newPos});
}
```

## Components

Components are plain, trivially copyable C++ structs. They hold data, nothing else. No inheritance, no virtual methods, no special base class.

```cpp
struct Health {
    float current = 100.f;
    float max = 100.f;
};

struct Velocity {
    Vec2 linear;
    float angular = 0.f;
};

struct EnemyTag {};  // zero-size marker component
```

### Built-in Components

Drift provides several built-in components that the engine subsystems operate on:

| Component | Description |
|-----------|-------------|
| `Transform2D` | Position, rotation, scale |
| `Sprite` | Texture, tint, z-order, source rect, origin |
| `Camera` | Zoom, active flag |
| `Name` | Debug label (64-char fixed buffer) |
| `Parent` | Parent entity ID (hierarchy) |
| `Children` | Array of child entity IDs (hierarchy) |
| `GlobalTransform2D` | Computed world-space transform |
| `RigidBody2D` | Physics body type, damping, gravity scale |
| `BoxCollider2D` | Box collision shape |
| `CircleCollider2D` | Circle collision shape |
| `Velocity2D` | Linear and angular velocity |
| `ParticleEmitter` | Particle system configuration |
| `SpriteAnimator` | Animation clips for sprite sheets |
| `CameraFollow` | Camera follow target and smoothing |
| `CameraShake` | Screen shake intensity and decay |
| `TrailRenderer` | Trail effect behind moving entities |

### Marker Components

Zero-size structs act as tags for filtering queries. They carry no data but let you distinguish entity archetypes:

```cpp
struct PlayerTag {};
struct EnemyTag {};
struct BulletTag {};

// Spawn a player entity
cmd.spawn().insert(Transform2D{}, Sprite{}, PlayerTag{});

// Query only entities that have PlayerTag
QueryMut<Transform2D, With<PlayerTag>> players;
```

### Component Registration

Most components are registered automatically. When a `Query` or `QueryMut` encounters a component type that hasn't been registered, it registers it on the fly using the C++ `typeid` name. Plugins that manage their own components typically register them explicitly in `build()`:

```cpp
void build(App& app) override {
    app.world().registerComponent<CameraFollow>("CameraFollow");
    app.world().registerComponent<CameraShake>("CameraShake");
}
```

Explicit registration lets you provide a clean name (instead of the mangled `typeid` name) and ensures the component is ready before any queries run.

## World

The `World` class wraps the flecs world. It manages entity creation, component storage, and query iteration. You typically do not interact with the `World` directly in game code -- instead, you use `Commands` for mutations and `Query`/`QueryMut` for reads. However, plugins and advanced code can access it through `app.world()`.

```cpp
// In a plugin's build() method
void build(App& app) override {
    World& world = app.world();

    // Register a component
    world.registerComponent<MyComponent>("MyComponent");

    // Check if an entity is alive
    bool alive = world.isAlive(someEntityId);

    // Read a component directly (bypassing queries)
    const Transform2D* t = world.get<Transform2D>(entityId, world.transform2dId());
}
```

Key `World` operations:

| Method | Description |
|--------|-------------|
| `createEntity()` | Allocate a new entity ID |
| `destroyEntity(id)` | Remove an entity and all its components |
| `isAlive(id)` | Check if an entity still exists |
| `registerComponent<T>(name)` | Register a typed component |
| `setComponent(id, compId, data, size)` | Set raw component data |
| `getComponent(id, compId)` | Read raw component data |
| `hasComponent(id, compId)` | Check if an entity has a component |
| `componentRegistry()` | Access the `ComponentRegistry` |

In systems, prefer `Commands` for mutations and `Query`/`QueryMut` for reads. Direct `World` access bypasses the deferred command buffer, which can cause issues with the parallel scheduler.

## ComponentRegistry

The `ComponentRegistry` maps C++ types to flecs `ComponentId` values. It is maintained by the `World` and used internally by queries and commands to translate between the typed API and the underlying flecs storage.

```cpp
const ComponentRegistry& registry = world.componentRegistry();

// Check if a type is registered
if (registry.has<Health>()) {
    ComponentId id = registry.get<Health>();
}

// Look up by string name
ComponentId id = registry.getByName("Transform2D");
```

You rarely need to use the `ComponentRegistry` directly. The typed APIs (`Commands::insert<T>()`, `Query<T>`) handle registration and lookup for you. When a `Commands::insert<T>()` encounters an unregistered type, it auto-registers it through the `World`.

## How Flecs Fits In

Flecs is the underlying ECS implementation. Drift's typed API is a compile-time layer on top of it:

- **Entity IDs** are flecs entity IDs (`uint64_t`).
- **Component storage** uses flecs archetypes for cache-friendly iteration.
- **Queries** build flecs query expressions from C++ template parameters.
- **The World** calls flecs functions through its `Impl` (pimpl pattern), keeping flecs headers out of the public API.

You can access the raw flecs world pointer via `world.flecsWorld()` if you need direct flecs interop, but this is an advanced escape hatch and not needed for normal use.

## Typical ECS Pattern

A complete example showing entities, components, and how they connect to systems:

```cpp
#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/Query.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/Time.hpp>

using namespace drift;

// Custom components
struct Health {
    float current = 100.f;
    float max = 100.f;
};

struct Speed {
    float value = 150.f;
};

struct PlayerTag {};
struct EnemyTag {};

// Resource to track game state
struct GameState : public Resource {
    DRIFT_RESOURCE(GameState)
    EntityId player = InvalidEntityId;
};

// Startup system: create entities
void setup(ResMut<GameState> game, Commands& cmd) {
    // Spawn player
    game->player = cmd.spawn()
        .insert(Transform2D{.position = {0.f, 0.f}},
                Sprite{}, Health{.current = 100.f, .max = 100.f},
                Speed{.value = 200.f}, PlayerTag{});

    // Spawn enemies
    for (int i = 0; i < 10; ++i) {
        cmd.spawn()
            .insert(Transform2D{.position = {i * 80.f, -200.f}},
                    Sprite{}, Health{.current = 50.f, .max = 50.f},
                    Speed{.value = 100.f}, EnemyTag{});
    }

    // Camera
    cmd.spawn().insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});
}

// Update system: damage all enemies over time
void damageEnemies(Res<Time> time,
                   QueryMut<Health, With<EnemyTag>> enemies,
                   Commands& cmd) {
    enemies.iterWithEntity([&](EntityId id, Health& hp) {
        hp.current -= 10.f * time->delta;
        if (hp.current <= 0.f) {
            cmd.entity(id).despawn();
        }
    });
}

int main() {
    App app;
    app.setConfig({.title = "ECS Demo"});
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameState>();
    app.startup<setup>("setup");
    app.update<damageEnemies>("damage_enemies");
    return app.run();
}
```
