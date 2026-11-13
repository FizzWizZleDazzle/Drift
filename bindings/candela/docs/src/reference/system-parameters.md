# System Parameters

Drift reads a system's parameter types and passes each one the matching data. Parameters may come in any order, and every parameter needs its type written out. Drift keeps every change a system makes to the components and resources it receives.

## Calls per frame

A system runs once per frame, except:

- When a parameter is a single component (`bird: Bird`), it runs once for each entity that has every such component. An `Entity` parameter then receives that entity, and a component named `_` is required without being read.
- When a parameter is a single collision or message (`hit: Overlap<Bird, Pipe>`, `died: Died`), it runs once for each such event this frame.

A system may not take both a single component and a single event.

## Kinds

| Parameter type | Receives |
|----------------|----------|
| A component, `@component` or engine | That component of the entity this call is for |
| `Entity` | The entity this call is for |
| `Row[]`, where `Row` is a `@query` struct | One row per matching entity |
| `(A, B, ...)[]`, a list of tuples of components | One tuple per entity with every component; `Entity` in a position gives the entity |
| `Lookup<T>` | Every `T`, found by entity with `get(e)` |
| A `@resource` or `@settings` struct | The resource. A system whose resource does not exist yet is skipped that frame |
| `State<S>`, where `S` is a `@state` enum | The current state, to read and to change with `set` |
| `M`, or `M[]`, where `M` is a `@message` struct | One message per call, or every one sent since this system last ran |
| `Contact<A, B>`, or `Contact<A, B>[]` | Collisions that started this frame between an entity with `A` and one with `B` |
| `ContactEnd<A, B>`, or `[]` | Collisions between them that ended this frame |
| `Overlap<A, B>`, or `[]` | Sensor overlaps that started this frame, either entity being the sensor |
| `OverlapEnd<A, B>`, or `[]` | Sensor overlaps that ended this frame |
| `Commands` | A buffer of spawns, inserts, removals, despawns and messages, applied when the call returns |
| An engine resource such as `Time`, `Input` or `Audio` | That resource; see [Engine Resources](resources.md) |

## Rows and tuples

A row or tuple holds the stored component, not a copy, while the call runs: assigning `row.velocity.linear` or `t.1.value` changes it, and Drift keeps the change. Spawning and despawning through `Commands` during the loop is safe, because the list was built before the call started.

The order of rows is the order entities were spawned in, and it is the same every run.

## Lookup

```rust
@system
fn apply_damage(hit: Damage, health: Lookup<Health>) {
    if health.get(hit.target) is Some(h) {
        h.hp -= hit.amount;
    }
}
```

| Method | Returns |
|--------|---------|
| `get(e)` | `Some(component)` when `e` has a `T`, else `None` |
| `has(e)` | Whether `e` has a `T` |
| `len()` | How many entities have a `T` |

## Contacts and overlaps

Each element has `a` and `b`, the two entities, in the order the type names them: in `Contact<Player, Hazard>`, `a` has the `Player` and `b` the `Hazard`. `Contact` also has `point` and `normal` (from `a` towards `b`), the first contact point.

## Engine resources

| Type | What it is |
|------|------------|
| `Time` | `dt`, `elapsed`, `frame` |
| `Input` | Keys, mouse, gamepads and input actions |
| `Window` | Logical size, scale mode, title, clear color, quitting |
| `Assets` | Making textures and sounds, and loading fonts, spritesheets, tilemaps and scenes |
| `Audio` | Music, buses and the output device |
| `Physics` | Gravity, raycasts, impulses and forces |
| `Effects` | Particle bursts, screen shake, flashes, fades and post effects |
| `Camera2D` | Converting between screen and world positions through the main camera |
| `Save` | The save slot named after `[project]` in `drift.toml` |
| `Random` | The game's seed and a random generator per purpose |
| `Draw` | Immediate debug shapes for this frame |
| `Systems` | Turning systems on and off |

Their fields and methods are in [Engine Resources](resources.md).
