# Coin Dash

A complete game: move a square with the arrow keys, WASD or a gamepad stick, collect coins, and dodge hazards that bounce around the arena. A hit ends the round; R restarts it. The best score is saved between runs. [Play it in the browser](../play/coin_dash/).

It shows most of what a game needs: components and resources of its own, states, per-entity systems and collision handlers, queries and lookups, commands, input actions, physics, text on the HUD, particles, sound made from samples, timers, save data and settings from `drift.toml`.

```sh
cd bindings/candela/examples/coin_dash
drift build --web
```

## drift.toml

```toml
{{#include ../../../examples/coin_dash/drift.toml}}
```

The arena is top-down, so `[physics]` turns gravity off. `[input]` binds the restart action; the movement axis is bound in code. `[coin_dash]` holds the game's tuning values; the `Tuning` struct below reads them.

## main.cdl

```rust
{{#include ../../../examples/coin_dash/main.cdl}}
```

## How it fits together

- `Round` is the game's state. `new_round` runs when a round starts (the first one starts after `setup`), `crash` when it ends, and `restart` waits for the restart action while the round is over.
- `steer` runs once per player: the `move` axis sets its velocity, and physics moves the body.
- `spawn_hazards` ticks the timer in `Game` and spawns a hazard away from the player.
- `pick_up` runs once for each coin the player touches; `hit` runs when the player touches a hazard and ends the round.
- `Art` makes its textures and sounds in its field defaults, so no system loads them.
- `score_label` and `hint_label` run once per label and copy the score and the round into the text.

Drift keeps every change a system makes. It runs the systems one after another on the script thread, and runs its own work (physics, the coin tweens, the "+1" labels' `Lifetime`) beside them where their data does not overlap. See [Systems](../concepts/systems.md#the-frame).
