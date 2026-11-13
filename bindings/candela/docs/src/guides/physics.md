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

The body is created at the end of the frame its components are added. Changing a field of the `RigidBody` or the collider later makes the body again with the new settings, where it is and at the speed it has: set `gravity_scale` to 0 for a power-up that makes the player float, or switch `kind` to `Static` to freeze a crate in place.

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

For pushes, use `Physics`: `apply_impulse(e, i)` changes velocity at once (a jump, a hit), and `apply_force(e, f)` acts over the frame (thrust, wind). `apply_torque` and `apply_angular_impulse` spin a body. Physics moves the `Transform`; writing a dynamic body's `Transform` yourself teleports it.

```rust
@system
fn knockback(hit: Contact<Player, Enemy>, physics: Physics) {
    physics.apply_impulse(hit.a, hit.normal * -40000.0);   // away from the enemy
}
```

Mass is density times area in pixels, so a 32 by 32 box of density 1 weighs 1024: impulses and forces in pixel units are large numbers. An impulse of `mass * speed` changes the speed by `speed` pixels per second.

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

`Contact` also carries `point` and `normal`, the first contact point and the direction from `a` to `b`. A player whose contact normal with the ground points down, `hit.normal.y > 0.5`, has landed on top of it rather than hit it from the side.

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
- `raycast_mask(origin, direction, distance, mask)` sees only colliders whose `category` is in `mask`, such as walls for line of sight.
- `overlap_point(p)` returns the entities whose colliders contain a point: what is under the mouse.
- `overlap_circle(center, radius)` returns the entities whose colliders touch a circle: an explosion's reach. `overlap_circle_mask` takes a mask too.
- `overlap_rect(r)` returns the entities whose colliders' bounding boxes touch a rect.

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

`set_debug_draw(true)` outlines every collider over the game.

### Without colliders

Things that only need "what is near here", such as pickups that home in on the nearest player or enemies that alert their neighbours, do not need physics bodies. Give them a `SpatialBounds` and ask the `Spatial` resource:

```rust
@system(schedule: Schedule::Startup)
fn spawn_bats(cmd: Commands) {
    cmd.spawn([at(300.0, 120.0), Bat { .. }, SpatialBounds { half_size: vec2(8.0, 8.0), .. }]);
}

@system
fn alert(_: Bat, t: Transform, me: Entity, spatial: Spatial, cmd: Commands) {
    for other in spatial.in_radius(t.position, 120.0) {
        if other.id != me.id {
            cmd.insert(other, Alerted {});
        }
    }
}
```

`in_rect(r)` and `in_radius(center, radius)` return the entities whose bounds touch the area, and `nearest(p, max_distance, ignore)` the closest one, or `Entity {}`. The index is rebuilt at the start of every frame from where the entities were then.

## Joints

A `Joint` joins an entity's body to another entity's body, or pins it to the world when `target` is left empty. Both entities need a body; the joint is made once they exist.

```rust
@system(schedule: Schedule::Startup)
fn pendulum(cmd: Commands) {
    // A weight on a 60 pixel rod, hanging from the point (500, 50).
    cmd.spawn([
        at(560.0, 50.0),
        Circle { radius: 8.0, .. },
        RigidBody { .. },
        CircleCollider { radius: 8.0, .. },
        Joint { kind: JointKind::Distance, target_anchor: vec2(500.0, 50.0), .. },
    ]);
}
```

| `JointKind` | Does |
|-------------|------|
| `Distance` | Keeps the anchors `length` apart (0: the distance they start at). With `stiffness` it is a spring; with `max_length` above `min_length` it is a rope that can go slack |
| `Revolute` | A hinge: the anchors stay together and the bodies turn about them. `limit` with `lower` and `upper` (radians) bounds the angle; `motor` turns it, as on a wheel or a door |
| `Prismatic` | A slider along `axis`: lifts, pistons. `limit` bounds the travel |
| `Weld` | Glues the bodies together; with `stiffness` it bends |

`anchor` is on this entity and `target_anchor` on the target, each in its body's local space (without a target, `target_anchor` is a point in the world). `collide_connected` lets the two joined bodies collide. Changing a field makes the joint again; removing the component removes it. A chain is one joint per link, each targeting the link before it.

See [Engine Components](../reference/components.md#physics) and [Engine Resources](../reference/resources.md#physics) for every field and method.
