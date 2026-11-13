# Engine Components

Components the engine draws and simulates. Use them like your own: put them in a spawn bundle, query them, look them up, and assign to them to change them. `Sprite`, `Rectangle`, `Circle`, `Text`, `Velocity`, `RigidBody`, the colliders and `Lifetime` bring a `Transform` when the entity has none. Build each with the fields you need and `..` for the defaults.

Some fields are changed by the engine as well (marked *engine*). A system sees the value from when it started; the engine's change lands after the system that ran before it.

## Placement

| Component | Fields |
|-----------|--------|
| `Transform` | `position`, `rotation` (radians), `scale` (1, 1). Relative to the parent when the entity has one. *Engine*: physics, `Velocity`, behaviors and tweens move it. `at(x, y)` builds one at a position |
| `GlobalTransform` | `position`, `rotation`, `scale` after every parent, from the engine's current transforms. Assign to it to move the entity to that place in world space; the engine rewrites the local `Transform` to match |
| `Parent` | `entity`, `keep_world` (false). Makes this entity's `Transform` relative to `entity`; despawning the parent despawns the child. With `keep_world`, the `Transform` is rewritten when the link is made so the entity stays where it is on screen. Inserting `Parent { entity: Entity {}, .. }` or removing `Parent` makes the entity a root again, where it is on screen. A parent holds up to 64 children |
| `Name` | `value`. Shown in the inspector and kept in scene files |

## Drawing

An entity draws one of `Sprite`, `Rectangle` or `Circle`, plus any `Text`.

| Component | Fields |
|-----------|--------|
| `Sprite` | `image` (a `Texture`, from `Assets`), `frame` (part of the image as a `Rect`; zero size for all of it), `anchor` (0.5, 0.5: the point placed at the position, and the pivot it rotates and scales around), `color` (tint), `flip_x`, `flip_y`, `z`, `layer`, `visible`, `blend`, `material`. *Engine*: `frame` under an `Animator`, `color` under a tween |
| `Rectangle` | `size`, `color`, `outline` (0 for filled), `corner_radius`, `anchor` (the pivot it turns around), `z`, `layer`, `visible` |
| `Circle` | `radius`, `color`, `outline`, `z`, `layer`, `visible`. A `Transform.scale` with uneven x and y draws an ellipse |
| `Text` | `value`, `size`, `color`, `align`, `valign`, `wrap` (width, 0 for none), `line_spacing`, `outline`, `outline_color`, `shadow_offset` (zero for none), `shadow_color`, `rich_text` (inline color tags), `font`, `z`, `layer`, `visible`. Holds up to 511 bytes |
| `Camera` | `zoom`, `rotation`, `active` (true), `order`, `viewport` (normalized rect, for split screen), `layer_mask` (`ALL_LAYERS`; build one with `layer_mask([0, 2])`), `target` (a render target texture), `clear`, `clear_color`. *Engine*: `zoom` under a tween, position under `CameraFollow`, `CameraBounds` and shake |
| `CameraFollow` | `target`, `smoothing`, `offset`, `dead_zone` |
| `CameraBounds` | `area`: the world rect the camera's view stays inside, such as the level. Centers the view on an axis where the area is smaller than it |
| `Tilemap` | `map`, `z`, `layer`, `colliders` (build static colliders from solid layers), `visible`. The map's top-left sits at the entity's `Transform`. Parallax comes from the map's layer settings |

## Motion and behavior

These run inside the engine, on its worker threads, so a game with many moving things does not spend script time moving them.

| Component | Fields |
|-----------|--------|
| `Velocity` | `linear` (pixels per second), `angular` (radians per second). Moves the entity; on a dynamic `RigidBody` it is the body's velocity (*engine*) |
| `Lifetime` | `seconds`. The entity despawns when it runs out. *Engine*: counts down |
| `Oscillate` | `amplitude` (`Vec2`), `frequency`, `phase`. Swings the entity around its position |
| `Follow` | `target`, `speed`, `distance`, `offset`. Moves toward another entity |
| `Orbit` | `center`, `point`, `radius`, `speed` (radians per second), `angle`. Circles another entity, or `point` while `center` is unset or gone |
| `Tween` | Built with `tween()`: `move_to`, `move_by`, `scale_to`, `rotate_to`, `rotate_by`, `fade_to`, `tint_to`, `zoom_to` (each `(target, seconds, Ease)`), `delay(seconds)`, `repeat(times)` or `yoyo(times)` (-1 forever). Inserting a changed tween restarts it. *Engine*: `finished` |
| `Animator` | `clips` (`AnimationClip { frames: Rect[], frame_seconds, looping }`), `clip` (set it to switch), `playing`. *Engine*: `frame`, `finished` |

## Physics

| Component | Fields |
|-----------|--------|
| `RigidBody` | `kind` (`BodyKind::Dynamic`), `fixed_rotation`, `gravity_scale`, `linear_damping`, `angular_damping`. Add a collider too |
| `BoxCollider` | `half_size`, `offset`, `sensor`, `density`, `friction`, `restitution`, `category` (1), `mask` (65535) |
| `CircleCollider` | `radius`, `offset`, and the rest as `BoxCollider` |
| `Joint` | `kind` (`JointKind::Revolute`), `target` (no entity: pinned to the world), `anchor`, `target_anchor`, `length`, `min_length`, `max_length`, `stiffness`, `damping`, `limit`, `lower`, `upper`, `motor`, `motor_speed`, `max_motor_force`, `axis`, `collide_connected`. See [Physics](../guides/physics.md#joints) |

A collider without a `RigidBody` is static. The body is created at the end of the frame the components are added; changing a `RigidBody` or collider field later makes it again with the new settings.

## Effects, light and sound

| Component | Fields |
|-----------|--------|
| `ParticleEmitter` | `rate`, `lifetime_min`/`max`, `speed_min`/`max`, `angle_min`/`max`, `size_start`, `size_end`, `size_start_max`, `size_end_max` (0 for the same as the minimum), `color_start`, `color_end`, `gravity`, `drag`, `max_particles`, `blend`, `texture`, `frame` (part of the texture), `shape`, `shape_radius`, `shape_extents`, `duration`, `looping`, `z`, `playing`, `rotation_min`/`max`, `spin_min`/`max` (radians per second), `inherit_velocity`, `local` (particles move with the emitter), `prewarm` (seconds simulated up front), `bounds` (a rect particles stop at), `seed`, `bursts` (`Burst { time, count, cycles, interval }`) |
| `Trail` | `width`, `lifetime` (seconds a point lasts), `color_start`, `color_end`, `min_distance`, `max_points`, `z` |
| `PointLight` | `color`, `intensity`, `radius`, `falloff` (1 fades evenly; more keeps a tighter core), `shadows`, `layer_mask` (cameras sharing a layer see it). Lights shine when drift.toml has a `[lighting]` section |
| `SpotLight` | As `PointLight`, plus `direction` (radians, added to the entity's rotation), `angle` (the whole cone), `softness` (0 hard edge, 1 fades from the middle) |
| `LightOccluder` | `shape` (`OccluderShape`), `half_size` (`Box`), `radius` (`Circle`), `points` (`Polygon`, up to 16), `offset`, `shadow_self` (false keeps its own area lit), `layer_mask`. Turns and scales with the entity |
| `AudioSource` | `sound`, `bus`, `volume`, `pitch`, `looping`, `autoplay` (true), `spatial`, `min_distance` (64), `max_distance` (1024), `rolloff` (`Rolloff::Linear` or `Inverse`), `priority`, `play` (set it to start the sound again). Changing `sound` starts the new one |
| `AudioListener` | `active` (true). Spatial sounds are heard from the first active one |

A camera that follows the player and stops at the level's edges:

```rust
cmd.spawn([at(0.0, 0.0), Camera { zoom: 2.0, .. },
           CameraFollow { target: player, smoothing: 6.0, .. },
           CameraBounds { area: rect(0.0, 0.0, 3200.0, 720.0) }]);
```

An emitter that fires a ring of 30 sparks every half second, three times, and trickles in between:

```rust
cmd.spawn([at(400.0, 300.0), ParticleEmitter {
    rate: 10.0, shape: EmitShape::Ring, shape_radius: 24.0, spin_min: -4.0, spin_max: 4.0,
    bursts: [Burst { count: 30, cycles: 3, interval: 0.5, .. }], ..
}]);
```

A torch that lights the room, and a wall that casts a shadow from its collider:

```rust
cmd.spawn([at(300.0, 200.0), PointLight { color: rgb(255, 180, 100), radius: 240.0, .. }]);
cmd.spawn([at(360.0, 220.0), Rectangle { size: vec2(20.0, 120.0), .. }, BoxCollider { half_size: vec2(10.0, 60.0), .. },
           LightOccluder { shape: OccluderShape::FromCollider, .. }]);
```
