<!-- Written by scripts/candela-reference.py from the doc comments in
     bindings/candela/drift/src. Edit those, not this page. -->

# Debug Overlay: drift/debug

An on-screen overlay and time controls that work in every build, the browser included:
frames per second, the entity count, the time scale, watched resource fields and recent
messages, with keys for collider outlines, slow motion, freezing and stepping.

```rust
import "drift" { * };
import "drift/debug" { * };
```

See [Debugging](../guides/debugging.md#the-debug-overlay-in-every-build).

## Debug

The overlay's settings and state, read from drift.toml [debug]: take it in a system to
watch fields or print lines.

The overlay sits in the bottom-right corner of the screen layer. With `keys` on, F2 shows or
hides it, F3 draws collider outlines, F4 switches slow motion, F6 freezes time and F7 runs one
frame while frozen.

```rust
@settings(section: "debug")
struct Debug {
    shown: bool = true,
    keys: bool = true,
    slow: float = 0.25,
    watches: ResourceField[] = [],
    lines: string[] = [],
    ages: float[] = [],
    fps: float = 0.0,
    colliders: bool = false,
    frozen: bool = false,
    stepping: int = 0,
    slowed: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `shown` | `bool` | `true` | The overlay shows. |
| `keys` | `bool` | `true` | The F-keys work. Turn them off for a release. |
| `slow` | `float` | `0.25` | The time scale of slow motion. |
| `watches` | `ResourceField[]` | `[]` | Resource fields the overlay shows; add them with watch&lt;Game>("score"). |
| `lines` | `string[]` | `[]` | The lines `print` added that are still on the overlay, oldest first; at most eight. |
| `ages` | `float[]` | `[]` | Seconds each of `lines` has been shown, in real time; a line goes after five. |
| `fps` | `float` | `0.0` | Frames per second, smoothed over recent frames of real time. |
| `colliders` | `bool` | `false` | Whether collider outlines are drawn; F3 switches it. |
| `frozen` | `bool` | `false` | Whether time is frozen; F6 switches it. |
| `stepping` | `int` | `0` | Frames left to run before freezing again after F7; 0 when not stepping. |
| `slowed` | `bool` | `false` | Whether slow motion is on; F4 switches it. |

```toml
[debug]
shown = false
slow = 0.1
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

## dbg_scale

```rust
fn dbg_scale(d: Debug) -> float
```

The time scale the overlay's controls ask for: 0 while frozen, `slow` in slow motion, 1
otherwise.

## dbg_keys

```rust
@system
fn dbg_keys(d: Debug, input: Input, time: Time, physics: Physics)
```

A system the module adds: it runs in every game that imports the module.

Reads F2, F3, F4, F6 and F7 while `keys` is on, and freezes time again after a stepped
frame.

## dbg_overlay

```rust
@system(after: [dbg_keys])
fn dbg_overlay(d: Debug, time: Time, draw: Draw, win: Window)
```

A system the module adds: it runs in every game that imports the module.

Draws the overlay while `shown`: frames per second, the entity count, the time mode, every
watched field and the printed lines, in 14 pixel text at z 90000 on the screen layer.
Printed lines age even while it is hidden.
