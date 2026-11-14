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

A direction from -1 to 1 on each axis: the 2D action `action` when it is set, plus, with
`defaults`, the arrow keys, WASD, the first gamepad's d-pad and its left stick.

Up is -1 on y. The stick counts once it is pushed past 0.2. Each axis is clamped to -1..1,
so a diagonal from the keys is (1, 1), longer than 1.

```rust
let dir = control_axis(input, "", true);
```

## approach

```rust
fn approach(from: float, to: float, step: float) -> float
```

Moves `from` toward `to` by at most `step`, without passing it.

```rust
speed = approach(speed, 300.0, 600.0 * time.dt);   // 600 pixels per second per second
```

## TopDownController

Walks in eight directions at `speed` pixels per second, reaching it in `speed /
acceleration` seconds and stopping in `speed / friction`. Diagonals are as fast as straight
lines. `facing` is the last direction moved in.

It sets the entity's Velocity, which it adds when missing. Use it on a kinematic body or an
entity with no body; on a dynamic body gravity and pushes fight it.

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

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `speed` | `float` | `220.0` | Top speed in pixels per second. |
| `acceleration` | `float` | `2000.0` | How fast it speeds up while a direction is held, in pixels per second per second. |
| `friction` | `float` | `2400.0` | How fast it slows down once nothing is held, in pixels per second per second. |
| `action` | `string` | `""` | A 2D input action to read as well, such as "move"; empty reads none. |
| `default_keys` | `bool` | `true` | Reads the arrow keys, WASD, the d-pad and the left stick; off reads only `action`. |
| `enabled` | `bool` | `true` | Off stops the controller; the Velocity keeps whatever it was. |
| `facing` | `Vec2` | `Vec2 { x: 0.0, y: 1.0 }` | The last direction moved in, length 1; starts facing down. |

```rust
cmd.spawn([at(320.0, 240.0), Sprite { image: art.hero, .. }, TopDownController { speed: 160.0, .. }]);
```

## ctl_top_down

```rust
@system
fn ctl_top_down(top_down_controller: (TopDownController, Velocity), input: Input, time: Time)
```

A system the module adds: it runs in every game that imports the module.

Moves every enabled TopDownController.

## PlatformerController

Runs and jumps on a dynamic body with fixed rotation, under the physics world's gravity.

Ground is found with three short rays below `foot` (the distance from the entity's position
to its feet), `width` apart; a surface counts as ground when it faces up. Jump is Space, Up
or W, or the first gamepad's South button, plus `jump_action`.

- Coyote time: a jump still works `coyote_time` seconds after running off a ledge.
- Jump buffering: a jump pressed `jump_buffer` seconds before landing happens on landing.
- Variable height: letting go of jump while rising cuts the speed by `jump_cut`, so a tap is
  a hop and a hold is a full jump.
- `air_jumps` extra jumps in the air: 1 is a double jump.

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

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `speed` | `float` | `240.0` | Top running speed in pixels per second. |
| `acceleration` | `float` | `2400.0` | How fast it speeds up on the ground, in pixels per second per second. |
| `air_acceleration` | `float` | `1400.0` | How fast it changes speed in the air, in pixels per second per second. |
| `friction` | `float` | `2800.0` | How fast it stops on the ground once nothing is held, in pixels per second per second. |
| `jump_speed` | `float` | `520.0` | Upward speed a jump starts with, in pixels per second. |
| `jump_cut` | `float` | `0.45` | What the upward speed is multiplied by when jump is let go while rising, 0 to 1. |
| `coyote_time` | `float` | `0.1` | Seconds after leaving the ground during which a jump still works. |
| `jump_buffer` | `float` | `0.12` | Seconds a jump press is remembered before landing. |
| `air_jumps` | `int` | `0` | Extra jumps in the air before landing again; 1 is a double jump. |
| `max_fall` | `float` | `900.0` | The fastest it falls, in pixels per second. |
| `foot` | `float` | `16.0` | Distance from the entity's position down to its feet, in pixels. |
| `width` | `float` | `24.0` | Distance between the outer ground rays, in pixels; about the collider's width. |
| `jump_action` | `string` | `""` | An input action that jumps as well; empty reads none. |
| `move_action` | `string` | `""` | A 2D input action whose x runs as well; empty reads none. |
| `default_keys` | `bool` | `true` | Reads the arrow keys, WASD, the d-pad, the left stick and the jump keys; off reads only the actions. |
| `enabled` | `bool` | `true` | Off stops the controller; the Velocity keeps whatever it was. |
| `grounded` | `bool` | `false` | Whether it stood on the ground this frame. |
| `since_ground` | `float` | `1.0` | Seconds since it last stood on the ground. |
| `since_jump_press` | `float` | `1.0` | Seconds since jump was last pressed. |
| `jumps_left` | `int` | `0` | Air jumps left before landing. |
| `jumping` | `bool` | `false` | Set from a jump until it lands or jump is let go while rising. |
| `facing` | `float` | `1.0` | The way it last ran: 1.0 right, -1.0 left. |

```rust
cmd.spawn([at(100.0, 300.0), Rectangle { size: vec2(24.0, 32.0), .. },
           RigidBody { fixed_rotation: true, .. }, BoxCollider { half_size: vec2(12.0, 16.0), .. },
           PlatformerController { air_jumps: 1, .. }]);
```

## Jumped

Sent when a PlatformerController jumps.

```rust
@message
struct Jumped {
    entity: Entity = Entity { .. },
    air: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `entity` | `Entity` | `Entity { .. }` | The entity that jumped. |
| `air` | `bool` | `false` | Whether it was an air jump. |

```rust
@system
fn double_jumped(j: Jumped) {
    if j.air {
        log("double jump");
    }
}
```

## Landed

Sent when a PlatformerController lands on the ground.

```rust
@message
struct Landed {
    entity: Entity = Entity { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `entity` | `Entity` | `Entity { .. }` | The entity that landed. |

## jump_pressed

```rust
fn jump_pressed(input: Input, c: PlatformerController) -> bool
```

Whether jump was pressed this frame: `c.jump_action`, or with the default keys Space, Up, W
or the first gamepad's South button.

## jump_held

```rust
fn jump_held(input: Input, c: PlatformerController) -> bool
```

Whether jump is held down: `c.jump_action`, or with the default keys Space, Up, W or the
first gamepad's South button.

## on_ground

```rust
fn on_ground(e: Entity, t: Transform, c: PlatformerController, physics: Physics) -> bool
```

Whether the controller stands on something: one of three rays from just above its feet,
6 pixels down, hits another entity's collider whose surface faces up.

## ctl_platformer

```rust
@system
fn ctl_platformer(platformer_controller: (Entity, PlatformerController, Velocity, Transform), input: Input, time: Time, physics: Physics, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Runs, jumps and lands every enabled PlatformerController.

## ShipController

An asteroids-style ship: Left and Right turn at `turn_speed` radians per second, Up thrusts
along the facing direction, and the ship drifts, slowed by `drag` per second, up to
`max_speed`.

The ship faces +X at rotation 0. `thrusting` is true while thrust is held, for a flame
sprite. It turns the Transform and sets the Velocity.

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

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `thrust` | `float` | `420.0` | Speed gained per second of full thrust, in pixels per second per second. |
| `turn_speed` | `float` | `4.0` | Turning speed in radians per second. |
| `max_speed` | `float` | `420.0` | The fastest it moves, in pixels per second. |
| `drag` | `float` | `0.6` | Share of the speed lost per second; 0 drifts forever. |
| `reverse` | `float` | `0.0` | Backward thrust while Down is held, in pixels per second per second; 0 gives Down no effect. |
| `action` | `string` | `""` | A 2D input action to read as well: x turns, negative y thrusts; empty reads none. |
| `default_keys` | `bool` | `true` | Reads the arrow keys, WASD, the d-pad and the left stick; off reads only `action`. |
| `enabled` | `bool` | `true` | Off stops the controller; the Velocity keeps whatever it was. |
| `thrusting` | `bool` | `false` | Whether thrust is held this frame. |

```rust
cmd.spawn([at(320.0, 240.0), Sprite { image: art.ship, .. }, ShipController { .. }, WrapAround { .. }]);
```

## ctl_ship

```rust
@system
fn ctl_ship(ship_controller: (ShipController, Velocity, Transform), input: Input, time: Time)
```

A system the module adds: it runs in every game that imports the module.

Turns and thrusts every enabled ShipController.
