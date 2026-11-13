# Roguelike

Go down the stairs as deep as you can. Every level is a new dungeon of rooms and corridors with rats, goblins, gold and potions.

[Play it in the browser](../play/roguelike/). The arrow keys, WASD or HJKL move one cell; walking into a monster attacks it, and Space waits a turn. Take the stairs (the striped tile) down. R starts a new run.

## What it shows

- `dungeon()` from a generator seeded by the game's seed
- A tilemap made in code with `create_tilemap` and filled from the grid with `Map.fill`, over a tileset drawn in code
- Rooms filled from a `weighted_table`, items on `poisson_on_grid` points
- Monsters that walk toward you along `find_path`
- A turn-based loop: one system acts once per key press
- Characters as `Text` glyphs, a message log in a resource

## Run it

```sh
cd bindings/candela/examples/roguelike
drift build --web
cd build/web && python3 -m http.server 8000
```

The example is self-contained: every image and sound is made in code.

## drift.toml

```toml
{{#include ../../../examples/roguelike/drift.toml}}
```

## Building a level

One generator from the game's seed makes every level, so a seed in `[random]` replays the same dungeons. The grid becomes the map's tile layer, the last room gets the stairs, and the rest of the rooms roll their monsters.

```rust
{{#include ../../../examples/roguelike/main.cdl:build}}
```

## A turn

The player moves or attacks, picks up what is underfoot, and then each monster within reach takes a step along its path or bites.

```rust
{{#include ../../../examples/roguelike/main.cdl:turn}}
```

## The whole game

```rust
{{#include ../../../examples/roguelike/main.cdl}}
```
