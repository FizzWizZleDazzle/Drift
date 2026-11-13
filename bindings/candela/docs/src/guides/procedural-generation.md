# Procedural Generation

Random numbers you can replay, noise, level generators, weighted tables, and pathfinding over the levels they make. They are the engine's own: a seed gives the same numbers and the same levels in a Candela game, in a C++ game and in the browser.

## Random numbers

`rng(seed)` makes a generator. Its methods advance it, so keep it in a resource to carry on where it left off:

```rust
@resource
struct Loot { rng: Rng = rng(1234) }

@system
fn drop(loot: Loot, cmd: Commands) {
    let gold = loot.rng.range(5, 10);          // 5 to 10, both included
    if loot.rng.chance(0.15) { /* a rare drop */ }
    let spot = loot.rng.inside_circle(32.0);
}
```

| Method | Returns |
|--------|---------|
| `next_int()` | 32 random bits, 0 to 4294967295 |
| `next_float()` | 0.0 up to, not including, 1.0 |
| `below(n)` | 0 up to, not including, `n` |
| `range(lo, hi)` | An int from `lo` to `hi`, both included |
| `range_float(lo, hi)` | A float from `lo` up to `hi` |
| `chance(p)` | `true` with probability `p` |
| `sign()` | -1.0 or 1.0 |
| `angle()`, `unit_vector()` | A random direction |
| `inside_circle(r)`, `inside_rect(r)` | A point inside a shape |
| `normal(mean, stddev)` | A Gaussian sample |
| `weighted(weights)` | An index chosen in proportion to its weight; -1 when no weight is above 0 |
| `pick<T>(items)`, `shuffle<T>(items)` | A random element; the items in a random order. Name the element type: `rng.shuffle<Card>(deck)` |
| `split()` | A new generator seeded from this one, for a sub-task |
| `copy()` | A generator at the same point, to replay what this one produces next |

`rng_stream(seed, stream)` gives the same seed another sequence; `seed_from(text)` turns text, such as a seed a player typed, into a seed. Structs are shared when assigned, so `let b = a;` gives a second name for the same generator: use `a.copy()` for an independent one.

`unit_vector`, `inside_circle` and `normal` go through the platform's `sin`, `cos` and `log`, whose last bits can differ between platforms; everything else is the same everywhere.

### The game's seed

`[random]` in `drift.toml` sets one seed for the whole game, and the `Random` engine resource hands it out:

```toml
[random]
seed = 1234   # 0 (the default) picks a new seed each run and logs it
```

```rust
@system(schedule: Schedule::Startup)
fn setup(random: Random, level: Level) {
    level.rng = random.stream("levels");   // the same every time for this seed
}
```

`random.seed()` is the seed in use; `random.stream(name)` is a generator for one purpose, the same for the same seed and name and independent of every other name, so rolling an extra loot drop does not change the next level. With seed 0 the log names the seed it picked; put it in `drift.toml`, or run `drift run --set random.seed=<n>`, to play the same run again.

## Weighted tables

```rust
let loot = weighted_table<string>().add("coin", 70.0).add("potion", 25.0).add("sword", 5.0);
if loot.roll(rng) is Some(item) {
    log("found a " + item);
}
```

Weights are relative, and an entry with weight 0 or less never comes up. `roll` returns `None` when no entry can.

## Noise

A `Noise` is a recipe for a smooth random field, with samples from -1.0 to 1.0:

```rust
let terrain = Noise { seed: 7, frequency: 0.03, octaves: 5, warp: 12.0, .. };
let height = terrain.sample(x, y);
```

| Field | Default | Meaning |
|-------|---------|---------|
| `seed` | 0 | Different seeds give unrelated fields; `noise(seed)` makes the default recipe |
| `kind` | `NoiseType::Simplex` | `Value` (blocky), `Perlin` or `Simplex` |
| `frequency` | 1.0 | Scales positions: 0.05 makes features about 20 units across |
| `octaves`, `lacunarity`, `gain` | 1, 2.0, 0.5 | Layers of finer detail, each `lacunarity` times the frequency and `gain` times the strength of the last |
| `warp`, `warp_frequency` | 0.0, 1.0 | Bends the field by up to `warp` units along another noise field, for swirls and rivers |

`samples(from, w, h, step)` returns `w * h` samples row by row in one call, which is much faster than calling `sample` in a loop. `grid(w, h, threshold, above, below)` makes a `Grid` with `above` where the noise at a cell's center is at least `threshold`.

## Grids

The generators work on a `Grid`: `width`, `height` and `cells`, an `int[]` row by row from the top-left. They write `FLOOR` (0) for open cells and `WALL` (1) for solid ones; dungeons add `DOOR` (2). Your own values can be anything.

| Member | Use |
|--------|-----|
| `grid(w, h, value)` | A grid filled with `value` |
| `get(c)`, `set(c, v)` | A cell's value (`WALL` outside the grid) |
| `in_bounds(c)`, `count(v)`, `cells_of(v)` | |
| `keep_largest_region(value, fill)` | The largest connected region of `value` stays; the others become `fill` |
| `remove_small_regions(value, min_size, fill)` | Regions under `min_size` cells become `fill` |
| `connect_regions(value, width)` | Carves tunnels until every region of `value` is joined |
| `region_sizes(value)` | The sizes of the connected regions |
| `flood_fill(from)` | A grid with 1 where `from` reaches through cells of its own value |

Cells are `Cell { x, y }`, built with `cell(x, y)`.

## Generators

Each generator takes a generator, advances it, and returns a grid of `width` x `height` cells:

```rust
let r = rng(seed);
let caves = cave(r, 60, 32, CaveOptions { fill: 0.48, .. });
let d = dungeon(r, 60, 32, DungeonOptions { max_room: 9, .. });
let tunnels = random_walk(r, 60, 32, WalkOptions { walkers: 4, brush: 2, .. });
```

**`cave`** fills the grid with random walls and smooths it with a cellular automaton into caverns.

| `CaveOptions` field | Default | Meaning |
|---------------------|---------|---------|
| `fill` | 0.45 | Chance a cell starts as wall. Higher gives narrower caves |
| `steps` | 5 | Smoothing passes |
| `birth`, `survive` | 5, 4 | Wall neighbours (of 8) that make a wall, and that keep one |
| `solid_edges` | `true` | The outer ring is always wall |
| `keep_largest` | `true` | Every cavern but the largest is filled, so the whole cave is reachable |

**`dungeon`** splits the area into partitions, puts a room in each and joins them with corridors, so every room is reachable. It returns a `Dungeon` with the `grid` and the `rooms`, each a `GridRect` (`x`, `y`, `w`, `h`, `center()`, `contains(c)`).

| `DungeonOptions` field | Default | Meaning |
|------------------------|---------|---------|
| `min_room`, `max_room` | 4, 10 | Room side in cells |
| `padding` | 1 | Wall kept around each room inside its partition |
| `corridor_width` | 1 | |
| `extra_corridors` | 2 | Corridors between nearby rooms on top of the ones that join them all; they make loops |
| `doors` | `true` | `DOOR` where a corridor enters a room |

**`random_walk`** starts from solid wall and lets walkers wander, carving floor, until a share of the cells is open.

| `WalkOptions` field | Default | Meaning |
|---------------------|---------|---------|
| `coverage` | 0.4 | Stop once this share of the cells is floor |
| `walkers` | 1 | |
| `turn_chance` | 0.4 | Chance per step to turn |
| `brush` | 1 | Carves `brush` x `brush` cells per step |
| `start` | middle | Start cell; `cell(-1, -1)` is the middle |

### Wave function collapse

`wfc(rng, sample, width, height, options)` builds a grid in which every pair of neighbouring values also sits side by side somewhere in `sample`, with values about as common as there. Draw a small example of the layout you want and it makes more of it:

```rust
let ruins = wfc(r, sample, 60, 32, WfcOptions { preset: border, .. });
if ruins is Some(g) {
    g.keep_largest_region(FLOOR, WALL);
}
```

| `WfcOptions` field | Default | Meaning |
|--------------------|---------|---------|
| `attempts` | 10 | Restarts after a contradiction |
| `periodic` | `false` | Neighbours wrap around the edges |
| `preset` | empty | A grid of the output size: a cell holding a value from the sample is fixed to it, any other value (such as -1) leaves it free. For solid borders, entrances, or a room you placed by hand |

It returns `None` when every attempt contradicted itself. It only learns which values sit next to which, so it copies the sample's local texture rather than its large structures; run `keep_largest_region` when the result must be one connected area.

### Scattering points

`poisson_disk(rng, area, min_distance)` returns points in a `Rect` no closer than `min_distance` to each other: even, without a pattern, for trees, rocks and spawn points. `poisson_on_grid(rng, g, value, min_distance)` does the same over the cells of `g` holding `value`, in cell units, so multiply by your cell size:

```rust
for p in poisson_on_grid(r, level.grid, FLOOR, 4.0) {
    cmd.spawn([at(p.x * 16.0, p.y * 16.0), Coin { .. }]);
}
```

## Into a tilemap

`Assets.create_tilemap` makes an empty map, and `Map.fill` writes a grid into one of its layers through a table of tile ids, one per value:

```rust
level.map = assets.create_tilemap(60, 32, 16, art.tiles, ["Floor", "Walls"]);
level.map.fill(0, floor, [0, 1, 2], cell(0, 0));   // value 1 -> tile 1, 2 -> tile 2
level.map.fill(1, caves, [0, 3], cell(0, 0));      // FLOOR stays empty, WALL is tile 3
level.map.set_layer_solid(1, true, [3]);           // walls become colliders
cmd.spawn([at(0.0, 0.0), Tilemap { map: level.map, .. }]);
```

See [Tilemaps](tilemaps.md#maps-made-in-code).

## Finding the way

`find_path(g, start, goal, blocked, diagonal)` returns the cells from `start` to `goal`, both included, through cells whose value is not in `blocked`; it is empty when there is no way. `diagonal` allows diagonal steps that do not cut corners.

A flow field answers "which way to the goal" for every cell at once, so one field steers any number of chasers:

```rust
level.field = flow_field(level.grid, [player_cell], [WALL], true);
// later, for each chaser:
velocity.linear = level.field.direction(cell_of(t.position)) * 55.0;
```

| `FlowField` method | Returns |
|--------------------|---------|
| `direction(c)` | A unit vector toward the next step; zero at a goal or where no goal is reachable |
| `direction_at(pos, cell_size)` | The same for a world position, with cells `cell_size` wide from the origin |
| `step(c)` | The neighbour one step closer |
| `cost(c)` | Steps to the nearest goal; -1.0 when none is reachable |

Build the field again when the goal moves to another cell, not every frame. The grid crosses to the engine on every `find_path` and `flow_field` call, so for large grids, compute paths when something changes rather than per entity per frame.

## Steering

Steering behaviors move agents smoothly toward or away from things: the engine's `steer_*` functions each return a force, a change of velocity per second. Add the ones you want with weights, cap the sum, and integrate into the `Velocity`:

```rust
@component
struct Chaser { wander: Wander = Wander { .. } }

@system
fn chase(c: Chaser, t: Transform, v: Velocity, time: Time, dice: Dice, players: (Player, Transform)[]) {
    let force = steer_wander(v.linear, c.wander, dice.rng, 90.0) * 0.3;
    for (_, p) in players {
        force = force + steer_arrive(t.position, v.linear, p.position, 90.0, 48.0);
    }
    v.linear = steer_truncate(v.linear + steer_truncate(force, 300.0) * time.dt, 90.0);
}
```

| Function | Steers |
|----------|--------|
| `steer_seek(pos, vel, target, max_speed)`, `steer_flee(pos, vel, threat, max_speed, panic_radius)` | Toward a point at full speed; away from one while it is within `panic_radius` (0: always) |
| `steer_arrive(pos, vel, target, max_speed, slow_radius)` | Toward a point, slowing inside `slow_radius` to stop on it |
| `steer_pursue(pos, vel, target, target_vel, max_speed)`, `steer_evade(...)` | Toward or away from where a moving target will be |
| `steer_separation(pos, neighbours, radius, max_speed)`, `steer_cohesion(pos, vel, neighbours, radius, max_speed)`, `steer_alignment(pos, vel, positions, velocities, radius, max_speed)` | Flocking: apart from close neighbours, toward their middle, along their heading |
| `steer_wander(vel, wander, rng, max_speed)` | Aimless but smooth; `Wander` (`angle`, `distance`, `radius`, `jitter`) is the agent's state, kept in a component |
| `steer_follow_path(pos, vel, path, follow, max_speed)` | Along a list of points, arriving at the last; `PathFollow` (`index`, `reach_radius`, `slow_radius`) is the agent's place on it. Feed it the cells of `find_path` turned into world positions |
| `steer_truncate(v, max_length)` | `v` shortened to at most `max_length`, for capping forces and speeds |

The `Spatial` resource finds the neighbours for the flocking behaviors.

## Seeing

`field_of_view(g, from, radius, opaque)` returns a grid of the same size with 1 on every cell `from` sees within `radius` cells and 0 elsewhere. Cells whose value is in `opaque` block sight and are seen themselves, so the walls of a room show around its floor. The view is symmetric: when the player sees a monster, the monster sees the player.

```rust
@system
fn fog_of_war(level: Level, players: (Player, Transform)[]) {
    for (_, t) in players {
        let at = cell(int(t.position.x / 16.0), int(t.position.y / 16.0));
        let seen = field_of_view(level.grid, at, 8, [WALL]);
        for i in range(seen.cells.len()) {
            if seen.cells[i] == 1 {
                level.explored.cells[i] = 1;   // remembered once seen
            }
        }
    }
}
```

`line_of_sight(g, a, b, opaque)` says whether one cell sees another, for a guard deciding whether it spotted the player, and `grid_line(a, b)` lists the cells of the straight line between two cells, for aiming and thrown objects on a grid.

The [Cave Walk](../examples/cave-walk.md) example puts all of this together.
