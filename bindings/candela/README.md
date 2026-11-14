# Drift for Candela

Write 2D games for the Drift engine in [Candela](https://candela.lumenfx.dev) and play them in the browser.

A game declares its components, resources and systems with attributes, and `drift` runs them: it compiles the scripts, calls every system each frame with the data its parameters ask for, and draws and simulates the engine components underneath. `drift build --web` turns a game folder into a web page for any static host.

## Quick start

```sh
curl -fsSL https://raw.githubusercontent.com/FizzWizZleDazzle/Drift/main/install.sh | sh
drift build --web path/to/game      # writes path/to/game/build/web
cd path/to/game/build/web && python3 -m http.server 8000
```

On Windows, install with `irm https://raw.githubusercontent.com/FizzWizZleDazzle/Drift/main/install.ps1 | iex`. `drift run path/to/game` plays a game in a desktop window.

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
fn steer(row: (Ship, Velocity), input: Input) {
    let (ship, velocity) = row;
    velocity.linear = input.axis2d("move") * ship.speed;
}
```

## What it gives a game

- Sprites, shapes, text, cameras, particles, trails, lights and post effects, declared as components.
- Input actions bound in `drift.toml`, touch, gamepads, and text entry.
- Box2D physics with contact and sensor events, joints and raycasts.
- Sound, music, playlists and mixer buses.
- Tilemaps from Tiled, LDtk or code, procedural generation and pathfinding.
- Save data that lasts in the browser, data files, HTTP and WebSockets, localization.
- Modules for HUDs and menus, health and damage, player controllers, title and pause screens, state machines, inventories and dialogue.

## Examples

Every folder in `examples/` is a complete game built as a web page by the docs workflow and playable from the book's [gallery](https://fizzwizzledazzle.github.io/Drift/candela-book/examples/gallery.html): a platformer, a top-down shooter, Breakout, Snake, a roguelike, an effects showcase, menus and states, save and load, a procedurally animated creature, Flappy Bird, Coin Dash, Cave Walk, and Star Catcher, the tutorial's game.

## Benchmarks

`benches/` holds games that stress one thing each: sprites moved by per-entity systems, physics bodies, particles, a crowd of procedurally animated creatures, lookups, and HUD text. A web build makes each one a page, `bench_<name>/`, and `benches/measure.mjs` times the pages in headless Chrome, startup and frame time:

```sh
node bindings/candela/benches/measure.mjs build-web/bindings/candela bench_creatures 'bench_sprites?n=4000'
```

Each page reads its size from the address, such as `?n=4000`; the comment at the top of each `main.cdl` lists what it takes.

## Limitations

See [Limitations](https://fizzwizzledazzle.github.io/Drift/candela-book/limitations.html) in the book.

## Documentation

The book is in [`docs/`](docs/), an mdBook, published at https://fizzwizzledazzle.github.io/Drift/candela-book/. Read it locally with `mdbook serve bindings/candela/docs`. Its reference pages are written by `scripts/candela-reference.py` from the `///` doc comments in the `drift` module's source, the same text an editor shows on hover. Document a new name in its doc comment, then run the script; the script's header explains the `//! page:` lines that place names on pages.

To build `drift` from source, see [Install](docs/src/getting-started/install.md#building-drift-from-source).
