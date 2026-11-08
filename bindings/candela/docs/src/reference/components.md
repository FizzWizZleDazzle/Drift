# Engine Components

Components the engine draws and simulates. Use them like your own: put them in a spawn bundle, query them, look them up, and name them in `writes:` to change them. Build each with the fields you need and `..` for the defaults.

Some fields are changed by the engine as well (marked *engine*). A system sees the value from when it started; the engine's change lands after the system that ran before it.

## Placement

| Component | Fields |
|-----------|--------|
| `Transform` | `position`, `rotation` (radians), `scale` (1, 1). Relative to the parent when the entity has one. *Engine*: physics, `Velocity`, behaviors and tweens move it. `at(x, y)` builds one at a position |
| `GlobalTransform` | `position`, `rotation`, `scale` after every parent. Read-only; the engine computes it |
| `Parent` | `entity`. Makes this entity's `Transform` relative to it; despawning the parent despawns the child |
| `Name` | `value`. Shown in the inspector and kept in scene files |

## Drawing

An entity draws one of `Sprite`, `Rectangle` or `Circle`, plus any `Text`.

| Component | Fields |
|-----------|--------|
| `Sprite` | `image` (a `Texture`, from `drift::Assets`), `frame` (part of the image as a `Rect`; zero size for all of it), `anchor` (0.5, 0.5: the point placed at the position), `color` (tint), `flip_x`, `flip_y`, `z`, `layer`, `visible`, `blend`, `material`. *Engine*: `frame` under an `Animator`, `color` under a tween |
| `Rectangle` | `size`, `color`, `outline` (0 for filled), `corner_radius`, `anchor`, `z`, `layer`, `visible` |
| `Circle` | `radius`, `color`, `outline`, `z`, `layer`, `visible` |
| `Text` | `value`, `size`, `color`, `align`, `valign`, `wrap` (width, 0 for none), `line_spacing`, `outline`, `outline_color`, `shadow_offset` (zero for none), `shadow_color`, `rich_text` (inline color tags), `font`, `z`, `layer`, `visible`. Holds up to 511 bytes |
| `Camera` | `zoom`, `rotation`, `active`, `order`, `viewport` (normalized rect, for split screen), `layer_mask`, `target` (a render target texture), `clear`, `clear_color`. *Engine*: `zoom` under a tween, position under `CameraFollow` and shake |
| `CameraFollow` | `target`, `smoothing`, `offset`, `dead_zone` |
| `Tilemap` | `map`, `z`, `layer`, `colliders` (build static colliders from solid layers). Parallax comes from the map's layer settings |

## Motion and behavior

These run inside the engine, on its worker threads, so a game with many moving things does not spend script time moving them.

| Component | Fields |
|-----------|--------|
| `Velocity` | `linear` (pixels per second), `angular` (radians per second). Moves the entity; on a dynamic `RigidBody` it is the body's velocity (*engine*) |
| `Lifetime` | `seconds`. The entity despawns when it runs out. *Engine*: counts down |
| `Oscillate` | `amplitude` (`Vec2`), `frequency`, `phase`. Swings the entity around its position |
| `Follow` | `target`, `speed`, `distance`, `offset`. Moves toward another entity |
| `Tween` | Built with `tween()`: `move_to`, `move_by`, `scale_to`, `rotate_to`, `rotate_by`, `fade_to`, `tint_to`, `zoom_to` (each `(target, seconds, Ease)`), `delay(seconds)`, `repeat(times)` or `yoyo(times)` (-1 forever). Inserting a changed tween restarts it. *Engine*: `finished` |
| `Animator` | `clips` (`AnimationClip { frames: Rect[], frame_seconds, looping }`), `clip` (set it to switch), `playing`. *Engine*: `frame`, `finished` |

## Physics

| Component | Fields |
|-----------|--------|
| `RigidBody` | `kind` (`BodyKind::Dynamic`), `fixed_rotation`, `gravity_scale`, `linear_damping`, `angular_damping`. Add a collider too |
| `BoxCollider` | `half_size`, `offset`, `sensor`, `density`, `friction`, `restitution`, `category`, `mask` |
| `CircleCollider` | `radius`, `offset`, and the rest as `BoxCollider` |

A collider without a `RigidBody` is static. Body settings are read when the body is created, at the end of the frame the components are added; to change them later, remove and insert the `RigidBody`.

## Effects, light and sound

| Component | Fields |
|-----------|--------|
| `ParticleEmitter` | `rate`, `lifetime_min`/`max`, `speed_min`/`max`, `angle_min`/`max`, `size_start`, `size_end`, `color_start`, `color_end`, `gravity`, `drag`, `max_particles`, `blend`, `texture`, `shape`, `shape_radius`, `shape_extents`, `duration`, `looping`, `z`, `playing` |
| `Trail` | `width`, `lifetime` (seconds a point lasts), `color_start`, `color_end`, `min_distance`, `max_points`, `z` |
| `PointLight` | `color`, `intensity`, `radius`, `falloff`, `shadows`. Lights shine when drift.toml has a `[lighting]` section |
| `SpotLight` | As `PointLight`, plus `direction`, `angle`, `softness` |
| `LightOccluder` | `shape` (`OccluderShape`), `half_size`, `radius` |
| `AudioSource` | `sound`, `bus`, `volume`, `pitch`, `looping`, `autoplay`, `spatial`, `min_distance`, `max_distance` |
| `AudioListener` | none. Spatial sounds are heard from this entity, or from the main camera without one |
