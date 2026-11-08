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
import "drift";
import "drift" { Commands, Rectangle, at, vec2, rgb };

@drift::system(schedule: drift::Schedule::Startup)
fn setup(cmd: Commands) {
    cmd.spawn([at(480.0, 270.0), Rectangle { size: vec2(32.0, 32.0), color: rgb(90, 170, 255), .. }]);
}
```

`drift run` opens the window with a blue square in the middle. The startup system ran once and spawned an entity with two components: a `Transform` from `at(...)`, which places it, and a `Rectangle`, which draws it. Nothing else draws the square; the engine draws every entity with a `Rectangle`.

## 3. Moving

Add a component of your own to mark the ship, bind the arrow keys, and steer it:

```rust
import "drift";
import "drift" { Commands, Rectangle, Velocity, Input, Key, at, vec2, rgb };

@drift::component
struct Ship { speed: float = 300.0 }

@drift::system(schedule: drift::Schedule::Startup)
fn setup(cmd: Commands, input: Input) {
    input.bind_axis2d("move", Key::Left, Key::Right, Key::Up, Key::Down);
    cmd.spawn([at(480.0, 270.0), Rectangle { size: vec2(32.0, 32.0), color: rgb(90, 170, 255), .. },
               Velocity { .. }, Ship { .. }]);
}

@drift::system(writes: [Velocity])
fn steer(ships: (Ship, Velocity)[], input: Input) {
    for row in ships {
        let (ship, velocity) = row;
        velocity.linear = input.axis2d("move").mul(ship.speed);
    }
}
```

`steer` runs every frame. Its first parameter is a query: every entity with a `Ship` and a `Velocity`. It sets the velocity from the `move` action, and the engine moves the entity by its velocity. `writes: [Velocity]` says the system changes velocities; without it the change would be discarded.

## 4. Stars to collect

Stars are sensors: they detect the ship touching them without pushing it.

```rust
@drift::component
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
               Velocity { .. }, Ship { .. }]);
    for i in range(5) {
        spawn_star(cmd);
    }
```

Add `Circle`, `CircleCollider`, `RigidBody`, `BodyKind` and `BoxCollider` to the import list, and `import "std/random" as random;`.

## 5. Picking them up

```rust
@drift::system
fn collect(hits: drift::Overlap<Ship, Star>[], cmd: Commands) {
    for hit in hits {
        cmd.despawn(hit.b);
        spawn_star(cmd);
    }
}
```

`drift::Overlap<Ship, Star>[]` receives the sensor overlaps that started this frame between a `Ship` and a `Star`; `hit.b` is the star.

## 6. A score

A resource holds the score, and a `Text` entity on the HUD shows it:

```rust
@drift::resource
struct Score { value: int = 0 }

@drift::component
struct ScoreLabel {}
```

In `setup`, spawn the label:

```rust
    cmd.spawn([at(16.0, 12.0), Text { size: 28.0, layer: hud_layer(), .. }, ScoreLabel {}]);
```

Count in `collect`, and show it after:

```rust
@drift::system(writes: [Score])
fn collect(hits: drift::Overlap<Ship, Star>[], cmd: Commands, score: Score) {
    for hit in hits {
        score.value += 1;
        cmd.despawn(hit.b);
        spawn_star(cmd);
    }
}

@drift::system(writes: [Text], after: [collect])
fn show_score(score: Score, labels: (ScoreLabel, Text)[]) {
    for row in labels {
        row.1.value = "Stars: " + string(score.value);
    }
}
```

Add `Text` and `hud_layer` to the imports. Run it and collect stars.

## 7. Change it while it runs

Leave the game running. In `steer`, change `mul(ship.speed)` to `mul(ship.speed * 2.0)` and save: the next frame steers with the new code. Changing the `speed` default instead would not affect the ship, because its `Ship` component already holds its value. Change `clear_color` in `drift.toml` and save: the background changes. The ship, the stars and the score stay as they were. See [Reloading While the Game Runs](../concepts/hot-reload.md).

## Next

[Concepts](../concepts/components.md) explains each idea used here in full. [Coin Dash](../examples/coin-dash.md) is a larger game built the same way.
