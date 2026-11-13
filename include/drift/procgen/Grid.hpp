#pragma once

// A rectangle of integer cells, the common currency of the generators in
// procgen/: caves, dungeons, walks and wave function collapse all produce a
// Grid, and fillTilemapLayer and navGridFrom turn one into tiles or a
// pathfinding grid.
//
// The generators write GridFloor (0) for open cells and GridWall (1) for
// solid ones; dungeons add GridDoor (2). Your own values can be anything.

#include <drift/gameplay/Pathfinding.hpp>

#include <cstdint>
#include <vector>

namespace drift {

inline constexpr int32_t GridFloor = 0;
inline constexpr int32_t GridWall = 1;
inline constexpr int32_t GridDoor = 2;

struct Grid {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<int32_t> cells;   // row by row from the top-left

    Grid() = default;
    Grid(int32_t w, int32_t h, int32_t value = 0)
        : width(w > 0 ? w : 0), height(h > 0 ? h : 0),
          cells(static_cast<size_t>(width) * static_cast<size_t>(height), value) {}

    bool inBounds(int32_t x, int32_t y) const { return x >= 0 && y >= 0 && x < width && y < height; }
    bool inBounds(GridPoint p) const { return inBounds(p.x, p.y); }

    // The value at (x, y), or `outside` beyond the edges.
    int32_t get(int32_t x, int32_t y, int32_t outside = GridWall) const {
        return inBounds(x, y) ? cells[index(x, y)] : outside;
    }
    int32_t get(GridPoint p, int32_t outside = GridWall) const { return get(p.x, p.y, outside); }
    void set(int32_t x, int32_t y, int32_t value) {
        if (inBounds(x, y)) cells[index(x, y)] = value;
    }
    void set(GridPoint p, int32_t value) { set(p.x, p.y, value); }

    void fill(int32_t value) { cells.assign(cells.size(), value); }
    // Sets every cell of the rectangle that lies inside the grid.
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t value);
    size_t count(int32_t value) const;

    size_t index(int32_t x, int32_t y) const {
        return static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
    }

    bool operator==(const Grid& o) const { return width == o.width && height == o.height && cells == o.cells; }
};

// ---- Regions ----

// Connected regions of cells equal to `value` (4-neighbour). Each region
// lists its cells; regions come in the order of their first cell, row by
// row.
std::vector<std::vector<GridPoint>> findRegions(const Grid& grid, int32_t value);

// Sets every region of `value` smaller than `minSize` cells to `fill`.
// Returns how many regions were removed.
size_t removeSmallRegions(Grid& grid, int32_t value, size_t minSize, int32_t fill);

// Keeps the largest region of `value` and sets the others to `fill`.
void keepLargestRegion(Grid& grid, int32_t value, int32_t fill);

// Carves `value` along straight L-shaped tunnels so every region of `value`
// joins the first one: each region connects to its nearest already-joined
// region. Returns the number of tunnels.
size_t connectRegions(Grid& grid, int32_t value, int32_t width = 1);

// Cells reachable from `start` through cells equal to grid.get(start)
// (4-neighbour), as a grid of 1 (reached) and 0.
Grid floodFill(const Grid& grid, GridPoint start);

// ---- Conversions ----

// A NavGrid with every cell blocked whose value is in `blocked`, cost 1
// otherwise.
NavGrid navGridFrom(const Grid& grid, const std::vector<int32_t>& blocked = {GridWall},
                    float cellSize = 1.f, Vec2 origin = {});

} // namespace drift
