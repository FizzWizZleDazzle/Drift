# Procedural Generation

Generators for levels and placement: cellular-automata caves, BSP dungeons, random walks, wave function collapse, Poisson-disk scattering, noise grids and weighted tables. Each one takes an `Rng`, so a seed gives the same level every time, on the desktop and in the browser. The results are `Grid`s that fill tilemap layers and pathfinding grids directly.

```cpp
#include <drift/procgen/Generators.hpp>
#include <drift/procgen/TilemapFill.hpp>

Rng rng = random->stream("level-1");
Grid cave = generateCave(rng, 80, 50);

TilemapHandle map = maps->createTilemap({.width = 80, .height = 50, .tileset = tiles});
const uint32_t tileFor[] = {1, 2};                    // GridFloor -> tile 1, GridWall -> tile 2
fillTilemapLayer(*maps, map, 0, cave, tileFor);
maps->setLayerCollision(map, 0, true, {2});           // walls become colliders
cmd.spawn().insert(Transform2D{}, Tilemap{.map = map});
```

The [Procgen example](../examples/procgen.md) walks through four generators with a player, pickups and chasing enemies.

## Grids

**Header:** `#include <drift/procgen/Grid.hpp>`

A `Grid` is a width x height array of `int32_t` cells, row by row from the top-left. The generators write `GridFloor` (0) for open cells and `GridWall` (1) for solid ones, and dungeons add `GridDoor` (2); any other values are yours.

| Member | Use |
|--------|-----|
| `Grid(w, h, value)` | A grid filled with `value` |
| `get(x, y, outside)`, `set(x, y, v)` | Read and write cells; `get` returns `outside` (`GridWall` by default) beyond the edges |
| `inBounds(x, y)`, `fill(v)`, `fillRect(x, y, w, h, v)`, `count(v)` | |
| `cells`, `width`, `height` | The raw data |

Region tools work on any value:

| Function | Does |
|----------|------|
| `findRegions(grid, value)` | Connected regions (4-neighbour) of `value`, each a list of cells |
| `keepLargestRegion(grid, value, fill)` | Every region but the largest becomes `fill` |
| `removeSmallRegions(grid, value, minSize, fill)` | Regions under `minSize` cells become `fill` |
| `connectRegions(grid, value, width)` | Carves L-shaped tunnels until every region of `value` is joined |
| `floodFill(grid, start)` | 1 where `start` reaches through cells of its own value, 0 elsewhere |
| `navGridFrom(grid, blocked, cellSize, origin)` | A `NavGrid` for [pathfinding](spatial-and-pathfinding.md#grid-pathfinding) with the `blocked` values blocked |

## Caves

**Header:** `#include <drift/procgen/Generators.hpp>`

`generateCave` fills the grid with random walls, then smooths it a few times with a cellular automaton: a cell becomes wall when enough of its eight neighbours are walls. The result is organic caverns.

```cpp
Grid cave = generateCave(rng, 80, 50, {.fill = 0.48f, .steps = 6});
```

| `CaveOptions` field | Default | Meaning |
|---------------------|---------|---------|
| `fill` | 0.45 | Chance a cell starts as wall. Higher gives narrower caves |
| `steps` | 5 | Smoothing passes |
| `birth`, `survive` | 5, 4 | Wall neighbours that turn a floor into wall, and that keep a wall |
| `solidEdges` | true | The outermost ring is always wall |
| `keepLargest` | true | Fill every cavern but the largest, so the whole cave is reachable |

## Dungeons

`generateDungeon` splits the area into partitions (binary space partitioning), puts a room in each, and joins sibling partitions with L-shaped corridors, so every room is reachable. It returns the grid and the rooms.

```cpp
Dungeon d = generateDungeon(rng, 80, 50, {.minRoom = 5, .maxRoom = 12, .extraCorridors = 4});
GridPoint start = d.rooms.front().center();
GridPoint boss = d.rooms.back().center();
```

| `DungeonOptions` field | Default | Meaning |
|------------------------|---------|---------|
| `minRoom`, `maxRoom` | 4, 10 | Room side in cells |
| `padding` | 1 | Wall cells kept between a room and the edge of its partition |
| `corridorWidth` | 1 | |
| `extraCorridors` | 2 | Corridors added between nearby rooms on top of the joining tree, which makes loops |
| `doors` | true | `GridDoor` where a corridor enters a room between two walls |

`Dungeon::rooms` holds `GridRect`s (`x`, `y`, `w`, `h`, `center()`, `contains()`), in partition order.

## Random walk

`randomWalk` starts from solid wall and lets walkers wander, carving floor, until a share of the area is open. One walker makes winding tunnels; several with a wider brush make open caverns.

```cpp
Grid tunnels = randomWalk(rng, 80, 50, {.coverage = 0.35f, .walkers = 4, .brush = 2});
```

| `WalkOptions` field | Default | Meaning |
|---------------------|---------|---------|
| `coverage` | 0.4 | Stop once this share of the cells is floor |
| `walkers` | 1 | Walkers start together and wander apart |
| `turnChance` | 0.4 | Chance per step to pick a new direction |
| `brush` | 1 | Carves `brush` x `brush` cells per step |
| `start` | middle | Start cell; `{-1, -1}` means the middle |

## Wave function collapse

**Header:** `#include <drift/procgen/Wfc.hpp>`

Wave function collapse builds a grid in which every pair of neighbours is allowed by a set of rules. Every cell starts able to hold any tile; the generator repeatedly fixes the most constrained cell to a tile, chosen by weight, and narrows its neighbours to what the rules still allow.

The easiest rules come from an example you draw: every value in it becomes a tile weighted by how often it appears, and every pair of values that sit side by side is allowed in that direction.

```cpp
Grid sample = ...;   // a small hand-made layout
WfcRules rules = WfcRules::fromSample(sample);
Grid ruins = generateWfc(rng, rules, 72, 48);
if (ruins.width == 0) { /* every attempt hit a contradiction */ }
```

Rules can also be written by hand:

```cpp
WfcRules coast;
coast.addTile(Water, 3.f);
coast.addTile(Sand, 1.f);
coast.addTile(Grass, 3.f);
coast.allowAll(Water, Water);
coast.allowAll(Sand, Sand);
coast.allowAll(Grass, Grass);
coast.allowAll(Water, Sand);   // sand between water and grass
coast.allowAll(Sand, Grass);
```

`allow(a, b, direction)` lets `b` sit in `direction` of `a` (and `a` in the opposite direction of `b`).

| `WfcOptions` field | Default | Meaning |
|--------------------|---------|---------|
| `attempts` | 10 | Restarts after a contradiction |
| `periodic` | false | Neighbours wrap around the edges, for tiling patterns |
| `preset` | empty | A grid of the output size: cells holding a tile value are fixed to it before collapsing starts; any other value (such as -1) leaves the cell free. Use it for solid borders, entrances, or a hand-placed room |

Rules allow up to 256 tiles. Strict rules can contradict themselves, more often on large grids; presets that the rules cannot complete always do. The simple tiled model only sees neighbours, so it reproduces local texture rather than large structures: run `keepLargestRegion` afterwards when the result must be one connected area.

## Poisson-disk sampling

Poisson-disk sampling places points no closer than a minimum distance to each other, evenly but without a visible pattern: trees, rocks, pickups, spawn points.

```cpp
std::vector<Vec2> trees = poissonDisk(rng, {0, 0, 2000, 1200}, 48.f);
// Only where the mask says so:
auto onGrass = poissonDisk(rng, area, 32.f, 30, [&](Vec2 p) { return isGrass(p); });
// Over the floor cells of a grid, in cell units (3.5, 2.5 is the middle of cell 3, 2):
std::vector<Vec2> spots = poissonDiskOnGrid(rng, dungeon.grid, GridFloor, 4.f);
```

The argument after the distance is how many candidates each point tries before it is retired (30 by default); more packs the points tighter. `poissonDiskOnGrid` also starts from every pocket of matching cells, so rooms behind thin corridors get points too.

## Noise grids

`gridFromNoise(seed, width, height, options, threshold, above, below)` samples [noise](../math/random-and-noise.md#noise) at each cell's center and writes `above` where it is at least `threshold`, `below` elsewhere:

```cpp
NoiseOptions islands{.frequency = 0.08f, .octaves = 4, .warp = 6.f};
Grid land = gridFromNoise(seed, 100, 60, islands, 0.1f, Land, Water);
```

## Weighted tables

**Header:** `#include <drift/procgen/WeightedTable.hpp>`

`WeightedTable<T>` rolls items by weight: loot drops, enemy types, room contents.

```cpp
WeightedTable<std::string> loot;
loot.add("coin", 70).add("potion", 25).add("sword", 5);

const std::string* drop = loot.roll(rng);          // nullptr when no weight is above 0
std::vector<std::string> chest = loot.roll(rng, 3);           // may repeat
std::vector<std::string> picks = loot.rollUnique(rng, 2);     // never repeats
```

Weights are relative (70, 25, 5 rolls the same as 0.7, 0.25, 0.05) and an entry with weight 0 or less never comes up. `rollIndex` returns the entry's index instead.

## Into tilemaps

**Header:** `#include <drift/procgen/TilemapFill.hpp>`

| Function | Does |
|----------|------|
| `fillTilemapLayer(maps, map, layer, grid, tileForValue, at)` | Writes the grid into a tile layer from cell `at`: a cell of value v becomes tile `tileForValue[v]`; 0, and values past the table's end, leave the cell empty |
| `fillIntGridLayer(maps, map, layer, grid, at)` | Writes values as they are into an LDtk IntGrid layer |
| `gridFromTilemapLayer(maps, map, layer)` | A layer's tile ids as a grid of the map's size |

Generated levels usually go into a map made in code with `TilemapResource::createTilemap` ([Tilemap: Maps made in code](../subsystems/tilemap.md#maps-made-in-code)). To give walls proper edges, fill a hidden mask layer with the grid and [autotile](../subsystems/tilemap.md#runtime-autotiling) the visible layer from it.

## Determinism

Every generator here uses only integer math, `+ - * /` and `sqrt` on floats, compiled without fused multiply-add, so the same seed and options give the same grid on every platform. The engine's tests check fixed seeds against stored results on Linux and, built with Emscripten, under Node; Windows and macOS builds are not verified yet.

The output for a seed is fixed within a Drift version, not across versions: improving a generator changes the levels existing seeds produce. Store generated content you must keep, rather than only its seed.
