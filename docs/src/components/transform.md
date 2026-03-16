# Transform2D

`Transform2D` is the core spatial component in Drift. Every entity that has a visual presence in the world -- sprites, cameras, particle emitters, physics bodies -- needs a `Transform2D` to define where it is, how it is rotated, and how it is scaled.

## Definition

```cpp
struct Transform2D {
    Vec2 position;
    float rotation = 0.f;      // radians, clockwise
    Vec2 scale = Vec2::one();   // (1, 1) by default
};
```

**Header:** `#include <drift/Types.hpp>`

## Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `position` | `Vec2` | `{0, 0}` | World-space position in pixels |
| `rotation` | `float` | `0.f` | Rotation in radians (clockwise) |
| `scale` | `Vec2` | `{1, 1}` | Non-uniform scale multiplier |

## Basic usage

Spawn an entity at a specific position:

```cpp
void setup(Commands& cmd) {
    cmd.spawn()
        .insert<Transform2D>({.position = {100.f, 200.f}});
}
```

Set position, rotation, and scale together using designated initializers:

```cpp
cmd.spawn()
    .insert<Transform2D>({
        .position = {400.f, 300.f},
        .rotation = 1.5708f,        // 90 degrees
        .scale = {2.f, 2.f}
    });
```

## Modifying transforms in systems

Use `QueryMut` to read and write transforms each frame:

```cpp
void moveRight(Res<Time> time, QueryMut<Transform2D> movers) {
    movers.iter([&](Transform2D& t) {
        t.position.x += 100.f * time->delta;
    });
}
```

When you need the entity ID alongside the transform, use `iterWithEntity`:

```cpp
void printPositions(Query<Transform2D> transforms) {
    transforms.iterWithEntity([](EntityId id, const Transform2D& t) {
        LOG_INFO("Entity {} at ({}, {})", id, t.position.x, t.position.y);
    });
}
```

## Updating a specific entity

If you have an entity ID stored somewhere, use `Commands::entity()` to update its transform:

```cpp
void moveCamera(ResMut<GameState> game, Commands& cmd) {
    cmd.entity(game->cameraId)
        .insert<Transform2D>({.position = {newX, newY}});
}
```

## Combining with other components

`Transform2D` is almost always paired with another component. For example, a visible sprite needs both `Transform2D` and `Sprite`:

```cpp
cmd.spawn()
    .insert(
        Transform2D{.position = {320.f, 240.f}},
        Sprite{.texture = tex, .zOrder = 1.f}
    );
```

A camera needs `Transform2D` for its position:

```cpp
cmd.spawn()
    .insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});
```

## Rotation helpers

Rotation is stored in radians. Common conversions:

| Degrees | Radians |
|---------|---------|
| 45 | 0.7854 |
| 90 | 1.5708 |
| 180 | 3.1416 |
| 360 | 6.2832 |

You can compute direction vectors from rotation:

```cpp
float angle = transform.rotation;
Vec2 forward = {std::cos(angle), std::sin(angle)};
```

## Hierarchy and GlobalTransform2D

When an entity has a `Parent` component, its `Transform2D` is interpreted as local (relative to the parent). The `HierarchyPlugin` computes a `GlobalTransform2D` that represents the final world-space transform after composing the entire parent chain. See the [Hierarchy](hierarchy.md) chapter for details.

## Notes

- `Transform2D` is a plain struct with no virtual functions. It is trivially copyable and safe to use with the ECS.
- The rendering system reads `Transform2D` to position sprites. If you remove the transform, the entity will not be drawn.
- Physics bodies synchronize their position back into `Transform2D` each frame, so you should not manually set position on a dynamic physics entity unless you intend to teleport it.
