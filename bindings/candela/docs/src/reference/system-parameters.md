# System Parameters

Drift reads a system's parameter types and passes each one the matching data. The parameters say what the system reads; `writes:` on the attribute says what it may change. Parameters may come in any order, and every parameter needs its type written out.

| Parameter type | Receives | Changes kept when |
|----------------|----------|-------------------|
| `Row[]`, where `Row` is a `@drift::query` struct | One row per matching entity | The component's type is in `writes:` |
| `(A, B, ...)[]`, a list of tuples of components | One tuple per entity with every component; `drift::Entity` in a position gives the entity | The component's type is in `writes:` |
| `drift::Lookup<T>` | Every `T`, found by entity with `get(e)` | `T` is in `writes:` |
| A `@drift::resource` or `@drift::settings` struct | The resource. A system whose resource does not exist yet is skipped that frame | The type is in `writes:` |
| `M[]`, where `M` is a `@drift::message` struct | Every `M` sent since this system last ran | Never: messages are read-only |
| `drift::Contact<A, B>[]` | Collisions that started this frame between an entity with `A` and one with `B` | Never |
| `drift::ContactEnd<A, B>[]` | Collisions between them that ended this frame | Never |
| `drift::Overlap<A, B>[]` | Sensor overlaps that started this frame, either entity being the sensor | Never |
| `drift::OverlapEnd<A, B>[]` | Sensor overlaps that ended this frame | Never |
| `drift::Commands` | A buffer of spawns, inserts, removals, despawns and messages, applied when the system returns | Always |
| An engine resource such as `drift::Time`, `drift::Input` or `drift::Audio` | That resource; see [Engine Resources](resources.md) | Its methods act at once |

## Rows and tuples

A row or tuple holds the stored component, not a copy, while the system runs: assigning `row.velocity.linear` or `t.1.value` changes it, and Drift keeps the change when the system returns, if the type is in `writes:`. Spawning and despawning through `Commands` during the loop is safe, because the list was built before the system started.

The order of rows is the order entities were spawned in, and it is the same every run.

## Lookup

```rust
@drift::system(writes: [Health])
fn apply_damage(hits: Damage[], health: drift::Lookup<Health>) {
    for hit in hits {
        if health.get(hit.target) is Some(h) {
            h.hp -= hit.amount;
        }
    }
}
```

| Method | Returns |
|--------|---------|
| `get(e)` | `Some(component)` when `e` has a `T`, else `None` |
| `has(e)` | Whether `e` has a `T` |
| `len()` | How many entities have a `T` |

## Contacts and overlaps

Each element has `a` and `b`, the two entities, in the order the type names them: in `drift::Contact<Player, Hazard>`, `a` has the `Player` and `b` the `Hazard`. `Contact` also has `point` and `normal` (from `a` towards `b`), the first contact point.

## Engine resources

| Type | What it is |
|------|------------|
| `drift::Time` | `dt`, `elapsed`, `frame` |
| `drift::Input` | Keys, mouse, gamepads and input actions |
| `drift::Window` | Logical size, scale mode, title, clear color, quitting |
| `drift::Assets` | Loading and making textures, sounds, fonts, spritesheets, tilemaps and scenes |
| `drift::Audio` | Music, buses and the output device |
| `drift::Physics` | Gravity, raycasts, impulses and forces |
| `drift::Effects` | Particle bursts, screen shake, flashes, fades and post effects |
| `drift::Camera2D` | Converting between screen and world positions through the main camera |
| `drift::Save` | The save slot named after `[project]` in `drift.toml` |
| `drift::Draw` | Immediate debug shapes for this frame |
| `drift::Systems` | Turning systems on and off |

Their fields and methods are in [Engine Resources](resources.md).
