# Messages and Collisions

A message says something happened: a door opened, an enemy died, the player took damage. One system sends it, and every system that reads its type receives it. Messages keep systems apart: the one that detects a death does not need to know about the score, the sound or the particles that follow.

## Sending and reading

```rust
@message
struct Died { entity: Entity, position: Vec2 }

@system
fn check_deaths(living: (Entity, Health, Transform)[], cmd: Commands) {
    for (e, health, t) in living {
        if health.hp <= 0 {
            cmd.send(Died { entity: e, position: t.position });
            cmd.despawn(e);
        }
    }
}

@system(after: [check_deaths])
fn score_deaths(deaths: Died[], score: Score) {
    score.value += deaths.len() * 10;
}

@system(after: [check_deaths])
fn death_effects(died: Died, effects: Effects) {
    effects.burst(died.position, ParticleEmitter { .. }, 30);
}
```

A list parameter, `deaths: Died[]`, receives every message of the type sent since the system last ran, once. A single parameter, `died: Died`, runs the system once for each of them. A reader ordered `after:` the sender gets the messages the same frame; one that runs before it gets them next frame. Messages nobody reads are dropped at the end of the next frame.

## Later, and from outside

`cmd.send_after(seconds, message)` sends a message after a delay. The engine sends messages too: an `HttpResponse` answers an HTTP request and a `SocketEvent` reports a WebSocket's traffic (see [Networking](../guides/networking.md)). Both kinds arrive at the start of a frame, before the `Update` systems run, and are read like messages a system sent.

## Collisions

Physics reports collisions as messages, typed by the components of the two entities:

| Type | Event |
|------|-------|
| `Contact<A, B>` | Two solid colliders started touching: one entity has `A`, the other `B` |
| `ContactEnd<A, B>` | They stopped touching |
| `Overlap<A, B>` | An entity entered a sensor collider, either one being the sensor |
| `OverlapEnd<A, B>` | It left |

`a` is the entity with `A` and `b` the one with `B`, whichever collider physics listed first. A pair whose entities do not have the two components is not delivered, so each system receives only the collisions it cares about. A single parameter makes the system a collision handler, called once per event:

```rust
@system
fn spikes(hit: Contact<Player, Spikes>, health: Lookup<Health>) {
    if health.get(hit.a) is Some(h) {
        h.hp = 0;
    }
}
```

A list, `hits: Contact<Player, Spikes>[]`, receives the frame's events at once.

`Contact` also carries `point` and `normal`, the first contact point and the direction from `a` to `b`. See [Physics](../guides/physics.md).
