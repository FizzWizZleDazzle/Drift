# Movement

Things move in three ways: the engine moves them by their `Velocity`, a tween moves them along a path you describe once, or your system writes their `Transform` every frame. Use the first two where you can: the engine does the work, and your systems only decide where things go. Components such as `Oscillate`, `Follow` and `Orbit` cover common motion with no system at all, and the [drift/control](../reference/control.md) module has ready-made player controllers.

## Velocity

`Velocity` moves an entity every frame: `linear` in pixels per second and `angular` in radians per second. A drifting cloud or a spinning coin needs no system at all:

```rust
cmd.spawn([at(100.0, 80.0), Sprite { image: art.cloud, .. }, Velocity { linear: vec2(12.0, 0.0), .. }]);
cmd.spawn([at(300.0, 200.0), Sprite { image: art.gem, .. }, Velocity { angular: 2.0, .. }]);
```

A player steers by setting the velocity from input:

```rust
@component(requires: [Velocity])
struct Player { speed: float = 200.0 }

@system
fn walk(row: (Player, Velocity), input: Input) {
    let (player, v) = row;
    v.linear = input.axis2d("move") * player.speed;
}
```

For a heavier feel, ease toward the wanted velocity instead of jumping to it:

```rust
let want = input.axis2d("move") * player.speed;
v.linear = v.linear.lerp(want, min(1.0, 10.0 * time.dt));
```

On an entity with a dynamic `RigidBody`, `Velocity` is the body's velocity and walls stop it; see [Collisions and Physics](collisions.md). Without a body, nothing stops it: keep it on screen yourself, as the [Star Catcher](../getting-started/tutorial.md) ship does.

## Toward a target, and facing it

```rust
@system
fn chase(row: (Enemy, Transform, Velocity), players: (Player, Transform)[]) {
    let (enemy, t, v) = row;
    for (_, target) in players {
        let to = target.position - t.position;
        v.linear = to.normalized() * enemy.speed;
        t.rotation = to.angle();          // face where it is going
    }
}
```

`normalized()` turns an offset into a direction of length 1, `angle()` gives its angle in radians, and `from_angle(a)` turns an angle back into a direction. `rotated`, `perp`, `dot`, `distance` and `clamp_length` cover the rest of the usual vector work; see [`Vec2`](../reference/values.md#vec2).

## Moving yourself

Writing `Transform.position` puts an entity exactly where you say. Multiply speeds by `time.dt`, the seconds since the last frame, so movement does not depend on the frame rate:

```rust
@system
fn bob(pickup: (Pickup, Transform), time: Time) {
    let (_, t) = pickup;
    t.position = vec2(t.position.x, 200.0 + sin(time.elapsed * 3.0) * 6.0);
}
```

Games on a grid move in whole cells on a timer or a key press. [Snake](../examples/snake.md) keeps the snake's cells in a resource and moves one cell per tick of a `Timer`; [Roguelike](../examples/roguelike.md) moves one cell per key press and turns cells into positions in a small per-entity system.

## Tweens

A `Tween` animates an entity from where it is to a target, over time, along an easing curve. Build one with `tween()` and chain steps; the steps run one after another, and the engine runs them:

```rust
// Pop in, wait, then fade out. Lifetime removes the entity afterwards.
cmd.spawn([
    at(x, y),
    Sprite { image: art.star, .. },
    tween().scale_to(vec2(1.4, 1.4), 0.1, Ease::OutQuad)
           .scale_to(vec2(1.0, 1.0), 0.3, Ease::OutBack)
           .delay(0.5)
           .fade_to(0.0, 0.25, Ease::Linear),
    Lifetime { seconds: 1.2 },
]);
```

| Step | Animates |
|------|----------|
| `move_to`, `move_by` | `Transform.position` |
| `scale_to` | `Transform.scale` |
| `rotate_to`, `rotate_by` | `Transform.rotation` |
| `fade_to` | The sprite's alpha, 0 to 1 |
| `tint_to` | The sprite's whole color |
| `zoom_to` | `Camera.zoom` |
| `delay` | Nothing, for a pause between steps |

`repeat(times)` plays the chain again from the start; `yoyo(times)` plays it forward, then backward. `-1` repeats forever, so `tween().move_by(vec2(0.0, -6.0), 0.5, Ease::InOutSine).yoyo(-1)` bobs a pickup up and down for good. The [Effects Showcase](../examples/effects.md) shows every family of easing curves side by side.

An entity holds one tween. Inserting a changed tween with `cmd.insert` starts it again from the entity's current values. `effects.set_tween_speed(s)` scales time for every tween, and 0 freezes them.

## Motion components

Four components cover common motion:

- `Lifetime { seconds }` despawns the entity when the time runs out, with its children. Use it for bullets, floating labels and debris.
- `Oscillate { amplitude, frequency, phase }` swings the entity around where it is, on top of any other motion. `amplitude` is a `Vec2`, so `vec2(0.0, 4.0)` bobs and `vec2(30.0, 0.0)` patrols.
- `Follow { target, speed, distance, offset }` moves straight toward another entity's position plus `offset`, stopping `distance` short. Nothing happens while the target is gone.
- `Orbit { center, point, radius, speed, angle }` circles another entity, or `point` while `center` is unset or gone, `radius` pixels out at `speed` radians per second. Spread the `angle`s of several orbiters to make a ring of shields.

```rust
// A drone that trails the player and hovers as it goes.
cmd.spawn([at(0.0, 0.0), Sprite { image: texture("assets/drone.png"), .. },
           Follow { target: player, speed: 120.0, distance: 40.0, .. },
           Oscillate { amplitude: vec2(0.0, 3.0), frequency: 2.0, .. }]);
```

For a camera that tracks the player, use `CameraFollow`, which smooths the motion; see [Cameras](cameras.md).

See [Engine Components](../reference/components.md#oscillate) for every field.

## Animation frames

A spritesheet cuts a texture into frames. `Assets` makes one from a grid with `spritesheet(texture, frame_w, frame_h)`, or loads an Aseprite file with `aseprite(path)`, which brings its frames and tags:

```rust
@system(schedule: Schedule::Startup)
fn spawn_hero(cmd: Commands, assets: Assets) {
    let sheet = assets.aseprite("assets/hero.aseprite");
    cmd.spawn([
        at(200.0, 200.0),
        Sprite { image: sheet.texture, .. },
        Animator { clips: [
            AnimationClip { frames: sheet.tag("idle"), frame_seconds: 0.15, looping: true },
            AnimationClip { frames: sheet.tag("run"), frame_seconds: 0.08, looping: true },
        ], .. },
        Hero {},
    ]);
}
```

For a grid sheet, take frames with `frames(first, count)`, `frame(col, row)` or `frame_at(i)`. `sheet.clip("run")` turns an Aseprite tag into a whole `AnimationClip`, timed by the file's frame durations (their average, since a clip has one frame time).

The `Animator` sets the sprite's `frame` as time passes. Switch clips by writing `clip`, the index into `clips`; the new clip starts from its first frame:

```rust
@system
fn hero_anim(heroes: (Hero, Velocity, Animator, Sprite)[]) {
    for (_, v, anim, sprite) in heroes {
        let running = v.linear.x != 0.0;
        let want = 0;
        if running {
            want = 1;
        }
        if anim.clip != want {
            anim.clip = want;
        }
        if running {
            sprite.flip_x = v.linear.x < 0.0;
        }
    }
}
```

A clip with `looping: false` stops on its last frame and sets `finished`, which a system reads to move on from an attack or a death animation. `playing: false` pauses the animator.

## Parts that move together

A child entity moves with its parent: spawn it with `cmd.spawn_child(parent, bundle)` and its `Transform` counts from the parent. A turret on a tank, a sword in a hand and a whole jointed character are built this way; see [Sprites, Shapes and Images](sprites.md#parts-that-move-together).
