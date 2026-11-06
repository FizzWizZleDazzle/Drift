# Coin Dash (candela)

**Source:** `bindings/candela/examples/coin_dash/main.cdl`

A small complete game written in [candela](../candela/overview.md): steer a square around an arena, collect coins, and dodge red balls that bounce off the walls. A hit ends the round; R starts the next one. The best score is kept in save data. Every texture and sound is generated in code.

## What It Shows

- **Systems and components**: `coin_dash(app)` adds a `setup` startup system, two update systems (`play` steers and spawns, `show_score` keeps the HUD text current) and two collision handlers. `Player`, `Coin`, `Hazard` and the label markers are the game's own components; the game state is a `Game` resource.
- **Collision handlers**: `on_sensor<Player, Coin>(pick_up)` collects a coin, `on_collision<Player, Hazard>(hit)` ends the round. Each handler gets the two entities in the declared order.
- **Drawing with components**: the player and walls are `Rectangle`s, coins and hazards are `Sprite`s, the score and hint are `Text` on `hud_layer()`. Nothing is drawn by hand.
- **Physics**: the player is a dynamic `RigidBody` steered through its `Velocity`; hazards are bouncy `CircleCollider`s with zero friction and full restitution; walls are static `BoxCollider`s. World gravity is off.
- **Declarative behavior**: coins pulse with a yoyo `Tween`; a pickup spawns a "+1" `Text` with a `Velocity` that floats it up and a `Lifetime` that removes it, with no system involved.
- **Input actions**: "move" is a 2D axis bound to the arrow keys, WASD and the left stick; "restart" is bound to R and the gamepad South button.
- **Effects and audio**: pickups throw an additive particle burst and play a generated blip; a hit flashes the screen red.
- **Timers and save data**: a repeating `Timer` in the `Game` resource adds a hazard every few seconds; the best score goes to a `Save`.

## Controls

| Input | Action |
|-------|--------|
| Arrows / WASD / left stick | Move |
| R / gamepad South | Play again after a hit |
| Escape | Quit |

## Running

Build the native library first (see [Candela](../candela/overview.md#building-the-package)), then:

```bash
cd bindings/candela
candela run examples/coin_dash/main.cdl
```

`tests/play_coin_dash.cdl` builds the same app with one more update system that plays it with synthetic key presses: it moves the player, drops a coin on it, throws a hazard at it and restarts, checking the game state and taking screenshots after each step.
