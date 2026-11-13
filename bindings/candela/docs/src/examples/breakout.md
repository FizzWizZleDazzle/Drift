# Breakout

Bounce the ball off the paddle and clear every brick. Where the ball lands on the paddle decides where it goes.

[Play it in the browser](../play/breakout/). Left and Right (or A and D, or the stick) move the paddle; Space, Up or a click launches the ball. Three balls per game; R starts over.

## What it shows

- Physics bodies with full bounce and no friction in a world without gravity
- A kinematic paddle that pushes the ball and is not pushed back
- Contact handlers for the paddle and the bricks, and a sensor that catches lost balls
- Keeping the ball's speed steady while physics handles the bounces
- States for serving, playing, winning and losing
- Bricks that pop in with chained tween delays, bursts in each brick's color
- Sounds made from samples, played at different pitches with `play_with`

## Run it

```sh
cd bindings/candela/examples/breakout
drift build --web
cd build/web && python3 -m http.server 8000
```

The example is self-contained: every image and sound is made in code.

## drift.toml

```toml
{{#include ../../../examples/breakout/drift.toml}}
```

## Steering the ball

`hit.a` is the ball and `hit.b` the paddle. Two lookups find their positions and the ball's velocity; the offset from the paddle's middle sets the new direction.

```rust
{{#include ../../../examples/breakout/main.cdl:paddle}}
```

## A steady speed

Physics keeps the bounces; this keeps the speed, and stops the ball from settling into a flat path that never comes back down.

```rust
{{#include ../../../examples/breakout/main.cdl:speed}}
```

## Losing a ball

A sensor below the paddle reports the ball passing through it, and the state goes back to serving or to the end.

```rust
{{#include ../../../examples/breakout/main.cdl:lose}}
```

## The whole game

```rust
{{#include ../../../examples/breakout/main.cdl}}
```
