<!-- Written by scripts/candela-reference.py from the doc comments in
     bindings/candela/drift/src. Edit those, not this page. -->

# Controllers: drift/control

Movement controllers for the player's entity: top-down 8-way movement, a platformer with
coyote time, jump buffering and variable jump height, and an asteroids-style ship. Each
reads the arrow keys, WASD and the first gamepad's left stick by default, plus the input
actions it names, and drives the entity's `Velocity`.

```rust
import "drift" { * };
import "drift/control" { * };
```

See [Building Blocks](../guides/building-blocks.md).

## control_axis

```rust
fn control_axis(input: Input, action: string, defaults: bool) -> Vec2
```

-1 to 1 on each axis from the arrow keys, WASD and the left stick, plus the 2D action
`action` when it is set.

## approach

```rust
fn approach(from: float, to: float, step: float) -> float
```

Moves `from` toward `to` by at most `step`.

## TopDownController

Walks in eight directions at `speed` pixels per second, reaching it in `speed /
acceleration` seconds and stopping in `speed / friction`. Diagonals are as fast as straight
lines. `facing` is the last direction moved in.

```rust
@component(requires: [Velocity])
struct TopDownController {
    speed: float = 220.0,
    acceleration: float = 2000.0,
    friction: float = 2400.0,
    action: string = "",
    default_keys: bool = true,
    enabled: bool = true,
    facing: Vec2 = Vec2 { x: 0.0, y: 1.0 },
}
```

## ctl_top_down

```rust
@system
fn ctl_top_down(top_down_controller: (TopDownController, Velocity), input: Input, time: Time)
```

A system the module adds: it runs in every game that imports the module.

## PlatformerController

- Coyote time: a jump still works `coyote_time` seconds after running off a ledge. - Jump
  buffering: a jump pressed `jump_buffer` seconds before landing happens on landing. -
  Variable height: letting go of jump while rising cuts the speed by `jump_cut`, so a tap is
  a hop and a hold is a full jump. - `air_jumps` extra jumps in the air: 1 is a double jump.

```rust
@component(requires: [Velocity])
struct PlatformerController {
    speed: float = 240.0,
    acceleration: float = 2400.0,
    air_acceleration: float = 1400.0,
    friction: float = 2800.0,
    jump_speed: float = 520.0,
    jump_cut: float = 0.45,
    coyote_time: float = 0.1,
    jump_buffer: float = 0.12,
    air_jumps: int = 0,
    max_fall: float = 900.0,
    foot: float = 16.0,
    width: float = 24.0,
    jump_action: string = "",
    move_action: string = "",
    default_keys: bool = true,
    enabled: bool = true,
    grounded: bool = false,
    since_ground: float = 1.0,
    since_jump_press: float = 1.0,
    jumps_left: int = 0,
    jumping: bool = false,
    facing: float = 1.0,
}
```

## Jumped

Sent when a PlatformerController jumps and when it lands.

```rust
@message
struct Jumped {
    entity: Entity = Entity { .. },
    air: bool = false,
}
```

## Landed

```rust
@message
struct Landed {
    entity: Entity = Entity { .. },
}
```

## jump_pressed

```rust
fn jump_pressed(input: Input, c: PlatformerController) -> bool
```

## jump_held

```rust
fn jump_held(input: Input, c: PlatformerController) -> bool
```

## on_ground

```rust
fn on_ground(e: Entity, t: Transform, c: PlatformerController, physics: Physics) -> bool
```

## ctl_platformer

```rust
@system
fn ctl_platformer(platformer_controller: (Entity, PlatformerController, Velocity, Transform), input: Input, time: Time, physics: Physics, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## ShipController

Asteroids-style: Left and Right turn at `turn_speed` radians per second, Up thrusts along
the facing direction, and the ship drifts, slowed by `drag` per second, up to `max_speed`.
The ship faces +X at rotation 0. `thrusting` is true while thrust is held, for a flame
sprite.

```rust
@component(requires: [Velocity])
struct ShipController {
    thrust: float = 420.0,
    turn_speed: float = 4.0,
    max_speed: float = 420.0,
    drag: float = 0.6,
    reverse: float = 0.0,
    action: string = "",
    default_keys: bool = true,
    enabled: bool = true,
    thrusting: bool = false,
}
```

## ctl_ship

```rust
@system
fn ctl_ship(ship_controller: (ShipController, Velocity, Transform), input: Input, time: Time)
```

A system the module adds: it runs in every game that imports the module.
