# Behaviors

Small components that make an entity move or expire on its own, so a game declares the behavior instead of writing a system for it. `BehaviorPlugin` runs them in PostUpdate as ordinary systems with declared access, so the scheduler runs them on worker threads beside other systems. It is part of `DefaultPlugins`.

| Component | Does |
|-----------|------|
| `Lifetime` | Despawns the entity when its time runs out |
| `Oscillate` | Swings the entity back and forth around where it is |
| `Follow` | Moves the entity toward another entity |
| `Orbit` | Circles the entity around another entity or a point |
| `Velocity2D` | Moves an entity without a `RigidBody2D` every frame (from `PhysicsPlugin`; see [Physics](physics.md)) |

[Tweens](../gameplay/tweening.md) and [sprite animation](sprite-animation.md) are the other declarative motions.

**Header:** `#include <drift/components/Behavior.hpp>`

## Lifetime

```cpp
struct Lifetime {
    float seconds = 1.f;
};
```

The count goes down by the frame time every frame; at zero the entity is despawned through `Commands`, children included. Use it for bullets, floating score text and effects.

```cpp
cmd.spawn().insert(Transform2D{.position = pos},
                   Text{...},
                   Velocity2D{.linear = {0.f, -60.f}},
                   Lifetime{0.6f});
```

## Oscillate

```cpp
struct Oscillate {
    Vec2 amplitude = {0.f, 8.f};   // pixels each way
    float frequency = 1.f;         // cycles per second
    float phase = 0.f;             // radians
    float elapsed = 0.f;           // seconds since it started
    Vec2 applied = {};             // offset already added to the position
};
```

Each frame the position gets the change in `amplitude * sin(2 pi frequency t + phase)`, so the swing adds to whatever else moves the entity: a coin can bob while a `Velocity2D` carries it. `elapsed` and `applied` are the component's own state.

## Follow

```cpp
struct Follow {
    EntityId target = InvalidEntityId;
    float speed = 100.f;     // pixels per second
    float distance = 0.f;    // stop this far from the target
    Vec2 offset = {};        // added to the target's position
};
```

Moves the entity straight toward the target's `Transform2D` position plus `offset` at `speed`, stopping `distance` short. Nothing happens while the target is missing. For cameras, `CameraFollow` smooths the motion instead (see [Camera Controllers](camera-controllers.md)).

## Orbit

```cpp
struct Orbit {
    EntityId center = InvalidEntityId;
    Vec2 point = {};       // the center while `center` is unset or gone
    float radius = 64.f;   // pixels
    float speed = 1.f;     // radians per second; negative turns the other way
    float angle = 0.f;     // where on the circle it is now
};
```

Puts the entity on a circle around the center entity's `Transform2D` position each frame and moves it along by `speed`. Shields, moons, circling pickups and enemies that strafe around the player are an `Orbit` each; give several the same center and spread their `angle`s to make a ring.

All four are reflected, so they save and load in [scenes](../subsystems/scenes.md).
