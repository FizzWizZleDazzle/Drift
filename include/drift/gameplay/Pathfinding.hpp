#pragma once

// A* pathfinding on a grid of weighted cells.
//
//   NavGrid grid(40, 30, 32.f);          // 40x30 cells of 32 pixels
//   grid.setBlocked({5, 3});
//   grid.setCost({6, 3}, 4.f);           // mud: four times as expensive
//   auto path = findPath(grid, {0, 0}, {20, 10});

#include <drift/Types.hpp>

#include <cstdint>
#include <vector>

namespace drift {

struct GridPoint {
    int32_t x = 0;
    int32_t y = 0;
    bool operator==(const GridPoint& o) const { return x == o.x && y == o.y; }
    bool operator!=(const GridPoint& o) const { return !(*this == o); }
};

class NavGrid {
public:
    NavGrid() = default;
    // `cellSize` and `origin` place the grid in the world, for the
    // world/cell conversions; the search itself works in cells.
    NavGrid(int32_t width, int32_t height, float cellSize = 1.f, Vec2 origin = {});

    void resize(int32_t width, int32_t height);
    int32_t width() const { return width_; }
    int32_t height() const { return height_; }
    bool inBounds(GridPoint p) const { return p.x >= 0 && p.y >= 0 && p.x < width_ && p.y < height_; }

    // Cost of entering a cell; 1 by default. A cost <= 0 blocks the cell.
    void setCost(GridPoint p, float cost);
    float cost(GridPoint p) const;
    void setBlocked(GridPoint p, bool blocked = true) { setCost(p, blocked ? 0.f : 1.f); }
    bool walkable(GridPoint p) const { return cost(p) > 0.f; }
    void fill(float cost);

    float cellSize = 1.f;
    Vec2 origin = {};
    GridPoint worldToCell(Vec2 world) const;
    Vec2 cellToWorld(GridPoint cell) const;   // cell center

private:
    int32_t width_ = 0;
    int32_t height_ = 0;
    std::vector<float> costs_;
};

enum class DiagonalMode : uint8_t {
    Never,              // 4-neighbour movement
    Always,             // 8-neighbour, may squeeze between two blocked corners
    NoCornerCutting,    // 8-neighbour, only when both adjacent orthogonals are walkable
    AllowOneCorner,     // 8-neighbour, when at least one adjacent orthogonal is walkable
};

struct PathOptions {
    DiagonalMode diagonal = DiagonalMode::NoCornerCutting;
    // Heuristic weight: 1 gives shortest paths; above 1 searches fewer
    // cells but may return slightly longer paths.
    float heuristicWeight = 1.f;
    // Give up after expanding this many cells (0 = no limit).
    uint32_t maxExpanded = 0;
};

// Cells from start to goal inclusive, or empty when there is no path (or
// start/goal are blocked or out of bounds). start == goal returns {start}.
std::vector<GridPoint> findPath(const NavGrid& grid, GridPoint start, GridPoint goal,
                                const PathOptions& options = {});

// Sum of entry costs along a path (diagonal steps cost sqrt(2) times more).
float pathCost(const NavGrid& grid, const std::vector<GridPoint>& path);

} // namespace drift
