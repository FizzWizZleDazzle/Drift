# Cave Walk

Walk around generated levels: a cellular-automata cave or a BSP dungeon with doors, built from one seed. Coins and gems are scattered over the floor, and slimes chase you around the walls. 1 and 2 switch between the cave and the dungeon, R rolls a new seed, the arrow keys or WASD move. A slime that reaches you halves your score and sends you back to the start. [Play it in the browser](../play/cave_walk/).

It shows the [Procedural Generation](../guides/procedural-generation.md) guide in one game: the game's seed from `drift.toml`, generator streams, a tilemap made in code and filled from grids, noise, Poisson-disk points, a weighted table and a flow field.

```sh
cd bindings/candela/examples/cave_walk
drift build --web
drift run --set random.seed=777   # on the desktop, the same levels every time
```

## drift.toml

```toml
{{#include ../../../examples/cave_walk/drift.toml}}
```

The view is top-down, so `[physics]` turns gravity off. `[random]` leaves the seed at 0, so every run starts with a new one; the log names it.

## main.cdl

```rust
{{#include ../../../examples/cave_walk/main.cdl}}
```

## How it fits together

- `Art` draws its tileset in a field default: four 16x16 tiles from grain noise, made with `Noise.samples` in one call.
- `setup` takes the seed from `Random`. `controls` switches the level kind or rolls a seed, and sets `rebuild`.
- `build` runs every frame but returns at once unless `rebuild` is set. It despawns the old level, generates a grid with its own stream of the seed for each kind, so switching back finds the same level, and writes it into a two-layer map: floors in two noise-shaped variants, and walls on a solid layer that becomes colliders. The first Poisson-disk point is the start; each other point rolls a coin, a gem or a slime from a weighted table.
- `chase` rebuilds the flow field when the player enters another cell; each slime near the player moves along it.
- `pick_up` runs once for each pickup the player touches, through sensor overlaps.
- The map sits under a strip for the HUD: `TOP` shifts the `Tilemap` entity down, and `world` and `cell_of` convert between cells and positions.
