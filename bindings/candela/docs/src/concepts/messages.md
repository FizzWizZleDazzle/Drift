# Messages and Collisions

A message says something happened: a door opened, an enemy died, the player took damage. One system sends it, and every system that reads its type receives it. Messages keep systems apart: the one that detects a death does not need to know about the score, the sound or the particles that follow.

## Sending and reading

```rust
@drift::message
struct Died { entity: drift::Entity, position: Vec2 }

@drift::system
fn check_deaths(living: (drift::Entity, Health, Transform)[], cmd: drift::Commands) {
    for row in living {
        let (e, health, t) = row;
        if health.hp <= 0 {
            cmd.send(Died { entity: e, position: t.position });
            cmd.despawn(e);
        }
    }
}

@drift::system(writes: [Score], after: [check_deaths])
fn score_deaths(deaths: Died[], score: Score) {
    score.value += deaths.len() * 10;
}

@drift::system(after: [check_deaths])
fn death_effects(deaths: Died[], effects: drift::Effects) {
    for d in deaths {
        effects.burst(d.position, ParticleEmitter { .. }, 30);
    }
}
```

A system receives every message of the type sent since it last ran, once. A reader ordered `after:` the sender gets the messages the same frame; one that runs before it gets them next frame. Messages nobody reads are dropped at the end of the next frame.

## Collisions

Physics reports collisions as messages, typed by the components of the two entities:

| Parameter | Receives |
|-----------|----------|
| `drift::Contact<A, B>[]` | Two solid colliders started touching: one entity has `A`, the other `B` |
| `drift::ContactEnd<A, B>[]` | They stopped touching |
| `drift::Overlap<A, B>[]` | An entity entered a sensor collider, either one being the sensor |
| `drift::OverlapEnd<A, B>[]` | It left |

`a` is the entity with `A` and `b` the one with `B`, whichever collider physics listed first. A pair whose entities do not have the two components is not delivered, so each system receives only the collisions it cares about:

```rust
@drift::system(writes: [Health])
fn spikes(hits: drift::Contact<Player, Spikes>[], health: drift::Lookup<Health>) {
    for hit in hits {
        if health.get(hit.a) is Some(h) {
            h.hp = 0;
        }
    }
}
```

`Contact` also carries `point` and `normal`, the first contact point and the direction from `a` to `b`. See [Physics](../guides/physics.md).
