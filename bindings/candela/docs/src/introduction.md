# Drift for Candela

Write 2D games in [Candela](https://candela.lumenfx.dev) and play them in the browser. Drift is the engine: it opens the window or the canvas, draws, simulates physics, plays sound and runs the frame. You describe the game as data and functions: components hold the data, and systems are the functions that run every frame.

```rust
import "drift" { * };

@component(requires: [Velocity])
struct Player { speed: float = 300.0 }

@system(schedule: Schedule::Startup)
fn setup(cmd: Commands, input: Input) {
    input.bind_axis2d("move", Key::Left, Key::Right, Key::Up, Key::Down);
    cmd.spawn([at(480.0, 270.0), Rectangle { color: rgb(90, 170, 255), .. }, Player { .. }]);
}

@system
fn steer(row: (Player, Velocity), input: Input) {
    let (player, velocity) = row;
    velocity.linear = input.axis2d("move") * player.speed;
}
```

```sh
drift build --web
```

## Why Drift for Candela

- **No loop to write.** You write systems; Drift calls them every frame with the data their parameters ask for.
- **Visuals are data.** Put a `Sprite`, `Text` or `ParticleEmitter` on an entity and it shows up. There is no draw call to make each frame.
- **The browser is the target.** `drift build --web` turns a game folder into a page you can put on any static host. No compiler, SDK or build system is needed on your side.
- **Batteries included.** Sprites, text, input actions, Box2D physics with joints, sound and music, tilemaps, procedural generation and pathfinding, cameras, particles, lights, post effects, save data and a HUD module.
- **Checked before it runs.** The compiler checks your attributes and system parameters, so mistakes show up as errors with a line number rather than as wrong behavior.

## Where to start

1. [Install](getting-started/install.md) the `drift` command.
2. Follow the [tutorial](getting-started/tutorial.md): ten minutes from an empty folder to a game in your browser.
3. Read the [concepts](concepts/components.md) once; every game uses them.
4. Pick the [guide](guides/input.md) for what you are building, or start from an [example](examples/gallery.md) and change it.

The [reference](reference/index.md) lists every type, field, function and attribute of the `drift` module, each with an example. Use the search box (press `S`) to find a name, or Shift-click a name in any code on these pages to open its entry; hover a name to see its signature. The entries are written from the doc comments in the module's source, so your editor shows the same text when you hover a name (see [Editor support](getting-started/install.md#editor-support)).

You need to know Candela itself: structs, functions, lists and imports. The [Candela book](https://docs.lumenfx.dev/candela/) covers the language. Drift's engine is also a C++ library; its [engine book](https://fizzwizzledazzle.github.io/Drift/) covers C++ games and the engine's design.

See [Limitations](limitations.md) for what Candela games cannot do yet.
