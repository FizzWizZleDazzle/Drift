#pragma once

// A* pathfinding on a grid of weighted cells.
//
//   NavGrid grid(40, 30, 32.f);          // 40x30 cells of 32 pixels
//   grid.setBlocked({5, 3});
//   grid.setCost({6, 3}, 4.f);           // mud: four times as expensive
//   auto path = findPath(grid, {0, 0}, {20, 10});

#include <drift/Types.hpp>

#include <cmath>
#include <cstdint>
#include <functional>
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

// A flow field: the cost to reach the nearest goal from every cell, and the
// neighbour to step to. One field steers any number of agents toward the
// same goals (a horde toward the player, units toward a rally point), where
// a path per agent would repeat the same search.
//
//   FlowField field(grid, {playerCell});
//   Vec2 dir = field.direction(enemy.position);   // unit vector, world space
class FlowField {
public:
    FlowField() = default;
    FlowField(const NavGrid& grid, const std::vector<GridPoint>& goals,
              DiagonalMode diagonal = DiagonalMode::NoCornerCutting) {
        build(grid, goals, diagonal);
    }

    // Recomputes the field (Dijkstra from every goal at once). Cells
    // farther than maxCost stay unreachable, which bounds the work.
    void build(const NavGrid& grid, const std::vector<GridPoint>& goals,
               DiagonalMode diagonal = DiagonalMode::NoCornerCutting, float maxCost = INFINITY);

    int32_t width() const { return width_; }
    int32_t height() const { return height_; }
    bool reachable(GridPoint cell) const;
    // Cost to the nearest goal; infinity when unreachable.
    float cost(GridPoint cell) const;
    // The neighbour one step closer to a goal. A goal cell, an unreachable
    // cell or one outside the grid returns itself.
    GridPoint next(GridPoint cell) const;
    // Unit vector from the cell's center toward next(cell); zero at a goal
    // or where nothing is reachable.
    Vec2 direction(GridPoint cell) const;
    // The same for a world position, through the grid's cellSize and
    // origin.
    Vec2 direction(Vec2 world) const;

private:
    int32_t width_ = 0;
    int32_t height_ = 0;
    float cellSize_ = 1.f;
    Vec2 origin_ = {};
    std::vector<float> cost_;
    std::vector<int32_t> next_;   // index of the next cell, -1 for none
};

// ---- Sight on a grid ----
//
// For roguelikes, stealth and fog of war: which cells an observer sees, and
// whether one cell sees another. `opaque` says which cells block sight;
// cells outside the grid are opaque. A wall is seen itself, so the room's
// walls show around the floor that is in view.

// The cells visible from `origin` within `radius` cells (Euclidean),
// origin included, by symmetric shadowcasting: when a sees b, b sees a.
std::vector<GridPoint> fieldOfView(int32_t width, int32_t height, GridPoint origin, int32_t radius,
                                   const std::function<bool(GridPoint)>& opaque);

// Whether nothing opaque lies strictly between a and b on the line of
// cells joining them.
bool lineOfSight(GridPoint a, GridPoint b, const std::function<bool(GridPoint)>& opaque);

// The cells of the line from a to b, both included (Bresenham).
std::vector<GridPoint> gridLine(GridPoint a, GridPoint b);

} // namespace drift
