#pragma once

// Level generators. Each takes an Rng and fills a Grid with GridFloor and
// GridWall cells; the same seed and options give the same level on every
// platform.
//
//   Rng rng(seed);
//   Grid cave = generateCave(rng, 80, 50);
//   Dungeon dungeon = generateDungeon(rng, 80, 50);
//   std::vector<Vec2> trees = poissonDisk(rng, {0, 0, 800, 600}, 24.f);

#include <drift/math/Noise.hpp>
#include <drift/math/Random.hpp>
#include <drift/procgen/Grid.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace drift {

// ---- Cellular-automata caves ----

struct CaveOptions {
    float fill = 0.45f;        // chance that a cell starts as wall
    int32_t steps = 5;         // smoothing passes
    // A cell becomes wall when at least `birth` of its 8 neighbours are
    // walls, and a wall stays one with at least `survive`.
    int32_t birth = 5;
    int32_t survive = 4;
    bool solidEdges = true;    // the outermost cells are always walls
    bool keepLargest = true;   // fill every floor region but the largest
};

Grid generateCave(Rng& rng, int32_t width, int32_t height, const CaveOptions& options = {});

// ---- BSP dungeons ----

struct GridRect {
    int32_t x = 0, y = 0, w = 0, h = 0;
    GridPoint center() const { return {x + w / 2, y + h / 2}; }
    bool contains(GridPoint p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
    bool operator==(const GridRect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

struct DungeonOptions {
    int32_t minRoom = 4;          // room side, in cells
    int32_t maxRoom = 10;
    int32_t padding = 1;          // wall cells kept between a room and its partition's edge
    int32_t corridorWidth = 1;
    // Corridors added on top of the tree that joins every room, between
    // rooms that are already close. They make loops.
    int32_t extraCorridors = 2;
    bool doors = true;            // GridDoor where a corridor enters a room
};

struct Dungeon {
    Grid grid;                     // GridWall, GridFloor, GridDoor
    std::vector<GridRect> rooms;   // in partition order
};

// Splits the area into partitions (binary space partitioning), puts one
// room in each, and joins sibling partitions with L-shaped corridors, so
// every room is reachable.
Dungeon generateDungeon(Rng& rng, int32_t width, int32_t height, const DungeonOptions& options = {});

// ---- Random walk ----

struct WalkOptions {
    float coverage = 0.4f;      // stop once this share of the cells is floor
    int32_t walkers = 1;        // walkers start together and wander apart
    float turnChance = 0.4f;    // chance per step to pick a new direction
    int32_t brush = 1;          // carves brush x brush cells per step
    GridPoint start = {-1, -1}; // (-1, -1) = the middle
};

// Starts all wall and carves floor where walkers ("drunkards") wander.
// Walkers stay one cell away from the edge.
Grid randomWalk(Rng& rng, int32_t width, int32_t height, const WalkOptions& options = {});

// ---- Poisson-disk sampling ----

// Points in `area` no closer than `minDistance` to each other, spread
// evenly but without a pattern (Bridson's algorithm). `attempts` is how
// many candidates each point tries before it is retired; higher packs
// tighter. `accept` (optional) rejects candidates, for masks.
std::vector<Vec2> poissonDisk(Rng& rng, const Rect& area, float minDistance, int32_t attempts = 30,
                              const std::function<bool(Vec2)>& accept = {});

// Poisson-disk points over the cells of `grid` equal to `value`, in cell
// units (a point at (3.5, 2.5) is the middle of cell (3, 2)).
std::vector<Vec2> poissonDiskOnGrid(Rng& rng, const Grid& grid, int32_t value, float minDistance,
                                    int32_t attempts = 30);

// ---- Noise ----

// A grid from noise: cells where the noise at the cell's centre is at or
// above `threshold` become `above`, the rest `below`.
Grid gridFromNoise(uint32_t seed, int32_t width, int32_t height, const NoiseOptions& options,
                   float threshold = 0.f, int32_t above = GridWall, int32_t below = GridFloor);

} // namespace drift
