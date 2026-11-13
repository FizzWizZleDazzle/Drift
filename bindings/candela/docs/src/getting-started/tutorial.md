# Tutorial: Your First Game

Build Star Catcher, a small game in which a ship collects stars against the clock, and play it in your browser. Each step adds a few lines and works on its own. It takes about ten minutes. You need `drift` [installed](install.md) and Python 3 for a local web server.

[Play the finished game](https://fizzwizzledazzle.github.io/Drift/candela-book/play/star_catcher/).

## 1. A game folder

A game is a folder with a `drift.toml` and a `main.cdl`. Make one:

```sh
mkdir star_catcher
cd star_catcher
```

Write `drift.toml`, the game's settings:

```toml
[project]
name = "Star Catcher"
entry = "main.cdl"

[window]
width = 960
height = 540

[render]
clear_color = [0.05, 0.05, 0.1]

[physics]
gravity = [0, 0]
```

The window is 960 by 540 pixels. Physics gravity is off because the game is seen from above.

## 2. Something on screen

Write `main.cdl`:

```rust
import "drift" { * };

@system(schedule: Schedule::Startup)
fn setup(cmd: Commands) {
    cmd.spawn([at(480.0, 270.0), Rectangle { size: vec2(32.0, 32.0), color: rgb(90, 170, 255), .. }]);
}
```

`import "drift" { * };` brings in the engine. `setup` is a system: `@system` marks it, and `schedule: Schedule::Startup` runs it once, before the first frame. It takes `Commands` and spawns an entity with two components: a `Transform` made by `at(...)`, which places it in the middle of the screen, and a `Rectangle`, which draws it. The `..` fills every field you do not name with its default.

## 3. Play it in the browser

Build the page and serve it:

```sh
drift build --web
cd build/web && python3 -m http.server 8000
```

Open <http://localhost:8000>: a blue square in the middle of a dark screen. Leave the server running in its own terminal. From now on, after each step run `drift build --web` in the game folder and reload the page.

## 4. Fly

Add a component of your own for the ship, bind the arrow keys to a `move` action, and steer with it:

```rust
import "drift" { * };

@component(requires: [Velocity])
struct Ship { speed: float = 320.0 }

@system(schedule: Schedule::Startup)
fn setup(cmd: Commands, input: Input) {
    input.bind_axis2d("move", Key::Left, Key::Right, Key::Up, Key::Down);
    input.bind_axis2d("move", Key::A, Key::D, Key::W, Key::S);
    cmd.spawn([
        at(480.0, 270.0),
        Rectangle { size: vec2(32.0, 32.0), color: rgb(90, 170, 255), corner_radius: 6.0, .. },
        Ship { .. },
    ]);
}

@system
fn steer(row: (Ship, Velocity), input: Input) {
    let (ship, velocity) = row;
    velocity.linear = input.axis2d("move") * ship.speed;
}
```

- `@component` makes `Ship` a component. `requires: [Velocity]` gives every ship a `Velocity` too, so the bundle does not list one.
- `steer` has no schedule, so it runs every frame. Its parameter `row: (Ship, Velocity)` is a row, a tuple of components, so it runs once for each entity that has both, and `let (ship, velocity) = row;` names them. Changing `velocity` changes the entity.
- `axis2d("move")` is a direction from the keys, and the engine moves an entity by its `Velocity` in pixels per second. The system only sets it.

Click the page so it has the keyboard, and fly around.

## 5. Stars

Stars are sensors: colliders that notice the ship touching them without pushing it. Add a tag component, a resource that holds a random number generator, and a function that spawns a star:

```rust
@component struct Star {}

@resource
struct Game { score: int = 0, rng: Rng = rng(1) }

fn spawn_star(cmd: Commands, game: Game) {
    cmd.spawn([
        at(game.rng.range_float(40.0, 920.0), game.rng.range_float(60.0, 500.0)),
        Circle { radius: 10.0, color: rgb(255, 220, 80), .. },
        CircleCollider { radius: 10.0, sensor: true, .. },
        tween().scale_to(vec2(1.3, 1.3), 0.5, Ease::InOutSine).yoyo(-1),
        Star {},
    ]);
}
```

A `@resource` is one value for the whole game. Its generator remembers where it is, so each star lands somewhere new. The `tween()` makes the star pulse forever; the engine runs it.

To touch a sensor, the ship needs a physics body and a collider of its own. Add them to its bundle in `setup`, take the `Game` resource, and spawn five stars:

```rust
@system(schedule: Schedule::Startup)
fn setup(cmd: Commands, input: Input, game: Game) {
    input.bind_axis2d("move", Key::Left, Key::Right, Key::Up, Key::Down);
    input.bind_axis2d("move", Key::A, Key::D, Key::W, Key::S);
    cmd.spawn([
        at(480.0, 270.0),
        Rectangle { size: vec2(32.0, 32.0), color: rgb(90, 170, 255), corner_radius: 6.0, .. },
        RigidBody { fixed_rotation: true, .. },
        BoxCollider { half_size: vec2(16.0, 16.0), .. },
        Ship { .. },
    ]);
    for i in range(5) {
        spawn_star(cmd, game);
    }
}
```

## 6. Catch them

```rust
@system
fn collect(hit: Overlap<Ship, Star>, cmd: Commands, game: Game) {
    game.score += 1;
    cmd.despawn(hit.b);
    spawn_star(cmd, game);
}
```

A system that takes one `Overlap<Ship, Star>` runs once for each time a ship enters a star's sensor. `hit.a` is the ship and `hit.b` the star. Changes to `game` are kept when the system returns.

## 7. A score on screen

Add a label entity on the HUD layer, and a system that writes the score into it:

```rust
@component struct ScoreLabel {}
```

In `setup`:

```rust
    cmd.spawn([at(16.0, 12.0), Text { size: 28.0, layer: hud_layer(), .. }, ScoreLabel {}]);
```

And a system:

```rust
@system
fn show_score(label: (ScoreLabel, Text), game: Game) {
    let (_, text) = label;
    text.value = "Stars " + string(game.score);
}
```

`hud_layer()` draws in screen pixels, on top of everything. The row `(ScoreLabel, Text)` requires the tag, and `_` in `let (_, text) = label;` skips it, so `show_score` runs for the label and nothing else.

## 8. Against the clock

A round lasts 30 seconds, then waits for Space. Phases of a game are a `@state` enum:

```rust
@state
enum Round { Playing, Over }
```

Give `Game` a timer, and bind Space to a `restart` action in `drift.toml`:

```rust
@resource
struct Game { score: int = 0, left: Timer = timer(30.0), rng: Rng = rng(1) }
```

```toml
[input]
restart = ["key:Space", "button:South"]
```

Move the star spawning out of `setup` into a system that runs each time a round starts, and count down while it plays:

```rust
@system(on_enter: Round::Playing)
fn new_round(cmd: Commands, game: Game, stars: (Entity, Star)[]) {
    for (e, _) in stars {
        cmd.despawn(e);
    }
    game.score = 0;
    game.left = timer(30.0);
    for i in range(5) {
        spawn_star(cmd, game);
    }
}

@system(in_state: [Round::Playing])
fn countdown(game: Game, time: Time, round: State<Round>) {
    if game.left.tick(time.dt) > 0 {
        round.set(Round::Over);
    }
}

fn restart_pressed(input: Input) -> bool => input.action_pressed("restart");

@system(in_state: [Round::Over], run_if: [restart_pressed])
fn restart(round: State<Round>) {
    round.set(Round::Playing);
}
```

- `on_enter: Round::Playing` runs `new_round` whenever the state becomes `Playing`, the first frame included.
- `(Entity, Star)[]` is a query: every entity with a `Star`, with its id, so the old stars can go.
- `in_state:` runs a system only in those states. Add `in_state: [Round::Playing]` to `steer` and `collect` too, so the ship stops catching stars when time is up.
- `run_if:` names functions that must return `true` for the system to run that frame.

Show the time in `show_score`:

```rust
@system
fn show_score(label: (ScoreLabel, Text), game: Game, round: State<Round>) {
    let (_, text) = label;
    if round.now == Round::Playing {
        text.value = "Stars " + string(game.score) + "    Time " + string(int(game.left.remaining()) + 1);
    } else {
        text.value = "Time's up: " + string(game.score) + " stars. Space to play again.";
    }
}
```

## 9. Sparkle

A burst of particles where a star was caught takes one call. Change `collect` to look up the star's position and burst there:

```rust
@system(in_state: [Round::Playing])
fn collect(hit: Overlap<Ship, Star>, places: Lookup<Transform>, cmd: Commands, game: Game, effects: Effects) {
    game.score += 1;
    if places.get(hit.b) is Some(t) {
        effects.burst(t.position, ParticleEmitter {
            speed_min: 60.0, speed_max: 160.0, lifetime_min: 0.2, lifetime_max: 0.5,
            color_start: rgb(255, 230, 120), color_end: rgba(255, 140, 40, 0), blend: Blend::Additive, ..
        }, 20);
    }
    cmd.despawn(hit.b);
    spawn_star(cmd, game);
}
```

A `Lookup<Transform>` finds any entity's `Transform` by its id.

## The whole game

The finished game also keeps the ship on the screen, stops it when time runs out, seeds the stars from the game's seed, and plays a sound made from samples. It is `bindings/candela/examples/star_catcher` in the Drift repository:

```rust
{{#include ../../../examples/star_catcher/main.cdl}}
```

## Next

- [Publishing to the Web](../guides/publishing.md) puts the page online.
- The [concepts](../concepts/components.md) explain components, systems, queries, resources, states and events in full.
- The [examples](../examples/gallery.md) are complete games to read and change.
