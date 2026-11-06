# Candela

Drift games can be written in [candela](https://candela.lumenfx.dev), a small statically typed scripting language, instead of C++. The `drift` candela package wraps the engine: a game is a candela program run with `candela run game.cdl`, and Drift does the windowing, rendering, physics, text, audio and the rest underneath.

Use it when you want a quick edit-and-run cycle with no C++ build, or when a game's logic reads more naturally as a short script. The engine is the same one C++ games use, so features and rendering match.

## How a game looks

A game is entities with components, and systems that work on them, in the style of Bevy. You write a `setup` system that spawns entities, `update` systems that run every frame, and handlers for collisions. The package owns the frame loop.

```rust
import "../src/main.cdl" { Key, Rectangle, Text, Transform, World, app, at, config, hud_layer, rgb, vec2 };

struct Player {
    speed: float = 200.0,
}

fn main() {
    app(config("Hello", 640, 360)).startup(setup).update(steer).run();
}

fn setup(world: World) {
    world.spawn()
        .insert(at(320.0, 180.0))
        .insert(Rectangle { size: vec2(32.0, 32.0), color: rgb(90, 170, 255), .. })
        .insert(Player { .. });
    world.spawn()
        .insert(at(10.0, 10.0))
        .insert(Text { value: "Arrow keys move", size: 20.0, layer: hud_layer(), .. });
}

fn steer(world: World, dt: float) {
    let input = world.input();
    for row in world.query2<Player, Transform>() {
        if input.key_down(Key::Left) { row.b.position.x -= row.a.speed * dt; }
        if input.key_down(Key::Right) { row.b.position.x += row.a.speed * dt; }
    }
}
```

There is no draw call: the rectangle and the text are components, and the engine draws them every frame. Immediate drawing (`world.draw_line`, ...) exists for debugging.

- **Components** are plain candela structs. Your own (`Player` above) live on the candela side. The engine's (`Transform`, `Sprite`, `Rectangle`, `Circle`, `Text`, `Camera`, `RigidBody`, colliders, `Velocity`, `ParticleEmitter`, lights, `Tilemap`, `Animator`, `Tween`, `Lifetime`, `Oscillate`, `Follow`, ...) are mirrored to the native engine, which draws and simulates them.
- **Systems** are functions: `startup(fn(World))` runs once, `update(fn(World, float))` every frame with the frame time. Several update systems run in the order they were added.
- **Queries** return rows: `world.query<A>()`, `world.query2<A, B>()`, `world.query3<A, B, C>()`. A row holds the `entity` and its components as `a`, `b`, `c`; they are the stored values, so assigning a field changes the component.
- **Collisions** call a handler: `on_collision<Player, Hazard>(f)` runs `f(world, player, hazard)` when an entity with `Player` starts touching one with `Hazard`, with the two in the declared order. `on_sensor<A, B>` does the same for sensor colliders.
- **Resources** hold game-wide state: `world.insert_resource(Game { .. })`, `world.resource<Game>()`.

## Declarative behavior

Engine components that move or change things run natively, so a game declares them instead of writing a system:

- `Velocity` moves an entity every frame (or drives a physics body).
- `Lifetime` despawns it after a time.
- `Oscillate` swings it back and forth on top of other motion.
- `Follow` moves it toward another entity.
- `Tween` animates position, scale, rotation, color or camera zoom with easing.
- `Animator` plays spritesheet clips on its `Sprite`.

Sprites moved by `Velocity` cost the script nothing per frame; the same sprites moved by a candela system cost a query and a write per sprite every frame. Prefer the components for anything that only needs to move, fade or expire.

## The frame

Each frame the package:

1. waits for the engine simulation it started last frame,
2. sends the components your systems changed to the engine and creates and despawns entities,
3. renders and presents the frame and polls input,
4. copies the collision events and the values the engine changes (positions after physics, animation frames, tweened colors), then starts the next simulation step (physics, behaviors, animation, particles, tweens, transform propagation) on a background thread,
5. calls the collision handlers and your update systems.

Steps 4 and 5 overlap: your systems run while the engine simulates. Two things follow, both by one frame:

- Systems see the world as it was when the simulation step started. What they write lands after that step, so a velocity set from input moves a physics body from the next step on.
- A system's change to a `Transform` is added to what the simulation did to the same entity meanwhile, so a script nudge and a `Velocity` both apply. Setting a position on something the engine also moves gives that position plus one step of the engine's motion.

Set `Config.pipelined = false` to run the simulation before your systems instead; systems then see the result of the current step, at the cost of the overlap. `world.stats()` reports how long the systems and the simulation took and how much of that ran at the same time.

Calls that need the live engine world (physics queries such as `raycast` and `apply_impulse`, audio, particle bursts, spawning a scene) wait for the simulation step to finish first, so they are always correct but end the overlap for that frame. Textures, fonts, input, effects and save data need no wait.

## Building the package

Build the native library with the package's script, which configures Drift with `DRIFT_BUILD_CANDELA=ON` and writes the library into `bindings/candela/dist/`:

```sh
cd bindings/candela
./build-native.sh
```

Configuring the whole repository with `-DDRIFT_BUILD_CANDELA=ON` does the same and also registers the candela tests with `ctest`. You need candela 0.1.6 or newer (`candela --version`) to run games and tests, but not to build the library.

## Quick start

Put a game in a folder inside the package, for example `bindings/candela/mygame/game.cdl`, starting from the example above, then run it from that folder:

```sh
cd bindings/candela/mygame
candela run game.cdl
```

List the names you use in the import's braces; types bring their methods along, so `World` needs no other import for `world.spawn()`. `import "../src/main.cdl" as drift;` works too, with every name written `drift::app`, `drift::Key::Left`. Paths for assets (`Sprite { image: "player.png", .. }`) are relative to the directory you run from.

`world.quit()` ends the game after the current frame; closing the window does the same.

## Conventions

- **Coordinates**: world units are pixels, +Y points down. With no `Camera` entity, world position (0, 0) is the top-left of the screen. Text and sprites on `hud_layer()` are in screen coordinates.
- **Building components**: write the fields you need and end with `..` to take the rest from the defaults: `RigidBody { kind: BodyKind::Static, .. }`. `at(x, y)` is a `Transform` at a position.
- **Floats and ints**: candela never converts between them, so write `vec2(1.0, 2.0)`, not `vec2(1, 2)`. Colors take channels from 0.0 to 1.0, or use `rgb(255, 128, 0)` and `rgba(...)`.
- **Errors**: a failing call raises an error kind you can catch, such as `drift_load_failed` for a file that will not load or `drift_missing_component` for `world.get<T>(e)` on an entity without a `T`. `last_error()` returns the message that goes with a native failure:

```rust
try {
    let tex = world.load_texture("player.png");
} catch "drift_load_failed" {
    print("could not load: " + last_error());
}
```

- **Spawning and despawning**: `world.spawn()` and `world.despawn(e)` take effect for queries at once and in the engine at the end of the frame. Queries return lists, so spawning or despawning while looping over one is safe. An entity the engine despawns (a `Lifetime` ran out, its parent went) disappears from queries at the start of the next frame.
- **One shape per entity**: an entity draws one of `Sprite`, `Rectangle` or `Circle`, plus any `Text`.

## Limitations

The package is desktop only: candela cannot load native libraries when it runs as WebAssembly, so a candela game does not build for the browser. Write the game in C++ to ship it on the web.

candela runs a script on one thread, so `world.par_each<T>(f)` and `par_each2` call `f` for one entity after another, the same as a loop over a query. They exist so systems written that way can run in parallel once candela can. The parallelism a candela game gets today is the engine's: the simulation beside the systems, and the native behavior components.

Components of yours stay on the candela side: the engine cannot see them, and scene files do not save them. Entities a scene spawns come back with their `Transform`, `Velocity`, `Sprite`, `Rectangle` or `Circle`, `Text`, `Camera`, `RigidBody`, colliders, lights, `Lifetime` and `Name`; other engine components on them stay engine-side. Tiled object spawners and C++ systems are not reachable from candela.

One app runs at a time, and its `Config` (including `lighting` and `pipelined`) is fixed when it starts.

## Next

- [API Reference](api.md) lists the API by area.
- [Coin Dash](../examples/coin-dash.md) walks through the example game.
