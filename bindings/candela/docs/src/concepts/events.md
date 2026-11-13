# Events and Collisions

An event says something happened: a door opened, an enemy died, two bodies collided. Drift has two kinds: messages your systems send each other, and collisions the physics engine reports. A system reads both the same way.

## Messages

A message is a struct marked `@message`. One system sends it, and every system that reads its type receives it. Messages keep systems apart: the one that detects a death does not need to know about the score, the sound or the particles that follow.

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

- A list parameter, `deaths: Died[]`, receives every message of that type sent since the system last ran.
- A single parameter, `died: Died`, runs the system once for each of them.
- A reader ordered `after:` the sender gets the messages the same frame; one that runs before it gets them next frame.

## Later, and from outside

`cmd.send_after(seconds, message)` sends a message after a delay. The engine sends messages too: an `HttpResponse` answers an HTTP request, a `SocketEvent` reports a WebSocket's traffic (see [Networking](../guides/networking.md)), and an `AnimationFinished` tells that a sprite animation clip played to its end (see [Movement](../guides/movement.md#animation-frames)). They arrive at the start of a frame, before the `Update` systems run, and are read like messages a system sent.

## Collisions

Physics reports collisions as events typed by components on the two entities:

| Type | Happens when |
|------|--------------|
| `Contact<A, B>` | Two solid colliders started touching: one entity has an `A`, the other a `B` |
| `ContactEnd<A, B>` | They stopped touching |
| `Overlap<A, B>` | An entity entered a sensor collider (`sensor: true`), whichever of the two is the sensor |
| `OverlapEnd<A, B>` | It left |

`a` is the entity with `A` and `b` the one with `B`. A collision between entities that do not have the two components is not delivered, so each system receives only the collisions it cares about. A single parameter makes a collision handler, called once per event:

```rust
@system
fn spikes(hit: Contact<Player, Spikes>, health: Lookup<Health>) {
    if health.get(hit.a) is Some(h) {
        h.hp = 0;
    }
}

@system
fn pick_up(hit: Overlap<Player, Coin>, cmd: Commands, score: Score) {
    score.value += 1;
    cmd.despawn(hit.b);
}
```

A list, `hits: Contact<Player, Spikes>[]`, receives the frame's events at once.

`Contact` also carries `point`, the first contact point, and `normal`, the direction from `a` to `b`. A contact with the ground whose normal points down, `hit.normal.y > 0.5`, means the player landed on top.

Collision events from a physics step reach systems the frame after it. See [Collisions and Physics](../guides/collisions.md) for bodies, colliders and sensors.
