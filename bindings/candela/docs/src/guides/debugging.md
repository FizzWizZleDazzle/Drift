# Debugging

Drift gives you several ways to see what a game is doing: the log in the browser console, shapes drawn from systems, debug views of colliders, bounds, cameras and lights, an in-game overlay with timings and recent errors, an inspector that shows any entity you click, and input you can script for tests.

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

Draws are in world coordinates, over entities, until `hud()` switches to screen coordinates; `world()` switches back, and `on_layer` and `at_z` place them among entities. World draws are lit and go through post effects like the entities around them, so in a dark, lit game they are dark too. `overlay()` keeps world coordinates but draws after lighting and post effects, at full brightness, which suits debug shapes:

```rust
draw.overlay();
draw.circle_lines(enemy.position, enemy.sight, rgb(255, 80, 80), 2.0);
```

`Draw` also has arcs, polygons, styled text, images and nine-slices; see [Engine Resources](../reference/resources.md#draw).

## The log

`log(message)` and `warn(message)` write to the engine log, beside the engine's own messages: settings read, the seed in use, the systems found, errors. In the browser the log is the developer console; on the desktop it is the terminal that ran `drift run`. A Drift page takes the function keys while the game has focus, so open the console from the browser's menu (More tools, Developer tools in Chrome) if F12 does nothing. The [debug overlay](#the-debug-overlay-in-every-build) and the [dev overlay](#the-dev-overlay-and-the-inspector) also show recent warnings and errors in the game.

```rust
log("wave " + string(waves.count) + " with " + string(enemies.len()) + " enemies");
```

## The debug overlay in every build

`import "drift/debug" { * };` adds an overlay in the bottom-right corner that works in every build, the browser included:

- frames per second, the frame time in milliseconds and a graph of the last 120 frames, with lines at 60 and 30 frames per second;
- the time the engine's phases took last frame (update and render, then each phase);
- draw calls, sprites drawn, entities, physics bodies and, in the browser, the memory the page uses;
- warnings and errors from the last ten seconds, the latest four;
- the time mode when frozen or slowed, resource fields you watch and lines you print;
- a menu of the debug views, slow motion and freeze: click a row to switch it.

```rust
@system(schedule: Schedule::Startup)
fn debug_setup(debug: Debug) {
    debug.watch<Game>("score");
    debug.watch<Game>("wave");
    debug.view("colliders", true);
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
| F4 | Colliders view on or off |
| F8 | Freezes or unfreezes game time |
| F9 | Steps one frame while frozen |
| Shift+F8 | Slow motion on or off, at `slow` (0.25) |
| F1 | The engine's dev overlay and inspector (see [below](#the-dev-overlay-and-the-inspector)) |

The overlay uses keys that have no browser shortcut of their own in Chrome, Firefox and Edge, and stays off the ones that do: F3 (find), F5 (reload), F6 (address bar), F7 (caret browsing), F11 (full screen) and F12 (developer tools). On a Mac keyboard hold Fn for the function keys. Clicks on the overlay's menu also reach the game.

`watch<T>(field)` takes the resource as its type and the field by name, with dots for a field of a struct field (`watch<Game>("player.hp")`). A type that is not a resource raises `drift_not_a_resource`, and a field the resource does not have raises `drift_no_such_field`, which lists the fields it has.

`[debug]` in `drift.toml` sets `shown`, `keys` (false turns the keys off), `slow`, `views` (the views on at the start) and `size` (the text size; 0 picks one from the screen height). Remove the import, or set `shown = false` and `keys = false`, for a release. Without the import nothing of it runs, and while it is hidden it only ages the printed lines.

```toml
[debug]
views = ["colliders", "camera"]
```

## Debug views

Debug views draw what the engine sees over the finished frame. They draw after lighting and post effects, so ambient darkness, vignettes and tints leave them at full brightness, and they follow the cameras. Switch them from the overlay's menu, with `debug.view(name, on)`, or from the dev overlay.

| View | Shows |
|------|-------|
| `colliders` | Every collider: boxes, circles, polygons, the chain loops of static texture colliders and the pieces of moving ones. Static bodies are green, kinematic blue, dynamic orange; a sleeping body is dimmed; sensors are dashed and unfilled. A red dot with a yellow normal marks each contact for half a second of game time after it begins, and a white arrow shows each moving body's velocity (a quarter second of travel). |
| `bounds` | The outline of what each sprite, shape and text draws, and a cross at each entity's position (its pivot) |
| `camera` | Each camera's view (cyan) and center, its `CameraBounds` area (magenta), its `CameraFollow` dead zone (yellow) and a line to the point it follows |
| `lights` | Each light's reach in its color (a circle, or a spot light's cone) and the outlines of light occluders; only with `[lighting]` |
| `tiles` | The tile grid of each tilemap near the view, with brighter lines on the edges of the 16 by 16 tile chunks maps are drawn in; cell lines drop out when cells are under 4 pixels on screen |

`physics.set_debug_draw(true)` is the same as the `colliders` view, for a game that does not import `drift/debug`:

```rust
@system
fn debug_keys(input: Input, physics: Physics) {
    if input.key_pressed(Key::F4) {
        physics.set_debug_draw(true);
    }
}
```

## The dev overlay and the inspector

Press **F1** in a running game, in the browser too, to show or hide the engine's dev overlay. Its menu shows the frame rate, a check box per panel and one per debug view.

- **Inspector.** Lists every entity. Click an entity in the game to select it (the one drawn on top wins; the inspector outlines whatever is under the mouse) or pick it from the list. It shows the engine components under their C++ names, such as `Transform2D`, with an editor for each field, and below them the game's own components with their fields, read-only.
- **Performance.** A frame time graph, the backend, draw calls, sprites, entities and memory, the time of each engine phase, and the time of each engine system last frame.
- **Log.** The latest warnings and errors, newest at the bottom.
- **Physics.** The colliders view with its options (shapes, bounding boxes, contacts, velocities, joints) and the body count.
- **Scene.** Saves the world to a scene file and spawns or reloads one.

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
| A binding in `[input]` with a wrong name | `drift.toml:12: input.jump: "key:Spcae" is not a binding; write one such as "key:Space"` at startup |
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
