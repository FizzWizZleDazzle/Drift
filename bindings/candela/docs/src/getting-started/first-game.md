# Your First Game

Build a small game step by step: a ship that flies around and collects stars. Each step runs.

## 1. A project

```sh
drift new stars
cd stars
```

`drift new` writes three things: `drift.toml` with the game's settings, `main.cdl` with the code, and an empty `assets/` folder. Replace `main.cdl` with the steps below.

Set the window in `drift.toml`:

```toml
[project]
name = "Stars"
entry = "main.cdl"

[window]
width = 960
height = 540

[render]
clear_color = [0.05, 0.05, 0.1]
```

## 2. Something on screen

```rust
import "drift" { * };

@system(schedule: Schedule::Startup)
fn setup(cmd: Commands) {
    cmd.spawn([at(480.0, 270.0), Rectangle { size: vec2(32.0, 32.0), color: rgb(90, 170, 255), .. }]);
}
```

`drift run` opens the window with a blue square in the middle. The startup system ran once and spawned an entity with two components: a `Transform` from `at(...)`, which places it, and a `Rectangle`, which draws it. Nothing else draws the square; the engine draws every entity with a `Rectangle`.

## 3. Moving

Add a component of your own to mark the ship, bind the arrow keys to a `move` axis, and steer it. Single-button actions can be bound in `drift.toml` instead (see [Input](../guides/input.md#actions)); an axis is bound in code:

```rust
import "drift" { * };

@component(requires: [Velocity])
struct Ship { speed: float = 300.0 }

@system(schedule: Schedule::Startup)
fn setup(cmd: Commands, input: Input) {
    input.bind_axis2d("move", Key::Left, Key::Right, Key::Up, Key::Down);
    cmd.spawn([at(480.0, 270.0), Rectangle { size: vec2(32.0, 32.0), color: rgb(90, 170, 255), .. },
               Ship { .. }]);
}

@system
fn steer(ship: Ship, velocity: Velocity, input: Input) {
    velocity.linear = input.axis2d("move") * ship.speed;
}
```

`steer` runs every frame, once for each entity with a `Ship` and a `Velocity`. It sets the velocity from the `move` action, and the engine moves the entity by its velocity. `requires: [Velocity]` gives every ship a `Velocity`, so the bundle does not list one.

## 4. Stars to collect

Stars are sensors: they detect the ship touching them without pushing it.

```rust
import "std/random" as random;

@component
struct Star {}

fn spawn_star(cmd: Commands) {
    cmd.spawn([
        at(random::float_range(40.0, 920.0), random::float_range(40.0, 500.0)),
        Circle { radius: 10.0, color: rgb(255, 220, 80), .. },
        CircleCollider { radius: 10.0, sensor: true, .. },
        Star {},
    ]);
}
```

The ship needs a body and a collider to touch anything. Add them to its bundle in `setup`, and spawn the first stars:

```rust
    cmd.spawn([at(480.0, 270.0), Rectangle { size: vec2(32.0, 32.0), color: rgb(90, 170, 255), .. },
               RigidBody { kind: BodyKind::Kinematic, .. }, BoxCollider { half_size: vec2(16.0, 16.0), .. },
               Ship { .. }]);
    for i in range(5) {
        spawn_star(cmd);
    }
```

## 5. Picking them up

```rust
@system
fn collect(hit: Overlap<Ship, Star>, cmd: Commands) {
    cmd.despawn(hit.b);
    spawn_star(cmd);
}
```

A system that takes one `Overlap<Ship, Star>` runs once for each sensor overlap that started this frame between a `Ship` and a `Star`; `hit.b` is the star.

## 6. A score

A resource holds the score, and a `Text` entity on the HUD shows it:

```rust
@resource
struct Score { value: int = 0 }

@component
struct ScoreLabel {}
```

In `setup`, spawn the label:

```rust
    cmd.spawn([at(16.0, 12.0), Text { size: 28.0, layer: hud_layer(), .. }, ScoreLabel {}]);
```

Count in `collect`, and show it after:

```rust
@system
fn collect(hit: Overlap<Ship, Star>, cmd: Commands, score: Score) {
    score.value += 1;
    cmd.despawn(hit.b);
    spawn_star(cmd);
}

@system(after: [collect])
fn show_score(_: ScoreLabel, text: Text, score: Score) {
    text.value = "Stars: " + string(score.value);
}
```

Run it and collect stars.

## 7. Change it while it runs

Leave the game running. In `steer`, change `* ship.speed` to `* ship.speed * 2.0` and save: the next frame steers with the new code. Changing the `speed` default instead would not affect the ship, because its `Ship` component already holds its value. Change `clear_color` in `drift.toml` and save: the background changes. The ship, the stars and the score stay as they were. See [Reloading While the Game Runs](../concepts/hot-reload.md).

## Next

[Concepts](../concepts/components.md) explains each idea used here in full. [Flappy Bird](../examples/flappy-bird.md) is a complete game built the same way.
