# Procgen

**Source:** `examples/procgen/main.cpp`

Walk around levels that four generators build from one seed: a cave, a dungeon, tunnels and ruins. Pickups and slimes are scattered over the floor, and the slimes chase you around the walls. The tiles are drawn in code, so the example needs no asset files.

## What It Shows

- **Generators**: `generateCave` (cellular automata), `generateDungeon` (BSP rooms, corridors and doors), `randomWalk` (four walkers with a 2x2 brush) and `generateWfc` with rules learned from a 10x10 hand-drawn sample, a wall border fixed through `WfcOptions::preset`, and `keepLargestRegion` to keep one connected area.
- **The game's seed**: the `Random` resource gives the seed from `drift.toml` `[random]`; each generator gets its own `Rng` stream of it, so switching modes keeps each level, and R rolls a new seed.
- **Maps made in code**: `createTilemap` with a floor and a wall layer over a generated tileset; `fillTilemapLayer` writes the grids, and the wall layer becomes static colliders through `setLayerCollision`.
- **Noise**: the tileset's grain and the patches of the two floor variants.
- **Poisson-disk sampling**: `poissonDiskOnGrid` spreads spawn points over the floor; the first one is the start.
- **Weighted tables**: each point rolls a coin, a gem or a slime.
- **Flow fields**: one `FlowField` toward the player, rebuilt when the player enters another cell, steers every slime; `steer::separation` keeps them from stacking.
- **Physics**: the player and slimes are dynamic bodies sliding along the tilemap colliders, and the camera follows the player.

## Controls

| Input | Action |
|-------|--------|
| WASD / arrows | Move |
| 1 / 2 / 3 / 4 | Cave, dungeon, tunnels, ruins |
| R | New seed |
| F12 | Save a screenshot |

A slime that reaches you halves your score and sends you back to the start.

## Running

```bash
cmake --build build --target procgen
cd examples/procgen && ../../build/procgen
```

`../../build/procgen tour` switches generator every four seconds. Set `seed` in `drift.toml`, or pass `--set random.seed=<n>`, to get the same levels again; with 0 the log names the seed it picked.
