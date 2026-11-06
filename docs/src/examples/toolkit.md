# Gameplay Toolkit

**Source:** `examples/toolkit/main.cpp`

A small top-down grid game built from the gameplay toolkit: steer a cursor, send a ball along the cheapest path to it, and collect coins. All textures are generated in code.

## What It Shows

- **Input actions**: "move" is a 2D axis bound to WASD, the arrow keys and the left stick; "go", "wall" and "rebind" are actions bound to keys, mouse buttons and gamepad buttons. Press R, then any key or button, to rebind "go".
- **Save data**: the best score and the bindings persist between runs through `SavePlugin`.
- **Noise**: the walls and mud come from simplex fBm.
- **Pathfinding**: "go" runs A* from the ball to the cursor. Mud costs four times as much as open ground, so paths go around it when they can.
- **Tweens**: the ball walks the path as a sequence of `moveTo` steps ending in a `call`; walls pop in with `Ease::OutBack` and a delay that makes a wave; coins bob with a yoyo loop and grow and fade when collected.
- **Timers**: a repeating `Timer` component sends `TimerFinished` and a system spawns a coin for each one.
- **Spatial index**: coins carry `SpatialBounds`; the ball collects them with `queryRadius`.
- **Events**: the pickup tween's `TweenFinished` event despawns the coin.
- **Y-sort**: walls, coins and the ball overlap by depth.

## Controls

| Input | Action |
|-------|--------|
| WASD / arrows / left stick / mouse | Move the cursor |
| Space / left click / gamepad South | Go to the cursor |
| E / right click / gamepad West | Add or remove a wall |
| R / gamepad North | Rebind "go" |

## Running

```bash
cmake --build build --target toolkit
cd examples/toolkit && ../../build/toolkit
```

The HUD uses the built-in font, so the example needs no asset files.
