# Animation, Tweens and Behaviors

Motion that follows a rule belongs in a component, not a system: the engine runs these components itself, on its worker threads, so a screen full of moving things costs your scripts nothing. Use a system only for the decisions, such as which clip to play or where to send something.

## Velocity

`Velocity` moves an entity every frame: `linear` in pixels per second and `angular` in radians per second. A spinning coin or a drifting cloud needs no system at all:

```rust
cmd.spawn([at(100.0, 80.0), Sprite { image: texture("assets/cloud.png"), .. }, Velocity { linear: vec2(12.0, 0.0), .. }]);
cmd.spawn([at(300.0, 200.0), Sprite { image: texture("assets/gem.png"), .. }, Velocity { angular: 2.0, .. }]);
```

On an entity with a dynamic `RigidBody`, `Velocity` is the body's velocity; see [Physics](physics.md).

## Sprite animation

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

## Tweens

A `Tween` animates an entity from where it is now to a target, over time, along an easing curve. Build one with `tween()` and chain steps; the steps run one after another:

```rust
// Pop, then fade out and vanish.
cmd.spawn([
    at(x, y),
    Sprite { image: texture("assets/star.png"), .. },
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

`repeat(times)` plays the whole chain again from the start; `yoyo(times)` plays it forward, then backward. `-1` repeats forever, so `tween().move_by(vec2(0.0, -6.0), 0.5, Ease::InOutSine).yoyo(-1)` bobs a pickup up and down for good.

An entity holds one `Tween`. Inserting a changed tween restarts it from the entity's current values. `finished` turns true when a tween that does not repeat has played. `Effects.set_tween_speed(s)` scales time for every tween, and 0 freezes them for a pause menu.

`ease(e, t)` gives the same curves for your own calculations; see [Values and Enums](../reference/values.md) for the list.

## Behaviors

Three components cover common motion:

- `Lifetime { seconds }` despawns the entity when the time runs out, with its children. Use it for bullets, floating labels and debris.
- `Oscillate { amplitude, frequency, phase }` swings the entity around where it is, on top of any other motion. `amplitude` is a `Vec2`, so `vec2(0.0, 4.0)` bobs and `vec2(30.0, 0.0)` patrols.
- `Follow { target, speed, distance, offset }` moves straight toward another entity's position plus `offset`, stopping `distance` short. Nothing happens while the target is gone.

```rust
// A drone that trails the player and hovers as it goes.
cmd.spawn([at(0.0, 0.0), Sprite { image: texture("assets/drone.png"), .. },
           Follow { target: player, speed: 120.0, distance: 40.0, .. },
           Oscillate { amplitude: vec2(0.0, 3.0), frequency: 2.0, .. }]);
```

For a camera that tracks the player, use `CameraFollow`, which smooths the motion; see [Sprites, Shapes and Cameras](rendering.md#cameras).

See [Engine Components](../reference/components.md#motion-and-behavior) for every field.
