# Physics

Drift simulates rigid bodies with Box2D. Give an entity a body and a collider, and the engine moves it, collides it and tells your systems what touched what. World units are pixels and +Y points down.

## Bodies and colliders

A `RigidBody` makes an entity simulated; a collider gives it a shape. Spawn both together:

```rust
@system(schedule: Schedule::Startup)
fn setup(cmd: Commands) {
    // A crate that falls and tumbles.
    cmd.spawn([
        at(200.0, 50.0),
        Rectangle { size: vec2(32.0, 32.0), color: rgb(180, 120, 60), .. },
        RigidBody { .. },
        BoxCollider { half_size: vec2(16.0, 16.0), .. },
    ]);
    // The floor: a collider with no body is static.
    cmd.spawn([
        at(320.0, 340.0),
        Rectangle { size: vec2(640.0, 20.0), color: rgb(70, 70, 90), .. },
        BoxCollider { half_size: vec2(320.0, 10.0), .. },
    ]);
}
```

| `BodyKind` | Behavior |
|------------|----------|
| `Dynamic` | Falls, collides and is pushed. The default |
| `Kinematic` | Moves only by its `Velocity`; pushes dynamic bodies and is not pushed |
| `Static` | Never moves |

`fixed_rotation` keeps a character upright. `density`, `friction` and `restitution` (bounciness) live on the collider. `BoxCollider` and `CircleCollider` take an `offset` from the entity's position.

The body is created at the end of the frame its components are added, from the settings they hold then. To change body settings later, remove the `RigidBody` and insert a new one.

## Moving bodies

On a dynamic body, `Velocity` is the body's velocity: read it to see how fast the body moves, and write it to set the speed directly, as a character controller does:

```rust
@system
fn run(input: Input, players: (Player, Velocity)[]) {
    for (_, v) in players {
        v.linear = vec2(input.axis("run") * 220.0, v.linear.y);
    }
}
```

For pushes, use `Physics`: `apply_impulse(e, i)` changes velocity at once (a jump, a hit), and `apply_force(e, f)` acts over the frame (thrust, wind). Physics moves the `Transform`; writing a dynamic body's `Transform` yourself teleports it.

## Gravity

Gravity starts from `[physics]` in `drift.toml`, in pixels per second squared. The default is small for pixel units, so most games set it:

```toml
[physics]
gravity = [0, 900]
```

A top-down game sets `[0, 0]`. `Physics` changes it while running with `set_gravity`, and `RigidBody.gravity_scale` scales it for one body.

## Collisions and sensors

Collisions arrive as events, filtered by the components on the two entities. A system that takes one `Contact<A, B>` runs once for each collision that started this frame between an entity with `A` and one with `B`, with `a` and `b` in that order:

```rust
@system
fn take_hits(hit: Contact<Player, Spike>, health: Lookup<Health>) {
    if health.get(hit.a) is Some(h) {
        h.hp -= 1;
    }
}
```

Take a list, `Contact<Player, Spike>[]`, to see the frame's collisions at once.

A collider with `sensor: true` detects overlap without pushing anything: pickups, trigger zones, checkpoints. `Overlap<A, B>` is a sensor overlap that started this frame, whichever of the two is the sensor. `ContactEnd` and `OverlapEnd` report the ends, for "standing on" and "inside the zone" logic.

`Contact` also carries `point` and `normal`, the first contact point and the direction from `a` to `b`.

## Collision filtering

Each collider has a `category` and a `mask`, both bit sets. Two colliders collide when each one's category is in the other's mask:

```rust
// 1 = player, 2 = enemy, 4 = player bullet
BoxCollider { half_size: vec2(8.0, 8.0), category: 4, mask: 2, .. }   // bullets hit enemies only
```

The default category is 1 and the default mask is every bit.

## Queries

`Physics` asks the world about shapes without spawning anything:

- `raycast(origin, direction, distance)` returns the first hit as a `RayHit` with `hit`, `entity`, `point`, `normal` and `fraction`. Sensors count as hits.
- `overlap_point(p)` and `overlap_rect(r)` return the entities whose colliders cover a point or touch a rect.

```rust
@system
fn line_of_sight(_: Guard, eye: Transform, physics: Physics,
                 players: (Entity, Player, Transform)[]) {
    for (player, _, t) in players {
        let to = t.position - eye.position;
        let ray = physics.raycast(eye.position, to.normalized(), to.length());
        if ray.hit && ray.entity == player {
            log("spotted");
        }
    }
}
```

See [Engine Components](../reference/components.md#physics) and [Engine Resources](../reference/resources.md#physics) for every field and method.
