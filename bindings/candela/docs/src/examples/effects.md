# Effects Showcase

Particles, bursts, post effects, a trail and tweens on one screen, each one switchable, to see what the settings do.

[Play it in the browser](../play/effects/). Keys 1 to 5 switch the five emitters. Click for fireworks; Space flashes the screen. B, V, C and P switch bloom, vignette, CRT and pixelate.

## What it shows

- `ParticleEmitter` settings: rate, lifetime, speed, angle, size, color, gravity, drag, blend and emit shapes
- Switching an emitter by writing `playing`
- `Effects.burst` and `Effects.flash`
- Post effects kept in a resource and switched with `set_enabled`
- A `Trail` behind a tweened comet
- Every family of `Ease` curves side by side, and a chain of tween steps

## Run it

```sh
cd bindings/candela/examples/effects
drift build --web
cd build/web && python3 -m http.server 8000
```

The example is self-contained: every image and sound is made in code.

## drift.toml

```toml
{{#include ../../../examples/effects/drift.toml}}
```

## Emitters

Fire glows because of additive blending and rises against a little negative gravity; smoke is slow, large and slowed by drag; the fountain is fast and pulled down; snow starts along a box; the ring starts on a circle.

```rust
{{#include ../../../examples/effects/main.cdl:fire}}
```

## Post effects

Each call on `Effects` adds an effect and returns an `Effect` handle. Keep the handles in a resource to switch them later.

```rust
{{#include ../../../examples/effects/main.cdl:post}}
```

## Tweens

Each square travels the same distance in the same time with a different curve. The sprite runs a chain of steps, and the comet a loop of moves with a trail behind it.

```rust
{{#include ../../../examples/effects/main.cdl:easing}}
```

## The whole game

```rust
{{#include ../../../examples/effects/main.cdl}}
```
