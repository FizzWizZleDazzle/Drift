# drift for candela

Write 2D games for the Drift engine in [candela](https://candela.lumenfx.dev).

A game is entities with components and systems that work on them, in the
style of Bevy. You write a setup system, update systems and collision
handlers; the package runs the frame loop, and the engine draws and
simulates the components. Drift supplies the window, renderer, physics,
text, audio, tilemaps and the rest as one native library, so there is
nothing else to install.

## Quick start

Build the native library once, from this directory:

```sh
./build-native.sh            # writes dist/libdrift_candela.so, .dylib or .dll
```

Then write a game in a folder inside the package, for example `mygame/game.cdl`, and run it from there:

```rust
import "../src/main.cdl" { Key, Rectangle, Transform, World, app, at, config, rgb, vec2 };

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
}

fn steer(world: World, dt: float) {
    let input = world.input();
    for row in world.query2<Player, Transform>() {
        if input.key_down(Key::Left) { row.b.position.x -= row.a.speed * dt; }
        if input.key_down(Key::Right) { row.b.position.x += row.a.speed * dt; }
    }
}
```

```sh
candela run game.cdl
```

Components are plain structs. Yours live in candela; the engine's
(`Transform`, `Sprite`, `Rectangle`, `Text`, `RigidBody`, ...) are mirrored to
the engine, which draws them every frame, so there is no draw call. Requires
candela 0.1.6 or newer.

## What it covers

- Entities, components, queries over one to three components, resources,
  and collision and sensor handlers by component type.
- Sprites, rectangles, circles, text, cameras, tilemaps, particle emitters,
  lights and sounds as components.
- Physics bodies and colliders, raycasts, impulses and gravity.
- Behavior that runs in the engine with no script: velocity, lifetime,
  oscillation, following another entity, tweens and sprite animation.
- Keyboard, mouse and gamepad state, and named input actions and axes with
  rebinding and JSON save.
- Textures from files or pixel lists, spritesheets, Aseprite files, fonts,
  sounds, mixer buses and music.
- Post effects and screen fades, timers, save data, Tiled and LDtk maps,
  JSON scenes and prefabs.

The engine simulates each frame on a background thread while your systems
run; the Drift book explains what that means for a game.

The [Coin Dash example](examples/coin_dash/main.cdl) uses most of these in a
small complete game.

## Limitations

Desktop only: candela cannot load native libraries when it runs as
WebAssembly, so a candela game does not build for the browser. candela runs a
script on one thread, so `world.par_each` runs one entity after another. One
app runs at a time.

## Tests

```sh
SDL_VIDEO_DRIVER=offscreen candela run tests/test_drift.cdl
SDL_VIDEO_DRIVER=offscreen candela run tests/test_lighting.cdl
SDL_VIDEO_DRIVER=offscreen candela run tests/play_coin_dash.cdl
```

Configuring Drift with `-DDRIFT_BUILD_CANDELA=ON` builds the library and adds
these to `ctest`. `tests/bench_movers.cdl` compares frame times of sprites
moved by `Velocity`, by a system, and of physics beside game logic.

## More

The guide and the API reference by area are in the Drift book:
`docs/src/candela/`.
