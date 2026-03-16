# Hierarchy

Drift supports entity hierarchies through `Parent` and `Children` components. When entities form a parent-child tree, the `HierarchyPlugin` propagates transforms down the tree so that children move, rotate, and scale relative to their parent. The system also provides the `Name` component for debug labeling and `GlobalTransform2D` for the computed world-space transform.

## Components

### Parent

Marks an entity as a child of another entity.

```cpp
struct Parent {
    EntityId entity = InvalidEntityId;
};
```

**Header:** `#include <drift/components/Hierarchy.hpp>`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `entity` | `EntityId` | `InvalidEntityId` | The parent entity ID. |

### Children

Tracks the child entities of a parent.

```cpp
static constexpr int MaxChildren = 16;

struct Children {
    EntityId ids[MaxChildren] = {};
    int count = 0;

    void add(EntityId id);
    void remove(EntityId id);
};
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `ids` | `EntityId[16]` | all `0` | Fixed-size array of child entity IDs. |
| `count` | `int` | `0` | Number of active children. |

| Method | Description |
|--------|-------------|
| `add(EntityId)` | Appends a child ID. No-op if the array is full (16 children max). |
| `remove(EntityId)` | Removes a child by swapping with the last entry. |

> **Note:** The fixed array limit of 16 children per entity is intentional for cache-friendly iteration. If you need more than 16 children, consider restructuring your hierarchy with intermediate grouping entities.

### Name

A debug label for entities. Useful for logging and the developer overlay.

```cpp
struct Name {
    char value[64] = {};

    Name() = default;
    explicit Name(const char* v);
};
```

**Header:** `#include <drift/components/Name.hpp>`

The `Name` component is a fixed 64-character buffer. It is trivially copyable and safe for ECS storage.

```cpp
cmd.spawn()
    .insert(Transform2D{}, Sprite{.texture = tex}, Name{"Player"});
```

### GlobalTransform2D

The computed world-space transform after composing the full parent chain.

```cpp
struct GlobalTransform2D {
    Vec2 position;
    float rotation = 0.f;
    Vec2 scale = Vec2::one();

    static GlobalTransform2D compose(const GlobalTransform2D& parent,
                                     const Transform2D& local);
    static GlobalTransform2D from(const Transform2D& t);
};
```

**Header:** `#include <drift/components/GlobalTransform.hpp>`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `position` | `Vec2` | `{0, 0}` | World-space position after applying the full parent chain. |
| `rotation` | `float` | `0.f` | World-space rotation (sum of all parent rotations plus local). |
| `scale` | `Vec2` | `{1, 1}` | World-space scale (product of all parent scales times local). |

| Static Method | Description |
|--------------|-------------|
| `compose(parent, local)` | Computes a child's world transform given the parent's `GlobalTransform2D` and the child's local `Transform2D`. Handles rotation, scale, and position correctly. |
| `from(transform)` | Creates a `GlobalTransform2D` directly from a `Transform2D` (used for root entities with no parent). |

You generally do not set `GlobalTransform2D` yourself -- the `HierarchyPlugin` computes it automatically.

## Plugin

The `HierarchyPlugin` is included in `DefaultPlugins`. It:

1. Registers the `GlobalTransform2D` component.
2. Adds a **transform propagation** system in `PostUpdate` that performs a BFS traversal from root entities (those without a `Parent` component) down through the tree, composing `GlobalTransform2D` at each level.

If you are not using `DefaultPlugins`, add it manually:

```cpp
app.addPlugin<HierarchyPlugin>();
```

## Building hierarchies

### Using withChildren

The most convenient way to create a parent-child hierarchy is the `withChildren` method on `EntityCommands`. It automatically sets up `Parent` and `Children` components:

```cpp
void setup(ResMut<AssetServer> assets, Commands& cmd) {
    TextureHandle tex = assets->load<Texture>("assets/ship.png");
    TextureHandle turretTex = assets->load<Texture>("assets/turret.png");

    cmd.spawn()
        .insert(
            Transform2D{.position = {400.f, 300.f}},
            Sprite{.texture = tex, .origin = {32.f, 32.f}},
            Name{"Ship"}
        )
        .withChildren([&](ChildSpawner& spawner) {
            // Left turret
            spawner.spawn().insert(
                Transform2D{.position = {-20.f, -10.f}},
                Sprite{.texture = turretTex, .origin = {4.f, 4.f}},
                Name{"Left Turret"}
            );
            // Right turret
            spawner.spawn().insert(
                Transform2D{.position = {20.f, -10.f}},
                Sprite{.texture = turretTex, .origin = {4.f, 4.f}},
                Name{"Right Turret"}
            );
        });
}
```

In this example, the turrets' `Transform2D` positions are local to the ship. When the ship moves or rotates, the turrets follow automatically.

### Manual hierarchy setup

You can also build hierarchies manually by inserting `Parent` and `Children` components directly:

```cpp
void setup(Commands& cmd) {
    EntityId parent = cmd.spawn()
        .insert(Transform2D{.position = {400.f, 300.f}}, Name{"Parent"});

    EntityId child = cmd.spawn()
        .insert(
            Transform2D{.position = {50.f, 0.f}},   // 50px to the right of parent
            Parent{.entity = parent},
            Name{"Child"}
        );

    // Update the parent's Children component
    cmd.entity(parent)
        .insert<Children>(Children{});

    // Note: withChildren handles this automatically and is preferred
}
```

## Transform propagation

When entities form a hierarchy, `Transform2D` on child entities is interpreted as **local** (relative to the parent). The `HierarchyPlugin` computes the final world-space `GlobalTransform2D` for each entity.

### Example: rotating parent

```cpp
void rotateShip(Res<Time> time, QueryMut<Transform2D, With<ShipTag>> ships) {
    ships.iter([&](Transform2D& t) {
        t.rotation += 1.f * time->delta;   // rotate 1 radian per second
    });
}
```

Both the ship and its turret children rotate together because the hierarchy system composes the parent's rotation into each child's `GlobalTransform2D`.

### Local vs world position

- `Transform2D` on a **root entity** (no `Parent`) is in world space.
- `Transform2D` on a **child entity** (has `Parent`) is relative to its parent.
- `GlobalTransform2D` always contains the final world-space result.

```cpp
// Read the world-space position of any entity, whether root or child
void readWorldPositions(Query<GlobalTransform2D, Name> entities) {
    entities.iter([](const GlobalTransform2D& gt, const Name& name) {
        LOG_INFO("{} is at world ({}, {})", name.value,
                 gt.position.x, gt.position.y);
    });
}
```

## Cascading despawn

When you despawn a parent entity, all of its children are automatically despawned as well. The `Commands::flush()` system walks the `Children` component and recursively removes descendants:

```cpp
// Despawning the ship also despawns its turrets
cmd.entity(shipId).despawn();
```

## Complete example

```cpp
#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/AssetServer.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/Hierarchy.hpp>
#include <drift/components/Name.hpp>
#include <drift/components/GlobalTransform.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/Time.hpp>

#include <cmath>

using namespace drift;

struct SolarSystem : public Resource {
    TextureHandle sunTex;
    TextureHandle planetTex;
    TextureHandle moonTex;
};

void setup(ResMut<SolarSystem> ss, ResMut<AssetServer> assets, Commands& cmd) {
    ss->sunTex = assets->load<Texture>("assets/sun.png");
    ss->planetTex = assets->load<Texture>("assets/planet.png");
    ss->moonTex = assets->load<Texture>("assets/moon.png");

    cmd.spawn().insert(Transform2D{}, Camera{.zoom = 0.8f, .active = true});

    // Sun at the center, with planets as children, and moons as grandchildren
    cmd.spawn()
        .insert(
            Transform2D{.position = {0.f, 0.f}},
            Sprite{.texture = ss->sunTex, .origin = {32.f, 32.f}, .zOrder = 1.f},
            Name{"Sun"}
        )
        .withChildren([&](ChildSpawner& spawner) {
            // Planet 1
            spawner.spawn()
                .insert(
                    Transform2D{.position = {150.f, 0.f}, .scale = {0.5f, 0.5f}},
                    Sprite{.texture = ss->planetTex, .origin = {16.f, 16.f}},
                    Name{"Planet 1"}
                )
                .withChildren([&](ChildSpawner& moonSpawner) {
                    moonSpawner.spawn().insert(
                        Transform2D{.position = {40.f, 0.f}, .scale = {0.5f, 0.5f}},
                        Sprite{.texture = ss->moonTex, .origin = {8.f, 8.f}},
                        Name{"Moon"}
                    );
                });

            // Planet 2
            spawner.spawn().insert(
                Transform2D{.position = {-250.f, 0.f}, .scale = {0.7f, 0.7f}},
                Sprite{.texture = ss->planetTex, .origin = {16.f, 16.f}},
                Name{"Planet 2"}
            );
        });
}

// Rotate the sun -- children and grandchildren follow automatically
void rotateSun(Res<Time> time, QueryMut<Transform2D, With<Name>> named) {
    named.iterWithEntity([&](EntityId, Transform2D& t) {
        t.rotation += 0.5f * time->delta;
    });
}

int main() {
    App app;
    app.setConfig({.title = "Hierarchy Example", .width = 800, .height = 600});
    app.addPlugins<DefaultPlugins>();
    app.initResource<SolarSystem>();
    app.startup<setup>();
    app.update<rotateSun>();
    return app.run();
}
```

In this example, rotating the sun causes the planets to orbit around it, and moons orbit their respective planets -- all handled automatically by transform propagation.

## Notes

- The maximum number of direct children per entity is 16 (`MaxChildren`). This is a compile-time constant.
- `GlobalTransform2D` is computed in `PostUpdate`. If you read it during `Update`, you are reading the previous frame's value. For most game logic this is fine.
- The `Name` component is a 64-character fixed buffer. It is purely for debugging and has no effect on engine behavior.
- Despawning a parent cascades to all descendants. There is no way to "orphan" children -- if you want a child to survive, reparent it first by changing its `Parent` component and updating the old and new parent's `Children` component.
- `withChildren` is the preferred way to build hierarchies because it correctly sets up both `Parent` and `Children` in a single fluent call.
