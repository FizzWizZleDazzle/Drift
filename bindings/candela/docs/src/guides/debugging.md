# Debugging

Drift gives you several ways to see what a game is doing: the log in the browser console, shapes drawn from systems, collider outlines, an in-game overlay that lists every entity, and input you can script for tests.

## Drawing shapes

`Draw` draws shapes for the current frame only. Use it to see hitboxes, paths, aim lines and anything else that has no sprite:

```rust
@system
fn show_aim(players: (Player, Transform)[], input: Input, draw: Draw) {
    for (_, t) in players {
        draw.line(t.position, input.mouse_world(), rgb(255, 80, 80), 2.0);
        draw.circle_lines(t.position, 48.0, rgba(255, 255, 255, 80), 1.0);
    }
    draw.hud();
    draw.text("aim", vec2(8.0, 8.0), 16.0, rgb(255, 255, 255));
}
```

Draws are in world coordinates, over entities, until `hud()` switches to screen coordinates; `world()` switches back, and `on_layer` and `at_z` place them among entities. `Draw` also has arcs, polygons, styled text, images and nine-slices; see [Engine Resources](../reference/resources.md#draw).

## The log

`log(message)` and `warn(message)` write to the engine log, beside the engine's own messages: settings read, the seed in use, the systems found, errors. In the browser the log is the developer console (F12, or Ctrl+Shift+J); on the desktop it is the terminal that ran `drift run`.

```rust
log("wave " + string(waves.count) + " with " + string(enemies.len()) + " enemies");
```

## Collider outlines

`physics.set_debug_draw(true)` outlines every collider over the game, which shows at once whether a sensor is where you think it is:

```rust
@system
fn debug_keys(input: Input, physics: Physics) {
    if input.key_pressed(Key::F3) {
        physics.set_debug_draw(true);
    }
}
```

## The debug overlay in every build

`import "drift/debug" { * };` puts a small overlay in the bottom-right corner that works in every build, the browser included: frames per second, the number of entities, the time scale, resource fields you watch and recent lines you print.

```rust
@system(schedule: Schedule::Startup)
fn debug_setup(debug: Debug) {
    debug.watch<Game>("score");
    debug.watch<Game>("wave");
}

@system
fn on_hit(d: Damaged, debug: Debug) {
    debug.print("hit for " + string(d.amount));
}
```

Its keys:

| Key | Does |
|-----|------|
| F2 | Shows or hides the overlay |
| F3 | Draws the colliders, as the physics engine sees them |
| F4 | Slow motion on or off, at `slow` (0.25) |
| F6 | Freezes or unfreezes game time |
| F7 | Steps one frame while frozen |

`watch<T>(field)` takes the resource as its type and the field by name, with dots for a field of a struct field (`watch<Game>("player.hp")`). A type that is not a resource raises `drift_not_a_resource`, and a field the resource does not have raises `drift_no_such_field`, which lists the fields it has.

`[debug]` in `drift.toml` sets `shown`, `keys` (false turns the keys off) and `slow`. Remove the import, or set `shown = false` and `keys = false`, for a release.

## The dev overlay

Press **F1** in a running game, in the browser too, to show or hide the engine's overlay. It shows the frame rate, and its panels list every entity and show its engine components (under their C++ names, such as `Transform2D`) with an editor for each field, time the engine's systems, and draw the physics world.

## Errors while running

When a system raises an error it does not catch, Drift discards that system's changes and commands from that call, logs the error, and turns the system off for the rest of the run. The other systems keep running. A compile error stops the game before it starts and names the file and line.

On a page built with `drift build --web --dev`, fix the code and build again: the page [hot reloads](publishing.md#hot-reload-while-you-work) the game without losing its state, and a system that was turned off by an error runs again with its new code. A change that does not compile leaves the game running the code it had and shows the error over it.

Wrap a call you expect to fail, such as loading a file a player picked, in `try` and `catch e`. A missing file does not match `catch "drift_load_failed"`: `texture` and `sound` throw `host_fn_error`, and `assets.font` throws the whole string `"drift_load_failed: <path>"`. See [Errors](../reference/errors.md) for every kind.

What common mistakes look like, so you can recognize them in the log:

| Mistake | What happens |
|---------|--------------|
| A file in a resource's field default is missing (`ship: Texture = texture("assets/ship.png")`) | The game does not start. The page shows "The game could not start" and `Art: building its defaults raised host_fn_error: ... drift_load_failed: assets/ship.png` |
| A file loaded in a system is missing | `loadTexture: cannot read 'assets/ship.png'` (or `Failed to read sound`), then `setup raised host_fn_error`; that system ends there |
| Reading an input action nothing declared | `reader raised drift_unknown_action: no input action "jmup"; the actions are ...`; that system is turned off |
| A binding in `[input]` with a wrong name | `drift.toml: input.jump: '"key:Spcae"' is not a binding` at startup |
| Shaking with no camera entity | Nothing happens and nothing is logged; spawn a `Camera` first, see [Screen shake](effects.md#screen-shake) |
| A dynamic body without `Velocity` | It stays where it was spawned; nothing is logged |
| Lights without `[lighting]` in `drift.toml` | Lights draw nothing; `set_ambient` logs `lighting is off; add a [lighting] section to drift.toml` |
| `effects.add` with a struct that is not a post effect | `Candela: Vec2 is not a post effect` |

## Which systems run

The log lists every system the game declares when it starts (`Candela: 9 systems: setup, new_game, ...`). A system that never seems to run usually takes a resource that does not exist yet, runs in another state, or has a `run_if` that never holds. `Systems.enabled("name")` says whether a system is switched on; a name that is not one of the game's systems raises `drift_unknown_system`, which lists them.

## Scripted input

`Input` can queue input as if a player gave it: `send_key(k, down)`, `send_mouse_move(pos)` and `send_mouse_button(pos, b, down)`. The input arrives next frame. Use it for automated tests and demo playback:

```rust
@resource
struct Demo { frame: int = 0 }

@system
fn play_demo(demo: Demo, input: Input, window: Window) {
    demo.frame += 1;
    if demo.frame == 30 {
        input.send_key(Key::Right, true);
    }
    if demo.frame == 90 {
        input.send_key(Key::Right, false);
        window.screenshot("after_move.png");
    }
    if demo.frame == 100 {
        window.quit();
    }
}
```

## Screenshots

`window.screenshot(path)` saves the next presented frame as a PNG, which suits bug reports and comparing a test run against an image you checked before. In the browser, follow it with `window.download(path)` to hand the file to the player.
