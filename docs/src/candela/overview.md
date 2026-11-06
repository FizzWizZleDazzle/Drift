# Candela

Drift games can be written in [candela](https://candela.lumenfx.dev), a small statically typed scripting language, instead of C++. The `drift` candela package wraps the engine: a game is a candela program run with `candela run game.cdl`, and Drift does the windowing, rendering, physics, text, audio and the rest underneath.

Use it when you want a quick edit-and-run cycle with no C++ build, or when a game's logic reads more naturally as a short script. The engine is the same one C++ games use, so features and rendering match.

## How it fits together

The package lives in `bindings/candela/`:

| Path | What it is |
|------|-----------|
| `shim/drift_candela.cpp` | A C interface over the engine, built with Drift linked in as `libdrift_candela.so` (`.dylib`, `.dll`) |
| `src/main.cdl` | The candela API: typed structs and methods over that library |
| `examples/coin_dash/` | A small complete game |
| `tests/` | Headless tests that drive a real engine |

candela calls native code through its foreign interface, which carries ints, floats, strings, int and float lists, and structs of those. Engine objects (entities, textures, sounds) cross as small integer handles, and the candela side wraps each one in its own type (`Entity`, `Texture`, `Sound`) with methods.

The candela program owns the main loop. Each call to `app.frame()` presents the previous frame, then polls events and runs the engine's systems (input, physics, animation, tweens) for the next one. Between two calls the game reads input, changes entities and queues draws, and those changes show in that frame. In C++ the same split is `App::start()`, `beginFrame()` and `endFrame()`; see [App and Plugins](../concepts/app-and-plugins.md#driving-the-loop-yourself).

## Building the package

Build the native library with the package's script, which configures Drift with `DRIFT_BUILD_CANDELA=ON` and writes the library into `bindings/candela/dist/`:

```sh
cd bindings/candela
./build-native.sh
```

Configuring the whole repository with `-DDRIFT_BUILD_CANDELA=ON` does the same and also registers the candela tests with `ctest`. You need candela 0.1.6 or newer (`candela --version`) to run games and tests, but not to build the library.

## Quick start

Put a game in a folder inside the package, for example `bindings/candela/mygame/game.cdl`:

```rust
import "../src/main.cdl" { start, config, vec2, rgb, Key };

fn main() {
    let app = start(config("Hello", 640, 360));
    let player = app.spawn_rect(vec2(320.0, 180.0), vec2(32.0, 32.0), rgb(90, 170, 255));
    while app.frame() {
        let speed = 200.0 * app.dt();
        if app.key_down(Key::Left) { player.move_by(vec2(-speed, 0.0)); }
        if app.key_down(Key::Right) { player.move_by(vec2(speed, 0.0)); }
        app.draw_text("Arrow keys move", vec2(10.0, 10.0), 20.0, rgb(255, 255, 255));
    }
}
```

```sh
cd bindings/candela/mygame
candela run game.cdl
```

List the names you use in the import's braces; types bring their methods along, so `Key` makes `Key::Left` available and `start` returns an `App` whose methods need no import. `import "../src/main.cdl" as drift;` works too, with every name written `drift::start`, `drift::Key::Left`. Paths for assets are relative to the directory you run from.

`frame()` returns `false` when the window closes or the game calls `app.quit()`, and the engine shuts down at that point. A game that leaves its loop early calls `app.close()`.

## Conventions

- **Coordinates**: world units are pixels, +Y points down. With no camera entity, world position (0, 0) is the top-left of the screen.
- **Floats and ints**: candela never converts between them, so write `vec2(1.0, 2.0)`, not `vec2(1, 2)`. Colors take channels from 0.0 to 1.0, or use `rgb(255, 128, 0)` and `rgba(...)`.
- **Options structs**: functions with many settings take a struct you build with a function of the same name and then edit, for example `let c = collider(); c.restitution = 0.8;`. The structs are `Config`/`config`, `Body`/`body`, `Collider`/`collider`, `Emitter`/`emitter`, `TextStyle`/`text_style`, `SpriteOptions`/`sprite_options` and `PlayOptions`/`play_options`.
- **Errors**: a failing call raises an error kind you can catch, such as `drift_load_failed` for a file that will not load. `last_error()` returns the message that goes with it:

```rust
try {
    let tex = app.load_texture("player.png");
} catch "drift_load_failed" {
    print("could not load: " + last_error());
}
```

- **Dead entities**: calls on a despawned entity do nothing and getters return defaults. `e.alive()` tells you whether it still exists.
- **Lists from the engine**: the foreign interface cannot return a list, so functions such as `app.collisions()`, `app.query(...)` and `map.objects(...)` build theirs on the candela side, one call per element. Call them once per frame and keep the result.

## Limitations

The package is desktop only: candela cannot load native libraries when it runs as WebAssembly, so a candela game does not build for the browser. Write the game in C++ to ship it on the web.

The native side cannot call into candela, so there are no callbacks: collision, sensor and tween completion are polled each frame (`app.collisions()`, `tween.active()`), tweens animate properties but cannot run a function at a step, and Tiled object spawners are replaced by reading `map.objects(...)` and spawning entities yourself. Systems, custom components and the ImGui dev panels stay in C++.

One app runs at a time; `app.close()` it before starting another. Its `Config` (including `lighting`) is fixed at `start`.

## Next

- [API Reference](api.md) lists every function by area.
- [Coin Dash](../examples/coin-dash.md) walks through the example game.
