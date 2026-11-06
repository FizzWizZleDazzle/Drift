# Tilemap

**Source:** `examples/tilemap/main.cpp`

A small platformer level built from a Tiled map and an LDtk project placed side by side. A player runs across both, colliding with colliders merged from the maps' collision layers.

## What It Shows

- `assets/meadow.tmj` (Tiled, infinite map): a repeating sky image layer and a tinted hill layer inside a group, both with parallax; animated water and lava; trees and crates from an image-collection tileset, drawn bottom-aligned; flipped and rotated arrow tiles at the far left.
- Platforms autotiled at startup from the hidden `PlatformMask` layer with 4-neighbour rules. The mask is also the platforms' collision layer.
- `assets/caves.ldtk` (LDtk): two levels with an IntGrid collision layer and its auto-layer tiles, a Tiles layer with flipped, stacked and off-grid tiles, and entities with fields. Each level keeps its LDtk world position and fills its background color.
- Object spawners: `PlayerSpawn` creates the player and the following camera; `Coin` creates coins from both Tiled objects and LDtk entities, reading their `value` property.
- Sorting between tile layers: the player's sprite sits between the `Platforms` and `FrontDecor` layers, so flowers and bushes draw in front of it.

The tileset and images are generated pixel art.

## Controls

| Key | Action |
|-----|--------|
| Left / Right, A / D | Run (stops the autopilot) |
| Up, W, Space | Jump |
| Tab | Toggle the autopilot |
| Left click | Add or remove a platform cell; the platforms re-autotile and the colliders follow |
| F1 | Show the merged colliders |
| F12 | Save `tilemap.png` |

## Running

```bash
cmake --build build --target tilemap
cd examples/tilemap && ../../build/tilemap
```
