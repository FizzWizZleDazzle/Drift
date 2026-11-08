# Flappy Bird

Flap through the gaps between the pipes. Space, Up, a click or the gamepad's A button flaps; after a crash, flap to play again. The game runs on the desktop with `drift run` and in the browser as a page.

It shows tag components, an enum in a resource and a component, settings from `drift.toml`, sensor overlaps as messages, kinematic bodies moved by `Velocity`, `Lifetime`, sounds, and HUD text.

```sh
cd bindings/candela/examples/flappy_bird
drift run
drift run --set flappy.pipe_gap=200   # an easier game for one run
```

## drift.toml

```toml
{{#include ../../../examples/flappy_bird/drift.toml}}
```

## main.cdl

```rust
{{#include ../../../examples/flappy_bird/main.cdl}}
```

## How it fits together

- `setup` binds the `flap` action, loads the art and sounds into the `Art` resource, and spawns the background, two ground strips, the bird, the score and the two banners.
- `flap` turns a press into a flap, starts the round from the ready screen, and resets the round after a crash.
- `fall` applies the game's own gravity to the bird and tilts it; physics gravity is off in `drift.toml`. Hitting the ground ends the round.
- `spawn_pipes` adds a pair of kinematic pipes on a timer. Their `Velocity` moves them and their `Lifetime` removes them once they have crossed the screen.
- `hit_pipe` reads `drift::Overlap<Bird, Pipe>`: the bird's collider is a sensor, so touching a pipe is an overlap, not a collision.
- `score`, `scroll_ground` and `hud` keep the score, the moving ground and the text and banners up to date.
