# Queries

A query is a system parameter that receives every entity with a given set of components. Write it as a list of tuples, or as a list of a row struct when you want names or filters.

## Tuples

```rust
@drift::system(writes: [Transform])
fn drift_right(movers: (Transform, Speed)[], time: drift::Time) {
    for row in movers {
        let (t, speed) = row;
        t.position.x += speed.value * time.dt;
    }
}
```

`(Transform, Speed)[]` receives one tuple for each entity with both a `Transform` and a `Speed`. `let (t, speed) = row;` names the parts, and `row.0`, `row.1` reach them too. The tuple holds the stored components, so changing `t` changes the entity's `Transform`, and the change is kept because `Transform` is in `writes:`.

Put `drift::Entity` in a tuple to get the entity's id, for commands and lookups:

```rust
@drift::system
fn despawn_dead(dead: (drift::Entity, Health)[], cmd: drift::Commands) {
    for row in dead {
        if row.1.hp <= 0 {
            cmd.despawn(row.0);
        }
    }
}
```

## Row structs

A `@drift::query` struct names its fields and can filter on components it does not read:

```rust
@drift::query(with: [Enemy], without: [Stunned])
struct Chaser { entity: drift::Entity, transform: Transform, speed: Speed }

@drift::system(writes: [Transform])
fn chase(chasers: Chaser[], players: (Player, Transform)[], time: drift::Time) {
    for row in players {
        let (_, target) = row;
        for c in chasers {
            let dir = target.position.sub(c.transform.position).normalized();
            c.transform.position = c.transform.position.add(dir.mul(c.speed.value * time.dt));
        }
    }
}
```

`with:` requires components without passing them; `without:` excludes entities that have them. Each field is a component, or `drift::Entity`.

## Lookups

A query walks every match. To reach one entity's component by its id, take a `drift::Lookup<T>`:

```rust
@drift::system(writes: [Health])
fn hurt(hits: drift::Contact<Bullet, Health>[], health: drift::Lookup<Health>,
        cmd: drift::Commands) {
    for hit in hits {
        if health.get(hit.b) is Some(h) {
            h.hp -= 10;
        }
        cmd.despawn(hit.a);
    }
}
```

`get(e)` answers `Some(component)` or `None`; `has(e)` and `len()` answer what they say.

## What a query sees

A query is built when its system starts. Entities spawned and despawned through `Commands` during the system appear or disappear once it returns, so looping and spawning at once is safe. Changes earlier systems made this frame are visible, and so are the engine's changes from the work it finished before the system started.

Rows come in the order entities were spawned, the same on every run.
