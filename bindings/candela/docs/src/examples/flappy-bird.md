# Flappy Bird

Flap through the gaps between the pipes. Space, Up, a click or the gamepad's A button flaps; after a crash, flap to play again. The game runs on the desktop with `drift run` and in the browser as a page.

It shows a game state with `on_enter` and `in_state`, per-entity systems and a collision handler, resources that load their own assets, settings and input bindings from `drift.toml`, kinematic bodies moved by `Velocity`, `Lifetime`, sounds and HUD text.

```sh
cd bindings/candela/examples/flappy_bird
drift run
drift run --set flappy.pipe_gap=200   # an easier game for one run
drift build --web                     # the page, in build/web
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

- `Phase` is the game's state: `Ready` before the first flap, `Playing`, and `Over` after a crash. `reset` runs on entering `Ready`, `start` on entering `Playing`, and `crash` on entering `Over`.
- `flap` runs once per bird when the `flap` action (bound in `[input]`) is pressed, and starts the round. `restart` goes back to `Ready` once the round has been over for half a second.
- `fall` applies the game's own gravity to the bird and tilts it; physics gravity is off in `drift.toml`. Hitting the ground ends the round.
- `spawn_pipes` adds a pair of kinematic pipes on a timer, built by the `pipe` bundle function. Their `Velocity` moves them and their `Lifetime` removes them once they have crossed the screen.
- `hit_pipe` takes one `Overlap<Bird, Pipe>`: the bird's collider is a sensor, so touching a pipe is an overlap, and the system runs once for each.
- `Art` loads its textures and sounds in its field defaults, and `Bird` brings its `Velocity` through `requires:`.
- `score`, `scroll_ground`, `score_label` and `banner` keep the score, the moving ground and the HUD up to date.
