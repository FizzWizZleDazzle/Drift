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

An `Animator` plays named clips on the entity's `Sprite`. A clip is a sprite sheet, the frames to show from it, and how long each one shows: one number for every frame, or a list with one duration per frame. `clip(sheet, frames, seconds)` makes one, and `.once()` makes it play one time instead of looping. `play` names the clip to start with.

```rust
@system(schedule: Schedule::Startup)
fn spawn_hero(cmd: Commands, assets: Assets) {
    let hero = assets.spritesheet(texture("assets/hero.png"), 32.0, 32.0);
    let slash = assets.spritesheet(texture("assets/slash.png"), 48.0, 32.0);
    cmd.spawn([at(100.0, 100.0), Sprite { image: hero.texture, .. }, Hero {}, Animator {
        clips: {
            "idle":   clip(hero, [0, 1, 2, 3], 0.15),
            "run":    clip(hero, range(8, 14), 0.08),
            "attack": clip(slash, [0, 1, 2], [0.05, 0.05, 0.2]).once()
        },
        play: "idle",
        ..
    }]);
}
```

Each clip carries its sheet, so switching to `attack` shows the slash sheet's texture and frame, and switching back shows the hero's. Frames count from 0 at the sheet's top-left, as in [sprite sheets](sprites.md#sprite-sheets).

Switch clips by name. `play` changes nothing when that clip is already playing, so a system can call it every frame:

```rust
@system
fn hero_anim(hero: (Hero, Velocity, Animator, Sprite), input: Input) {
    let (_, v, anim, sprite) = hero;
    if input.key_pressed(Key::X) {
        anim.play_once("attack", "idle");   // once, then back to idle
    } else if !anim.once {
        if v.linear.x != 0.0 {
            anim.play("run");
        } else {
            anim.play("idle");
        }
    }
    if v.linear.x != 0.0 {
        sprite.flip_x = v.linear.x < 0.0;
    }
}
```

`play_once(name, then)` plays a clip one time, even a looping one, and then switches to `then`; an empty `then` holds the last frame. A clip made with `.once()` ends the same way when `play` starts it. `finished("attack")` is true once that clip has played to its end, until another clip starts. To act on the end without checking every frame, take the `AnimationFinished` message, which names the entity and the clip:

```rust
@system
fn remove_fallen(done: AnimationFinished, cmd: Commands) {
    if done.clip == "die" {
        cmd.despawn(done.entity);
    }
}
```

The animator also tells where it is: `play` is the clip playing, `frame` the index into that clip's frames and `time` the seconds into the clip. Setting `frame` or `time` jumps there. `speed` scales time, so 2.0 plays twice as fast, and `playing: false` pauses.

A name the animator does not have is an error, never ignored: `play` and `play_once` raise `drift_unknown_clip` with the names it has, and an unknown name written into the `play` field logs the same error. `clip` raises `drift_bad_clip` for a frame outside the sheet or a duration list whose length does not match the frames.

### Aseprite files

`assets.aseprite(path)` loads an Aseprite file (.ase, .aseprite) as a sheet, and `sheet.clip(tag)` turns one of its tags into a clip: its frames in the order the tag plays them (forward, reverse or ping-pong), each with its own duration from the file. A tag set to repeat once plays once; any other loops.

```rust
let knight = assets.aseprite("assets/knight.aseprite");
cmd.spawn([at(200.0, 200.0), Sprite { image: knight.texture, .. }, Animator {
    clips: { "idle": knight.clip("idle"), "run": knight.clip("run"), "die": knight.clip("die") },
    play: "idle",
    ..
}]);
```

A tag the file does not have raises `drift_unknown_clip` with the tags it has.

### Updating a game written with numbered clips

Clips used to be lists of frame rects with one frame time, held in a list and picked by index. They are now sheet frames with their durations, held by name. `sheet.tag(name)` and `sheet.frames(first, count)` are gone; `clip(sheet, frames, seconds)` and `sheet.clip(tag)` make clips, and `Spritesheet.source` is now `id`.

Before:

```rust
Animator { clips: [
    AnimationClip { frames: sheet.frames(0, 4), frame_seconds: 0.15, looping: true },
    AnimationClip { frames: sheet.frames(8, 6), frame_seconds: 0.08, looping: true },
], .. }
// ...
anim.clip = 1;
if anim.finished { ... }
```

After:

```rust
Animator {
    clips: { "idle": clip(sheet, range(0, 4), 0.15), "run": clip(sheet, range(8, 14), 0.08) },
    play: "idle",
    ..
}
// ...
anim.play("run");
if anim.finished("run") { ... }
```

## Parts that move together

A child entity moves with its parent: spawn it with `cmd.spawn_child(parent, bundle)` and its `Transform` counts from the parent. A turret on a tank, a sword in a hand and a whole jointed character are built this way; see [Sprites, Shapes and Images](sprites.md#parts-that-move-together).
