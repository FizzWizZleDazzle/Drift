# drift for candela

Write 2D games for the Drift engine in [candela](https://candela.lumenfx.dev).

A game is an ordinary candela program: it starts the engine, runs a loop, and
reads input, moves entities and draws between frames. Drift supplies the
window, renderer, physics, text, audio, tilemaps and the rest; the package
ships them as one native library, so there is nothing else to install.

## Quick start

Build the native library once, from this directory:

```sh
./build-native.sh            # writes dist/libdrift_candela.so, .dylib or .dll
```

Then write a game in a folder inside the package, for example `mygame/game.cdl`, and run it:

```rust
import "../src/main.cdl";

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
candela run game.cdl
```

`app.frame()` presents the previous frame, then polls input and runs the
engine for the next one; it returns `false` once the window closes. Import the
package bare, as above, so enum variants such as `Key::Left` resolve.

## What it covers

- The frame loop, delta time, logical resolution and scale modes, screenshots.
- Keyboard, mouse and gamepad state, and named input actions and axes with
  rebinding and JSON save.
- Entities with handles: transform, sprite, text, camera, physics body and
  colliders, particle emitter, lights, tilemap, parent and child, plus any
  reflected component through JSON.
- Collision and sensor events per frame, raycasts, impulses and gravity.
- Immediate drawing of shapes, textures and text on world layers or the HUD.
- Textures from files or pixel lists, spritesheets, Aseprite files and
  animation clips.
- Sounds from files or samples, voices, mixer buses and music.
- Tweens with easing, timers and cooldowns, post effects and screen fades,
  save data, Tiled and LDtk maps, JSON scenes and prefabs.

The [Coin Dash example](examples/coin_dash/main.cdl) uses most of these in a
small complete game.

## Limitations

Desktop only: candela cannot load native libraries when it runs as
WebAssembly, so a candela game does not build for the browser. The native
library cannot call back into candela, so the game polls for events each frame
instead of registering handlers. One app runs at a time.

## Tests

```sh
SDL_VIDEO_DRIVER=offscreen candela run tests/test_drift.cdl
SDL_VIDEO_DRIVER=offscreen candela run tests/test_lighting.cdl
SDL_VIDEO_DRIVER=offscreen candela run tests/play_coin_dash.cdl
```

Configuring Drift with `-DDRIFT_BUILD_CANDELA=ON` builds the library and adds
these to `ctest`.

## More

The API reference by area is in the Drift book: `docs/src/candela/`.
