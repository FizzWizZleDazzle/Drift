<!-- Written by scripts/candela-reference.py from the drift module and
     bindings/candela/docs/api-notes.md. Edit those, not this page. -->

# Procedural Generation

Seeded random numbers, noise, level generators, weighted tables and pathfinding over grids. A seed gives the same numbers and the same levels on every platform and in the browser. See [Procedural Generation](../guides/procedural-generation.md).

## Rng

A seeded random number generator. Its methods advance it, so keep it in a resource or component to carry on where it left off. Assigning a struct shares it: `let b = a;` gives a second name for the same generator, and `a.copy()` gives an independent one.

```rust
struct Rng {
    hi: int = 0,
    lo: int = 0,
    inc: int = 1,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `hi` | `int` | `0` | Generator state |
| `lo` | `int` | `0` | Generator state |
| `inc` | `int` | `1` | Generator stream |

```rust
@resource
struct Loot { rng: Rng = rng(1234) }

@system
fn drop(loot: Loot) {
    let gold = loot.rng.range(5, 10);
}
```

### Rng.next_int

```rust
fn next_int(self) -> int
```

32 random bits, 0 to 4294967295.

```rust
let bits = r.next_int();
```

### Rng.next_float

```rust
fn next_float(self) -> float
```

0.0 up to, not including, 1.0.

```rust
if r.next_float() < 0.25 { spawn_bonus(); }
```

### Rng.below

```rust
fn below(self, n: int) -> int
```

0 up to, not including, n.

```rust
let side = r.below(4);   // 0, 1, 2 or 3
```

### Rng.range

```rust
fn range(self, lo: int, hi: int) -> int
```

lo to hi, both included.

```rust
let damage = r.range(1, 6);   // a six-sided die
```

### Rng.range_float

```rust
fn range_float(self, lo: float, hi: float) -> float
```

lo up to, not including, hi.

```rust
let x = r.range_float(0.0, 640.0);
```

### Rng.chance

```rust
fn chance(self, p: float) -> bool
```

`true` with probability `p`, 0 to 1.

```rust
if r.chance(0.15) { drop_rare(); }
```

### Rng.sign

```rust
fn sign(self) -> float
```

-1.0 or 1.0.

```rust
let dir = r.sign();
```

### Rng.angle

```rust
fn angle(self) -> float
```

Radians, 0 to tau.

```rust
let v = from_angle(r.angle()) * 100.0;
```

### Rng.unit_vector

```rust
fn unit_vector(self) -> Vec2
```

A direction of length 1.

```rust
let push = r.unit_vector() * 200.0;
```

### Rng.inside_circle

```rust
fn inside_circle(self, radius: float) -> Vec2
```

A point inside a circle of `radius` around the origin.

```rust
let spot = center + r.inside_circle(32.0);
```

### Rng.inside_rect

```rust
fn inside_rect(self, r: Rect) -> Vec2
```

A point inside a rect.

```rust
let p = r.inside_rect(rect(0.0, 0.0, 640.0, 360.0));
```

### Rng.normal

```rust
fn normal(self, mean: float, stddev: float) -> float
```

A Gaussian sample.

```rust
let height = r.normal(170.0, 10.0);
```

### Rng.weighted

```rust
fn weighted(self, weights: float[]) -> int
```

An index chosen in proportion to its weight; -1 when no weight is above zero.

```rust
let i = r.weighted([70.0, 25.0, 5.0]);
```

### Rng.pick

```rust
fn pick<T>(self, items: T[]) -> T
```

A random element. The array must not be empty.

```rust
let name = r.pick<string>(["Ada", "Grace", "Alan"]);
```

### Rng.shuffle

```rust
fn shuffle<T>(self, items: T[]) -> T[]
```

The items in a random order.

```rust
let deck = r.shuffle<Card>(cards);
```

### Rng.copy

```rust
fn copy(self) -> Rng
```

A generator at the same point, to replay what this one produces next.

```rust
let replay = r.copy();
```

### Rng.split

```rust
fn split(self) -> Rng
```

A new generator seeded from this one: its own sequence for a sub-task.

```rust
let room_rng = level.rng.split();
```

## rng

```rust
fn rng(seed: int) -> Rng
```

A generator seeded with `seed`.

```rust
let r = rng(42);
```

## rng_stream

```rust
fn rng_stream(seed: int, stream: int) -> Rng
```

Another sequence for the same seed: generators with different streams do not repeat each other.

```rust
let caves = rng_stream(seed, 1);
let dungeons = rng_stream(seed, 2);
```

## seed_from

```rust
fn seed_from(text: string) -> int
```

A seed from text, for seeds players type: rng(seed_from("daily-2026-11-12")).

```rust
let r = rng(seed_from("daily-2026-11-13"));
```

## WeightedTable

Items rolled by weight: loot, spawns, room types.

```rust
struct WeightedTable<T> {
    items: T[],
    weights: float[],
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `items` | `T[]` | required | The entries |
| `weights` | `float[]` | required | Their weights |

```rust
let loot = weighted_table<string>().add("coin", 70.0).add("potion", 25.0).add("sword", 5.0);
if loot.roll(r) is Some(item) {
    log("found a " + item);
}
```

### WeightedTable.add

```rust
fn add(self, item: T, weight: float) -> WeightedTable<T>
```

Adds an item with a weight, and returns the table, so calls chain.

```rust
table.add("gem", 2.0);
```

### WeightedTable.roll

```rust
fn roll(self, rng: Rng) -> Option<T>
```

An item chosen in proportion to its weight, or `None` when no entry can come up.

```rust
if table.roll(r) is Some(kind) { spawn(kind); }
```

## weighted_table

```rust
fn weighted_table<T>() -> WeightedTable<T>
```

An empty table of items of type `T`. Weights are relative; an entry with weight 0 or less never comes up.

```rust
let enemies = weighted_table<int>().add(1, 10.0).add(2, 3.0);
```

## Noise

A noise recipe. Samples are -1.0 to 1.0; the same seed gives the same field everywhere. `frequency` scales positions (0.05 makes features about 20 units across), `octaves` layers finer detail, and `warp` bends the field by up to that many units.

```rust
struct Noise {
    seed: int = 0,
    kind: NoiseType = NoiseType::Simplex,
    frequency: float = 1.0,
    octaves: int = 1,
    lacunarity: float = 2.0,
    gain: float = 0.5,
    warp: float = 0.0,
    warp_frequency: float = 1.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `seed` | `int` | `0` | Different seeds give unrelated fields |
| `kind` | `NoiseType` | `NoiseType::Simplex` | `Value` is blocky; `Perlin` and `Simplex` are smooth |
| `frequency` | `float` | `1.0` | Scales positions; 0.05 makes features about 20 units across |
| `octaves` | `int` | `1` | Layers of finer detail |
| `lacunarity` | `float` | `2.0` | Each layer's frequency over the last one's |
| `gain` | `float` | `0.5` | Each layer's strength over the last one's |
| `warp` | `float` | `0.0` | Bends the field by up to this many units, for swirls and rivers |
| `warp_frequency` | `float` | `1.0` | The frequency of the bending field |

```rust
let terrain = Noise { seed: 7, frequency: 0.03, octaves: 5, .. };
let h = terrain.sample(x, y);
```

### Noise.sample

```rust
fn sample(self, x: float, y: float) -> float
```

The noise at a point, from -1 to 1.

```rust
let v = terrain.sample(float(x), float(y));
```

### Noise.samples

```rust
fn samples(self, from: Vec2, w: int, h: int, step: float) -> float[]
```

w x h samples, row by row, starting at `from` and `step` apart.

```rust
let row = terrain.samples(vec2(0.0, 0.0), 64, 64, 1.0);
```

### Noise.grid

```rust
fn grid(self, w: int, h: int, threshold: float, above: int, below: int) -> Grid
```

A grid from the noise at each cell's center: `above` where it is at least `threshold`, `below` elsewhere.

```rust
let land = noise(seed).grid(60, 32, 0.1, FLOOR, WALL);
```

## noise

```rust
fn noise(seed: int) -> Noise
```

The default recipe with a seed.

```rust
let n = noise(seed);
```

## Cell

A cell of a grid or tilemap: a column `x` and a row `y`.

```rust
struct Cell {
    x: int = 0,
    y: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `x` | `int` | `0` | Column, from 0 at the left |
| `y` | `int` | `0` | Row, from 0 at the top |

```rust
let c = Cell { x: 3, y: 4 };
```

## cell

```rust
fn cell(x: int, y: int) -> Cell
```

```rust
let start = cell(1, 1);
```

## Grid

The generators write `FLOOR` for open cells and `WALL` for solid ones, and dungeons add `DOOR`; your own values can be anything.

```rust
struct Grid {
    width: int = 0,
    height: int = 0,
    cells: int[] = [],
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `width` | `int` | `0` | Columns |
| `height` | `int` | `0` | Rows |
| `cells` | `int[]` | `[]` | `width * height` values, row by row from the top-left |

```rust
let g = grid(20, 10, FLOOR);
g.set(cell(5, 5), WALL);
```

### Grid.in_bounds

```rust
fn in_bounds(self, c: Cell) -> bool
```

Whether a cell is inside the grid.

```rust
if g.in_bounds(c) { g.set(c, FLOOR); }
```

### Grid.get

```rust
fn get(self, c: Cell) -> int
```

The value at a cell; WALL outside the grid.

```rust
if level.grid.get(next) != WALL { pos.cell = next; }
```

### Grid.set

```rust
fn set(self, c: Cell, value: int)
```

Sets a cell's value; cells outside the grid are ignored.

```rust
g.set(cell(0, 0), DOOR);
```

### Grid.count

```rust
fn count(self, value: int) -> int
```

How many cells hold `value`.

```rust
let open = g.count(FLOOR);
```

### Grid.cells_of

```rust
fn cells_of(self, value: int) -> Cell[]
```

Every cell holding `value`, row by row.

```rust
let floors = g.cells_of(FLOOR);
```

### Grid.keep_largest_region

```rust
fn keep_largest_region(self, value: int, fill: int)
```

Keeps the largest connected region of `value`; the others become `fill`.

```rust
caves.keep_largest_region(FLOOR, WALL);
```

### Grid.remove_small_regions

```rust
fn remove_small_regions(self, value: int, min_size: int, fill: int)
```

Regions of `value` under `min_size` cells become `fill`.

```rust
g.remove_small_regions(FLOOR, 12, WALL);
```

### Grid.connect_regions

```rust
fn connect_regions(self, value: int, width: int)
```

Carves tunnels of `value` so every region of it is connected.

```rust
g.connect_regions(FLOOR, 2);
```

### Grid.region_sizes

```rust
fn region_sizes(self, value: int) -> int[]
```

The sizes of the connected regions of `value`, in cells.

```rust
let sizes = g.region_sizes(FLOOR);
```

### Grid.flood_fill

```rust
fn flood_fill(self, from: Cell) -> Grid
```

1 where `from` can reach through cells of its own value, 0 elsewhere.

```rust
let reach = g.flood_fill(start);
```

## grid

```rust
fn grid(width: int, height: int, value: int) -> Grid
```

A grid of `width` by `height` cells, all holding `value`.

```rust
let g = grid(40, 25, WALL);
```

## FLOOR

```rust
const FLOOR: int = 0
```

The value generators write for open cells.

```rust
if g.get(c) == FLOOR { place_coin(c); }
```

## WALL

```rust
const WALL: int = 1
```

The value generators write for solid cells, and what `Grid.get` answers outside the grid.

```rust
let path = find_path(g, a, b, [WALL], false);
```

## DOOR

```rust
const DOOR: int = 2
```

The value `dungeon` writes where a corridor enters a room.

```rust
let doors = d.grid.cells_of(DOOR);
```

## CaveOptions

Cellular-automata caves: random walls smoothed into caverns.

```rust
struct CaveOptions {
    fill: float = 0.45,
    steps: int = 5,
    birth: int = 5,
    survive: int = 4,
    solid_edges: bool = true,
    keep_largest: bool = true,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `fill` | `float` | `0.45` | Chance a cell starts as wall; higher gives narrower caves |
| `steps` | `int` | `5` | Smoothing passes |
| `birth` | `int` | `5` | Wall neighbours, of 8, that turn a cell into wall |
| `survive` | `int` | `4` | Wall neighbours that keep a wall |
| `solid_edges` | `bool` | `true` | The outer ring is always wall |
| `keep_largest` | `bool` | `true` | Fills every cavern but the largest, so the whole cave is reachable |

```rust
let caves = cave(r, 60, 32, CaveOptions { fill: 0.48, steps: 4, .. });
```

## cave

```rust
fn cave(rng: Rng, width: int, height: int, o: CaveOptions) -> Grid
```

```rust
let g = cave(level.rng, 60, 32, CaveOptions { .. });
```

## GridRect

A rectangle of cells: a dungeon room.

```rust
struct GridRect {
    x: int = 0,
    y: int = 0,
    w: int = 0,
    h: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `x` | `int` | `0` | Left column |
| `y` | `int` | `0` | Top row |
| `w` | `int` | `0` | Width in cells |
| `h` | `int` | `0` | Height in cells |

```rust
for room in d.rooms {
    if room.contains(player_cell) { log("in a room"); }
}
```

### GridRect.center

```rust
fn center(self) -> Cell
```

The cell in the middle.

```rust
let start = d.rooms[0].center();
```

### GridRect.contains

```rust
fn contains(self, c: Cell) -> bool
```

Whether a cell is inside the room.

```rust
if room.contains(c) { lit = true; }
```

## DungeonOptions

```rust
struct DungeonOptions {
    min_room: int = 4,
    max_room: int = 10,
    padding: int = 1,
    corridor_width: int = 1,
    extra_corridors: int = 2,
    doors: bool = true,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `min_room` | `int` | `4` | Smallest room side, in cells |
| `max_room` | `int` | `10` | Largest room side |
| `padding` | `int` | `1` | Wall kept around each room inside its partition |
| `corridor_width` | `int` | `1` | Corridor width in cells |
| `extra_corridors` | `int` | `2` | Corridors between nearby rooms on top of the ones that join them all; they make loops |
| `doors` | `bool` | `true` | Writes `DOOR` where a corridor enters a room |

```rust
let d = dungeon(r, 40, 25, DungeonOptions { min_room: 4, max_room: 8, extra_corridors: 3, .. });
```

## Dungeon

What `dungeon` makes.

```rust
struct Dungeon {
    grid: Grid,
    rooms: GridRect[],
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `grid` | `Grid` | required | The level |
| `rooms` | `GridRect[]` | required | Every room, in the order the generator made them |

```rust
let d = dungeon(r, 40, 25, DungeonOptions { .. });
let stairs = d.rooms[d.rooms.len() - 1].center();
```

## dungeon

```rust
fn dungeon(rng: Rng, width: int, height: int, o: DungeonOptions) -> Dungeon
```

Rooms in a binary space partition, joined by corridors so every room is reachable.

```rust
let d = dungeon(level.rng, 40, 25, DungeonOptions { .. });
level.grid = d.grid;
```

## WalkOptions

```rust
struct WalkOptions {
    coverage: float = 0.4,
    walkers: int = 1,
    turn_chance: float = 0.4,
    brush: int = 1,
    start: Cell = Cell { x: -1, y: -1 },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `coverage` | `float` | `0.4` | Stops once this share of the cells is floor |
| `walkers` | `int` | `1` | How many walkers carve at once |
| `turn_chance` | `float` | `0.4` | Chance per step to turn |
| `brush` | `int` | `1` | Carves `brush` by `brush` cells per step |
| `start` | `Cell` | `Cell { x: -1, y: -1 }` | Start cell; `cell(-1, -1)` is the middle |

```rust
let tunnels = random_walk(r, 60, 32, WalkOptions { walkers: 4, brush: 2, .. });
```

## random_walk

```rust
fn random_walk(rng: Rng, width: int, height: int, o: WalkOptions) -> Grid
```

Walkers wander from the start and carve floor out of solid wall.

```rust
let g = random_walk(r, 60, 32, WalkOptions { coverage: 0.35, .. });
```

## poisson_disk

```rust
fn poisson_disk(rng: Rng, area: Rect, min_distance: float) -> Vec2[]
```

Points in `area` no closer than `min_distance` to each other: even spacing without a pattern, for trees, rocks and spawn points.

```rust
for p in poisson_disk(r, rect(0.0, 0.0, 960.0, 540.0), 48.0) {
    cmd.spawn([at(p.x, p.y), Sprite { image: art.tree, .. }]);
}
```

## poisson_on_grid

```rust
fn poisson_on_grid(rng: Rng, g: Grid, value: int, min_distance: float) -> Vec2[]
```

The same over the cells of `g` holding `value`, in cell units: a point at (3.5, 2.5) is the middle of cell (3, 2).

```rust
for p in poisson_on_grid(r, level.grid, FLOOR, 6.0) {
    cmd.spawn([at(p.x * 16.0, p.y * 16.0), Coin {}]);
}
```

## WfcOptions

```rust
struct WfcOptions {
    attempts: int = 10,
    periodic: bool = false,
    preset: Grid = Grid { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `attempts` | `int` | `10` | Restarts after a contradiction |
| `periodic` | `bool` | `false` | Neighbours wrap around the edges |
| `preset` | `Grid` | `Grid { .. }` |  |

```rust
let ruins = wfc(r, sample, 60, 32, WfcOptions { attempts: 20, .. });
```

## wfc

```rust
fn wfc(rng: Rng, sample: Grid, width: int, height: int, o: WfcOptions) -> Option<Grid>
```

Wave function collapse: a width x height grid where every pair of neighbouring values also sits side by side somewhere in `sample`, with values about as common as there. None when every attempt contradicted itself.

```rust
if wfc(r, sample, 60, 32, WfcOptions { .. }) is Some(g) {
    g.keep_largest_region(FLOOR, WALL);
    level.grid = g;
}
```

## find_path

```rust
fn find_path(g: Grid, start: Cell, goal: Cell, blocked: int[], diagonal: bool) -> Cell[]
```

The cells from start to goal, both included, avoiding cells whose value is in `blocked`; empty when there is no way. `diagonal` allows diagonal steps that do not cut corners.

```rust
let path = find_path(level.grid, monster_cell, player_cell, [WALL], false);
if path.len() > 1 {
    pos.cell = path[1];   // one step closer
}
```

## FlowField

For every cell, the next step toward the nearest goal: one field steers any number of chasers.

```rust
struct FlowField {
    width: int = 0,
    height: int = 0,
    next: int[] = [],
    costs: float[] = [],
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `width` | `int` | `0` | Columns |
| `height` | `int` | `0` | Rows |
| `next` | `int[]` | `[]` | For each cell, the index of the next cell toward a goal |
| `costs` | `float[]` | `[]` | For each cell, steps to the nearest goal |

```rust
level.field = flow_field(level.grid, [player_cell], [WALL], true);
v.linear = level.field.direction(my_cell) * 60.0;
```

### FlowField.in_bounds

```rust
fn in_bounds(self, c: Cell) -> bool
```

Whether a cell is inside the field.

```rust
if field.in_bounds(c) { step = field.step(c); }
```

### FlowField.cost

```rust
fn cost(self, c: Cell) -> float
```

Steps to the nearest goal; -1.0 when none is reachable.

```rust
if field.cost(c) < 0.0 { log("cut off"); }
```

### FlowField.step

```rust
fn step(self, c: Cell) -> Cell
```

The neighbour one step closer; the cell itself at a goal or where no goal is reachable.

```rust
pos.cell = field.step(pos.cell);
```

### FlowField.direction

```rust
fn direction(self, c: Cell) -> Vec2
```

Unit vector toward the next step, for a cell.

```rust
v.linear = field.direction(c) * speed;
```

### FlowField.direction_at

```rust
fn direction_at(self, p: Vec2, cell_size: float) -> Vec2
```

The same for a world position, with cells `cell_size` wide from the origin.

```rust
v.linear = field.direction_at(t.position, 16.0) * speed;
```

## flow_field

```rust
fn flow_field(g: Grid, goals: Cell[], blocked: int[], diagonal: bool) -> FlowField
```

A flow field over a grid toward the nearest of `goals`, through cells whose value is not in `blocked`. Build it again when a goal moves to another cell, not every frame.

```rust
let field = flow_field(g, [goal], [WALL], true);
```

## SpatialBounds

Puts the entity in the spatial index: a box of `half_size` around its position plus `offset`; a zero size indexes the point.

```rust
struct SpatialBounds {
    half_size: Vec2 = Vec2 { .. },
    offset: Vec2 = Vec2 { .. },
}
```

## Spatial

"What is near here" over the entities with SpatialBounds, without colliders. The index holds where they were at the start of the frame.

```rust
struct Spatial {
    id: int = 0,
}
```

### Spatial.in_rect

```rust
fn in_rect(self, r: Rect) -> Entity[]
```

### Spatial.in_radius

```rust
fn in_radius(self, center: Vec2, radius: float) -> Entity[]
```

### Spatial.nearest

```rust
fn nearest(self, p: Vec2, max_distance: float, ignore: Entity) -> Entity
```

The closest within `max_distance`, skipping `ignore` (pass the asking entity); Entity {} when there is none.

## field_of_view

```rust
fn field_of_view(g: Grid, from: Cell, radius: int, opaque: int[]) -> Grid
```

The cells `from` sees within `radius` cells, as a grid of the same size: 1 where visible, 0 elsewhere. Cells whose value is in `opaque` block sight and are seen themselves. Symmetric: if a sees b, b sees a.

## line_of_sight

```rust
fn line_of_sight(g: Grid, a: Cell, b: Cell, opaque: int[]) -> bool
```

Whether no opaque cell lies strictly between a and b.

## grid_line

```rust
fn grid_line(a: Cell, b: Cell) -> Cell[]
```

The cells of the straight line from a to b, both included.
