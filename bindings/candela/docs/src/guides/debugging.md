# Debugging

Drift gives you several ways to see what a game is doing: shapes drawn from systems, the engine log, an in-game overlay that shows every entity and resource, per-system timings, and input you can script for tests.

## Drawing shapes

`drift::Draw` draws shapes for the current frame only. Use it to see hitboxes, paths, aim lines and anything else that has no sprite:

```rust
@drift::system
fn show_aim(players: (Player, Transform)[], input: Input, draw: drift::Draw) {
    for row in players {
        let (_, t) = row;
        draw.line(t.position, input.mouse_world(), rgb(255, 80, 80), 2.0);
        draw.circle_lines(t.position, 48.0, rgba(255, 255, 255, 80), 1.0);
    }
    draw.hud();
    draw.text("aim", vec2(8.0, 8.0), 16.0, rgb(255, 255, 255));
}
```

Draws are in world coordinates until `hud()` switches to screen coordinates; `world()` switches back.

## The log

`log(message)` and `warn(message)` write to the engine log, beside the engine's own messages: assets loaded, settings read, errors.

```rust
log("wave " + string(waves.count) + " with " + string(enemies.len()) + " enemies");
```

## The dev overlay

Dev builds have an in-game overlay. Press **F1** to show or hide it.

| Panel | What it does |
|-------|--------------|
| Inspector | Lists every entity as a tree by `Parent`, by `Name` or id. Selecting one shows its components, your own included, with an editor for each field; edits apply at once. Add and remove components, spawn and despawn entities. Resources, yours and the engine's settings, are listed below the entities and edit the same way |
| Performance | A graph of frame times and a table of how long each system took in the last frame. Your systems appear by function name beside the engine's, with the time spent in the script |
| Physics | Draws the colliders, contact points and normals as the physics engine sees them |
| Scene | Saves the world to a file, spawns a scene file, reloads a file |

A change you make in the inspector is a change to the stored component, so your systems see it on their next run.

## Errors while running

When a system raises an error it does not catch, Drift discards that system's changes and commands for the frame and logs the error with the script file and line. The other systems keep running. In dev builds the failing system stays off, and the overlay marks it, until you save its script; the reload turns it back on. In release builds it runs again next frame.

Wrap a call you expect to fail, such as loading a file a player picked, in `try` and `catch "drift_load_failed"`. See [Errors](../reference/errors.md) for every kind.

## drift check

```sh
drift check
```

Compiles the game without running it and lists every system with its schedule, what it reads, what it writes, and the order it runs in. Use it to answer "why does this system run before that one" and "why is my change discarded": a change to a type missing from `writes:` is discarded, and dev builds log the first one.

## Scripted input

`drift::Input` can queue input as if a player gave it: `send_key(k, down)`, `send_mouse_move(pos)` and `send_mouse_button(pos, b, down)`. The input arrives next frame. Use it for automated tests and demo playback:

```rust
@drift::resource
struct Demo { frame: int = 0 }

@drift::system(writes: [Demo])
fn play_demo(demo: Demo, input: Input, window: drift::Window) {
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

`window.screenshot(path)` saves the next presented frame as a PNG, which suits bug reports and comparing a test run against an image you checked before.
