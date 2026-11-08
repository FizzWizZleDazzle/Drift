# Queries

A query is a parameter that receives entities by the components they have. A single component parameter gives a system one entity per call (see [Systems](systems.md)); a list parameter gives it every match at once. Write a list as tuples, or as a row struct when you want names or filters.

## Tuples

```rust
@system
fn line_up(movers: (Transform, Speed)[]) {
    let x = 0.0;
    for (t, speed) in movers {
        t.position.x = x;
        x += speed.value;
    }
}
```

`(Transform, Speed)[]` receives one tuple for each entity with both a `Transform` and a `Speed`. `for (t, speed) in movers` names the parts; `row.0` and `row.1` reach them too. The tuple holds the stored components, so changing `t` changes the entity's `Transform`, and Drift keeps the change.

Put `Entity` in a tuple to get the entity's id, for commands and lookups, and `_` for a part you do not read:

```rust
@system
fn despawn_dead(dead: (Entity, Health)[], cmd: Commands) {
    for (e, health) in dead {
        if health.hp <= 0 {
            cmd.despawn(e);
        }
    }
}
```

## Row structs

A `@query` struct names its fields and can filter on components it does not read:

```rust
@query(with: [Enemy], without: [Stunned])
struct Chaser { entity: Entity, transform: Transform, speed: Speed }

@system
fn chase(chasers: Chaser[], players: (Player, Transform)[], time: Time) {
    for (_, target) in players {
        for c in chasers {
            let dir = (target.position - c.transform.position).normalized();
            c.transform.position = c.transform.position + dir * (c.speed.value * time.dt);
        }
    }
}
```

`with:` requires components without passing them; `without:` excludes entities that have them. Each field is a component, or `Entity`.

## Lookups

A query walks every match. To reach one entity's component by its id, take a `Lookup<T>`:

```rust
@system
fn hurt(hit: Contact<Bullet, Health>, health: Lookup<Health>, cmd: Commands) {
    if health.get(hit.b) is Some(h) {
        h.hp -= 10;
    }
    cmd.despawn(hit.a);
}
```

`get(e)` answers `Some(component)` or `None`; `has(e)` and `len()` answer what they say. A change made through `get` is kept.

## What a query sees

A query is built when its call starts. Entities spawned and despawned through `Commands` appear or disappear once the call returns, so looping and spawning at once is safe. Changes earlier systems made this frame are visible, and so are the engine's changes from the work it finished before the system started.

Rows come in the order entities were spawned, the same on every run.
