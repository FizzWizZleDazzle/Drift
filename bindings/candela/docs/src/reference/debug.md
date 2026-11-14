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
| `lines` | `string[]` | `[]` |  |
| `ages` | `float[]` | `[]` |  |
| `fps` | `float` | `0.0` |  |
| `colliders` | `bool` | `false` |  |
| `frozen` | `bool` | `false` |  |
| `stepping` | `int` | `0` |  |
| `slowed` | `bool` | `false` |  |

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

A line on the overlay that goes after five seconds; also logged.

## dbg_scale

```rust
fn dbg_scale(d: Debug) -> float
```

## dbg_keys

```rust
@system
fn dbg_keys(d: Debug, input: Input, time: Time, physics: Physics)
```

A system the module adds: it runs in every game that imports the module.

## dbg_overlay

```rust
@system(after: [dbg_keys])
fn dbg_overlay(d: Debug, time: Time, draw: Draw, win: Window)
```

A system the module adds: it runs in every game that imports the module.
