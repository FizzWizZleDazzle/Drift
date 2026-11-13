# Drift for Candela

Write 2D games for the Drift engine in [candela](https://candela.lumenfx.dev), as components and systems.

A game declares its components, resources and systems with attributes, and `drift` runs them: it compiles the scripts, calls every system each frame with the data its parameters ask for, and draws and simulates the engine components (sprites, text, physics bodies, particles, tweens) on its own threads. Games run on the desktop and, built with Emscripten, in the browser.

## Quick start

```sh
curl -fsSL https://raw.githubusercontent.com/FizzWizZleDazzle/Drift/main/install.sh | sh
drift run path/to/game              # play it
drift build --web path/to/game      # write it as a web page, in path/to/game/build/web
```

On Windows, install with `irm https://raw.githubusercontent.com/FizzWizZleDazzle/Drift/main/install.ps1 | iex`.

To build `drift` from source instead:

```sh
cmake -B build -DDRIFT_BUILD_CANDELA=ON -DDRIFT_CANDELA_SOURCE=<candela checkout>
cmake --build build --target drift_run
build/bindings/candela/drift run bindings/candela/examples/flappy_bird
```

`DRIFT_CANDELA_SOURCE` points at a candela checkout with the changes of lumen-fx/candela#246 until they are released. Building needs a Rust toolchain.

```rust
import "drift" { * };

@component(requires: [Velocity])
struct Ship { speed: float = 300.0 }

@system(schedule: Schedule::Startup)
fn setup(cmd: Commands, input: Input) {
    input.bind_axis2d("move", Key::Left, Key::Right, Key::Up, Key::Down);
    cmd.spawn([at(480.0, 270.0), Rectangle { size: vec2(32.0, 32.0), .. }, Ship { .. }]);
}

@system
fn steer(ship: Ship, velocity: Velocity, input: Input) {
    velocity.linear = input.axis2d("move") * ship.speed;
}
```

## Examples

- `examples/flappy_bird`: states, per-entity systems, sensor overlaps, settings from `drift.toml`.
- `examples/coin_dash`: input actions, physics contacts, particles, tweens, save data, sound made from samples.
- `examples/cave_walk`: generated caves and dungeons in a tilemap made in code, seeded random numbers, Poisson-disk spawns from a weighted table, flow-field chasers.

## Limitations

Scripts are not reloaded while the game runs yet, and the `drift check` and `drift new` commands and `drift build` without `--web` are not built. Several engine resources the book describes (Audio, Physics, Draw, Camera2D) are not in the `drift` module yet, and maps do not list their objects (`Map.objects`, `object_property`).

## Documentation

The book is in [`docs/`](docs/), an mdBook: `mdbook serve bindings/candela/docs`.
