# Queries

A query is a parameter that receives entities by the components they have. A row parameter, one tuple such as `(Player, Transform)`, gives a system one entity per call (see [Systems](systems.md)); a list parameter gives it every match at once. Write the list as tuples, or as a row struct when you want names or filters.

## Tuples

```rust
@system
fn line_up(movers: (Transform, Speed)[]) {
    let x = 0.0;
    for (t, speed) in movers {
        t.position = vec2(x, t.position.y);
        x += speed.value;
    }
}
```

`(Transform, Speed)[]` receives one tuple for each entity with both a `Transform` and a `Speed`. `for (t, speed) in movers` names the parts. The tuple holds the stored components, so changing `t` changes the entity's `Transform`, and Drift keeps the change.

Put `Entity` in a tuple to get the entity's id, for commands and lookups, and `_` for a part you do not read:

```rust
@system
fn despawn_dead(things: (Entity, Health)[], cmd: Commands) {
    for (e, health) in things {
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

`with:` requires components without passing them; `without:` leaves out entities that have them. Each field is a component, or `Entity`.

## Lookups

A query walks every match. To reach one entity's component by its id, such as the other entity in a collision, take a `Lookup<T>`:

```rust
@system
fn hurt(hit: Contact<Bullet, Enemy>, health: Lookup<Health>, cmd: Commands) {
    if health.get(hit.b) is Some(h) {
        h.hp -= 10;
    }
    cmd.despawn(hit.a);
}
```

`get(e)` answers `Some(component)` or `None`; `has(e)` and `len()` answer what they say. A change made through `get` is kept, and a second `get` of the same entity in the same call answers the same value, changes included.

A lookup fetches only the entities a system asks it for, so it suits a system that runs once per entity and reads one other entity, such as a leg reading its body.

## What a query sees

A query is built when the call starts. Entities spawned and despawned through `Commands` appear or disappear once the call returns, so looping and spawning at once is safe. Changes earlier systems made this frame are visible.

Rows come in the order entities were spawned, the same on every run.

## Keeping queries cheap

A query of an engine component, such as `Transform` or `Rectangle`, reads every entity that has it from the engine each time the system runs. Ask for your own tag components alongside, such as `(Enemy, Transform)[]`, so the query covers only the entities you need. A list parameter in a system that runs once per entity is built again for every call; when such a system needs one other entity, a `Lookup` reads just that one. A level built from hundreds of tiles is better as a [tilemap](../guides/levels-and-tilemaps.md) than as hundreds of entities.
