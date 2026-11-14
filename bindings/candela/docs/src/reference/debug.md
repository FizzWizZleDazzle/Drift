<!-- Written by scripts/candela-reference.py from the doc comments in
     bindings/candela/drift/src. Edit those, not this page. -->

# Debug Overlay: drift/debug

An on-screen overlay, debug views and time controls that work in every build, the browser
included: frames per second, a frame time graph, time per engine phase, counters, recent
warnings and errors, watched resource fields and printed lines, with a menu and keys for the
views, slow motion, freezing and stepping. Debug views draw after lighting and post effects,
so they stay readable in a dark, lit game.

```rust
import "drift" { * };
import "drift/debug" { * };
```

See [Debugging](../guides/debugging.md#the-debug-overlay-in-every-build).

## Debug

The overlay's settings and state, read from drift.toml [debug]: take it in a system to
watch fields, print lines or switch views.

The overlay sits in the bottom-right corner of the screen layer. With `keys` on, F2 shows or
hides it, F4 switches the colliders view, F8 freezes time, F9 runs one frame while frozen and
Shift+F8 switches slow motion. Its menu rows switch each view with a click.

```rust
@settings(section: "debug")
struct Debug {
    shown: bool = true,
    keys: bool = true,
    slow: float = 0.25,
    views: string[] = [],
    size: float = 0.0,
    watches: ResourceField[] = [],
    lines: string[] = [],
    ages: float[] = [],
    fps: float = 0.0,
    frames: float[] = [],
    frozen: bool = false,
    stepping: int = 0,
    slowed: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `shown` | `bool` | `true` | The overlay shows. |
| `keys` | `bool` | `true` | The keys work. Turn them off for a release. |
| `slow` | `float` | `0.25` | The time scale of slow motion. |
| `views` | `string[]` | `[]` | The debug views that are on, by name: `colliders`, `bounds`, `camera`, `lights` (with `[lighting]`) and `tiles`. drift.toml sets the ones on at the start. |
| `size` | `float` | `0.0` | Text size in screen pixels; 0 picks one from the window height (20 at 1080). The overlay keeps this size however the game's logical resolution is scaled. |
| `watches` | `ResourceField[]` | `[]` | Resource fields the overlay shows; add them with watch&lt;Game>("score"). |
| `lines` | `string[]` | `[]` | The lines `print` added that are still on the overlay, oldest first; at most eight. |
| `ages` | `float[]` | `[]` | Seconds each of `lines` has been shown, in real time; a line goes after five. |
| `fps` | `float` | `0.0` | Frames per second, smoothed over recent frames of real time. |
| `frames` | `float[]` | `[]` | The last 120 frame times in milliseconds of real time, oldest first. |
| `frozen` | `bool` | `false` | Whether time is frozen; F8 switches it. |
| `stepping` | `int` | `0` | Frames left to run before freezing again after F9; 0 when not stepping. |
| `slowed` | `bool` | `false` | Whether slow motion is on; Shift+F8 switches it. |

```toml
[debug]
shown = false
slow = 0.1
views = ["colliders", "camera"]
```

```rust
@system(schedule: Schedule::Startup)
fn watch_score(debug: Debug) {
    debug.watch<Game>("score");
}
```

### Debug.watch

```rust
fn watch<T>(self, path: string)
```

Shows a field of the resource T on the overlay: watch&lt;Game>("score"), or
watch&lt;Game>("player.hp") for a field of a struct field. A type that is not a resource,
or a field it does not have, raises drift_not_a_resource or drift_no_such_field.

### Debug.print

```rust
fn print(self, text: string)
```

A line on the overlay that goes after five seconds of real time; also logged. The
overlay keeps the eight newest lines.

```rust
debug.print("boss spawned");
```

### Debug.view

```rust
fn view(self, name: string, on: bool)
```

Switches a debug view by name: `colliders`, `bounds`, `camera`, `lights` or `tiles`. A
name no view has is logged and changes nothing.

```rust
debug.view("bounds", true);
```

### Debug.showing

```rust
fn showing(self, name: string) -> bool
```

Whether the view `name` is on.

## dbg_scale

```rust
fn dbg_scale(d: Debug) -> float
```

The time scale the overlay's controls ask for: 0 while frozen, `slow` in slow motion, 1
otherwise.

## dbg_start

```rust
@system(schedule: Schedule::Startup)
fn dbg_start(d: Debug)
```

A system the module adds: it runs in every game that imports the module.

Switches on the views drift.toml [debug] `views` names.

## dbg_keys

```rust
@system
fn dbg_keys(d: Debug, input: Input, time: Time)
```

A system the module adds: it runs in every game that imports the module.

Reads F2, F4, F8, Shift+F8 and F9 while `keys` is on, and freezes time again after a
stepped frame.

## dbg_overlay

```rust
@system(after: [dbg_keys])
fn dbg_overlay(d: Debug, time: Time, draw: Draw, win: Window, input: Input)
```

A system the module adds: it runs in every game that imports the module.

Draws the overlay while `shown`: frames per second and a frame time graph, time per engine
phase, counters, recent warnings and errors, the time mode, watched fields, printed lines,
and the menu of views. Printed lines age even while it is hidden.
