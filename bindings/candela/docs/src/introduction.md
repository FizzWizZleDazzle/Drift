# Drift for Candela

Write 2D games for the Drift engine in [candela](https://candela.lumenfx.dev). You describe the game as data and functions: components hold the data, systems are the functions that run every frame, and Drift does the rest. It opens the window, runs the frame, draws, simulates physics, plays sound, and runs its own work on several threads beside your scripts.

```rust
import "drift";
import "drift" { Commands, Transform, Velocity, Rectangle, Input, at, vec2, rgb };

@drift::component
struct Player { speed: float = 300.0 }

@drift::system(schedule: drift::Schedule::Startup)
fn setup(cmd: Commands) {
    cmd.spawn([at(480.0, 270.0), Rectangle { size: vec2(32.0, 32.0), color: rgb(90, 170, 255), .. },
               Velocity { .. }, Player { .. }]);
}

@drift::system(writes: [Velocity])
fn steer(players: (Player, Velocity)[], input: Input) {
    for row in players {
        let (player, velocity) = row;
        velocity.linear = input.axis2d("move").mul(player.speed);
    }
}
```

```sh
drift run
```

## Why Drift for candela

- **No loop to write.** You write systems; Drift calls them every frame with the data their parameters ask for.
- **Visuals are data.** Put a `Sprite`, `Text` or `ParticleEmitter` on an entity and it shows up. There is no draw call to make each frame.
- **The engine works while your script does.** Physics, animation, tweens, particles and behaviors run inside the engine on worker threads, at the same time as your systems wherever their data does not overlap. You say what a system writes, and Drift schedules around it.
- **Change it while it runs.** Saving a script, an image or `drift.toml` updates the running game and keeps its state.
- **Checked before it runs.** The compiler checks your attributes, system parameters and declared writes, so mistakes show up as errors with a line number rather than as wrong behavior.
- **Batteries included.** Sprites and spritesheets, text, input actions with rebinding, Box2D physics, audio with buses and music, tilemaps from Tiled and LDtk, lights and shadows, post effects, scenes and prefabs, save data, and an in-game inspector.

## How to read this book

[Getting Started](getting-started/install.md) installs `drift` and builds a small game. [Concepts](concepts/components.md) explains components, systems, queries, commands, resources and messages, which every game uses. The guides cover one part of the engine each, and the reference lists every attribute, parameter, component and engine resource. [Coin Dash](examples/coin-dash.md) is a complete game to read from top to bottom.

You need to know candela itself: structs, functions, lists and imports. The [candela book](https://docs.lumenfx.dev/candela/) covers the language.
