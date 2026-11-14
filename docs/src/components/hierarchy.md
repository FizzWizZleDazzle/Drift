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
static constexpr int MaxChildren = 64;

struct Children {
    EntityId ids[MaxChildren] = {};
    int count = 0;

    void add(EntityId id);
    void remove(EntityId id);
};
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `ids` | `EntityId[64]` | all `0` | Fixed-size array of child entity IDs. |
| `count` | `int` | `0` | Number of active children. |

| Method | Description |
|--------|-------------|
| `add(EntityId)` | Appends a child ID. No-op if the array is full (64 children max). |
| `remove(EntityId)` | Removes a child by swapping with the last entry. |

The fixed array keeps `Children` trivially copyable. For more than 64 children, group them under intermediate entities.

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
    Vec2 toWorld(Vec2 local) const;
    Vec2 toLocal(Vec2 world) const;
};
```

**Header:** `#include <drift/components/GlobalTransform.hpp>`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `position` | `Vec2` | `{0, 0}` | World-space position after applying the full parent chain. |
| `rotation` | `float` | `0.f` | World-space rotation: the parent rotations plus the local one, with the local one negated under a mirrored parent. |
| `scale` | `Vec2` | `{1, 1}` | World-space scale (product of all parent scales times local). |

| Static Method | Description |
|--------------|-------------|
| `compose(parent, local)` | A child's world transform from the parent's `GlobalTransform2D` and the child's local `Transform2D`: the local position is scaled by the parent's scale, then rotated and moved with the parent. A parent mirrored on one axis (`scale.x * scale.y < 0`) mirrors the child's rotation too, so a rig flipped with `scale.x = -1` flips as one picture. |
| `toWorld(local)`, `toLocal(world)` | A point in this transform's local space to world space, and back. |
| `from(transform)` | Creates a `GlobalTransform2D` directly from a `Transform2D` (used for root entities with no parent). |

You do not add or set `GlobalTransform2D` yourself: the `HierarchyPlugin` adds it to every entity in a tree, root included, and keeps it up to date. Sprites, text and lights in a tree render at their `GlobalTransform2D`.

## Plugin

The `HierarchyPlugin` is included in `DefaultPlugins`. It:

1. Registers the `GlobalTransform2D` component.
2. Adds a **transform propagation** system in `PostUpdate` that walks each tree from its root (an entity with `Children` and no `Parent`) and composes `GlobalTransform2D` at each level.

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

### Changing a hierarchy

`#include <drift/Hierarchy.hpp>` has functions that keep `Parent` and `Children` in agreement. Use them instead of inserting either component by hand:

| Function | Does |
|----------|------|
| `setParent(world, child, parent, keepWorld = false)` | Makes `child` a child of `parent`. With `keepWorld`, rewrites the child's `Transform2D` so it stays where it is on screen. `InvalidEntityId` as the parent detaches the child. Returns false and changes nothing when either entity is dead, the parent is the child or below it, or the parent already has `MaxChildren` children |
| `removeParent(world, child, keepWorld = false)` | Detaches `child`; same as `setParent` with `InvalidEntityId` |
| `parentOf(world, e)` | The parent, or `InvalidEntityId` for a root |
| `childrenOf(world, e)`, `descendantsOf(world, e)` | The direct children in the order they were added; everything below `e`, depth first |
| `despawnRecursive(world, e)` | Destroys `e` and everything below it, and unlinks it from its parent |
| `worldTransformOf(world, e)` | The entity's world transform composed from the current `Transform2D` values. `GlobalTransform2D` holds the same as of the last propagation; this one is never a frame behind |
| `localToWorld(world, e, p)`, `worldToLocal(world, e, p)` | A point in the entity's local space (where a child at `p` would be) to world space, and back |
| `localTransformFor(world, parent, worldT)` | The `Transform2D` an entity under `parent` needs to sit at the world transform `worldT` |

These act on the `World` at once, so call them from an exclusive system or inside `Commands::queue`, not while a query iterates.

```cpp
void grab(App& app, EntityId hand, EntityId item) {
    setParent(app.world(), item, hand, /*keepWorld=*/true);   // the item stays put, then moves with the hand
}
```

## Transform propagation

When entities form a hierarchy, `Transform2D` on child entities is interpreted as **local** (relative to the parent). The `HierarchyPlugin` computes the final world-space `GlobalTransform2D` for each entity. It writes a `GlobalTransform2D` only when the result changed, so trees that stand still (a torch with its flame, a building with its windows) cost reads, not writes.

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

When you despawn a parent entity, all of its descendants are despawned as well, and the entity is removed from its own parent's `Children`. `Commands::despawn` and the dev inspector both use `despawnRecursive`:

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
    app.config().window.title = "Hierarchy Example";
    app.config().window.width = 800;
    app.config().window.height = 600;
    app.addPlugins<DefaultPlugins>();
    app.initResource<SolarSystem>();
    app.startup<setup>();
    app.update<rotateSun>();
    return app.run();
}
```

In this example, rotating the sun causes the planets to orbit around it, and moons orbit their respective planets; all handled automatically by transform propagation.

## Notes

- The maximum number of direct children per entity is 64 (`MaxChildren`). This is a compile-time constant.
- `GlobalTransform2D` is computed in `PostUpdate`. If you read it during `Update`, you are reading the previous frame's value; `worldTransformOf` gives the current one.
- An entity that leaves a tree (no `Parent` and no `Children` any more) loses its `GlobalTransform2D` and draws from its `Transform2D` again.
- The `Name` component is a 64-character fixed buffer. It is purely for debugging and has no effect on engine behavior.
- Despawning a parent cascades to all descendants. To keep a child, detach it first with `removeParent(world, child, true)`.
