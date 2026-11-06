# Coin Dash (candela)

**Source:** `bindings/candela/examples/coin_dash/main.cdl`

A small complete game written in [candela](../candela/overview.md): steer a square around an arena, collect coins, and dodge red balls that bounce off the walls. A hit ends the round; R starts the next one. The best score is kept in save data. Every texture and sound is generated in code.

## What It Shows

- **The candela frame loop**: `setup` builds the game once, then `update` runs between `app.frame()` calls.
- **Input actions**: "move" is a 2D axis bound to the arrow keys, WASD and the left stick; "restart" is bound to R and the gamepad South button.
- **Physics**: the player is a dynamic body moved by setting its velocity; hazards are bouncy circles with zero friction and full restitution; the walls are static box colliders. World gravity is off.
- **Collision and sensor events**: coins are sensor colliders, collected through `app.sensor_entered()`; a hazard touching the player shows up in `app.collisions()`.
- **Generated assets**: coin and hazard textures come from `app.create_texture` with a soft-edged disc; the pickup and crash sounds are sine sweeps from `app.create_sound`.
- **Text and HUD**: the score and the restart hint are `Text` entities on `hud_layer()`.
- **Tweens, particles, effects**: coins pulse with a yoyo scale tween, pickups throw an additive particle burst, and a hit flashes the screen red.
- **Timers and save data**: a repeating `Timer` adds a hazard every few seconds; the best score goes to a `Save` in the per-user directory.

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

`tests/play_coin_dash.cdl` imports the game and plays it with synthetic key presses: it moves the player, drops a coin on it, throws a hazard at it and restarts, checking the game state after each step.
