# Tweening

Tweens animate a value from where it is now to a target over time, shaped by an easing curve. Use them for UI pops, doors sliding open, camera zoom punches, fades, or walking a character along a path.

**Headers:** `#include <drift/gameplay/Tween.hpp>`, `#include <drift/gameplay/Easing.hpp>`

**Plugin:** `TweenPlugin` (included in `DefaultPlugins`)

## Starting a tween

The `Tweens` resource creates tweens. `on(entity)` returns a builder; add steps, then call `start()`.

```cpp
void openDoor(ResMut<Tweens> tweens, Res<Doors> doors) {
    tweens->on(doors->left).moveBy({-64.f, 0.f}, 0.4f, Ease::OutCubic).start();
}
```

`start()` returns a `TweenId`. A builder that is never started does nothing and logs a warning.

Property steps start from the entity's value at the moment the step begins, so a tween always continues smoothly from wherever the entity is.

| Step | Animates |
|------|----------|
| `moveTo(Vec2, duration, ease)` / `moveBy(...)` | `Transform2D::position` |
| `rotateTo(radians, ...)` / `rotateBy(...)` | `Transform2D::rotation` |
| `scaleTo(Vec2, ...)` | `Transform2D::scale` |
| `tintTo(Color, ...)` | `Sprite::tint`, all four channels |
| `fadeTo(alpha, ...)` | `Sprite::tint.a`, alpha given as 0..1 |
| `zoomTo(zoom, ...)` | `Camera::zoom` |
| `value(from, to, duration, ease, fn)` | Any float: `fn(float)` receives the eased value each frame |
| `delay(seconds)` | Waits |
| `call(fn)` | Runs `fn` when the sequence reaches it |

`moveBy` and `rotateBy` are relative to the value when the step begins.

## Sequences and loops

Steps run one after another:

```cpp
tweens->on(coin)
    .scaleTo({1.4f, 1.4f}, 0.1f, Ease::OutQuad)
    .scaleTo({1.f, 1.f}, 0.3f, Ease::OutBack)
    .delay(0.5f)
    .fadeTo(0.f, 0.25f)
    .start();
```

To run two animations at the same time, start two tweens on the same entity; each animates its own property.

`loop(times, mode)` repeats the whole sequence. `times` counts total plays and `-1` repeats forever.

- `TweenLoop::Restart` jumps back to the start values each cycle.
- `TweenLoop::Yoyo` plays forward, then backward. `yoyo(times)` is shorthand; `yoyo(2)` goes there and back once.

```cpp
// Bob up and down forever.
tweens->on(pickup).moveBy({0.f, -6.f}, 0.5f, Ease::InOutSine).yoyo().start();
```

A tween with no entity, from `tweens->sequence()`, can hold `value`, `delay` and `call` steps, for example to fade music volume or drive a shader parameter.

## Controlling tweens

```cpp
tweens->stop(id);            // stop where it is
tweens->stop(id, true);      // jump to the end values and send TweenFinished
tweens->stopAll(entity);     // every tween on this entity
tweens->pause(id);
tweens->resume(id);
tweens->isActive(id);
tweens->timeScale = 0.f;     // freeze all tweens (pause menu), 1 = normal speed
```

A tween whose entity is despawned, or lacks the component it animates, ends quietly.

## Completion events

When a tween finishes all its cycles it sends `TweenFinished { TweenId id; EntityId entity; }`. Like all events it is readable the frame after it is sent.

```cpp
void despawnFaded(EventReader<TweenFinished> done, Res<Dying> dying, Commands& cmd) {
    for (const auto& ev : done)
        if (dying->contains(ev.entity)) cmd.entity(ev.entity).despawn();
}
```

## Timing

`TweenPlugin` advances tweens once per frame in `PostUpdate`, after `Update` systems have run, so a tweened property wins over a value an `Update` system wrote in the same frame. Callbacks from `value` and `call` run inside that system while no other system runs; they may start new tweens, which begin on the next frame. Do not draw or touch textures from them.

To drive tweens yourself (a custom loop, a test), call `Tweens::update(dt, world, events)`.

## Easing curves

`ease(Ease, t)` maps `t` in 0..1 to progress. Every curve returns exactly 0 at `t = 0` and 1 at `t = 1`.

| Family | Values | Character |
|--------|--------|-----------|
| `Linear` | | Constant speed |
| `Sine`, `Quad`, `Cubic`, `Quart`, `Quint`, `Expo`, `Circ` | `In*`, `Out*`, `InOut*` | Increasingly sharp acceleration |
| `Back` | `InBack`, `OutBack`, `InOutBack` | Pulls back past the start or overshoots the end |
| `Elastic` | `InElastic`, `OutElastic`, `InOutElastic` | Wobbles like a plucked string |
| `Bounce` | `InBounce`, `OutBounce`, `InOutBounce` | Bounces off the end |
| `Spring` | | Overshoots and settles like a damped spring |

`In` curves start slow, `Out` curves end slow, `InOut` do both. `OutQuad` is a good default for movement and `OutBack` for things popping in.

Easing works without tweens too:

```cpp
float k = ease(Ease::InOutSine, timer.fraction());
Vec2 p = lerp(from, to, k);
```
