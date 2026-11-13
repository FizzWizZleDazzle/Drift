#include <drift/gameplay/Pathfinding.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace drift {

NavGrid::NavGrid(int32_t width, int32_t height, float cs, Vec2 o) : cellSize(cs), origin(o) {
    resize(width, height);
}

void NavGrid::resize(int32_t width, int32_t height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    costs_.assign(static_cast<size_t>(width_) * height_, 1.f);
}

void NavGrid::setCost(GridPoint p, float c) {
    if (!inBounds(p)) return;
    costs_[static_cast<size_t>(p.y) * width_ + p.x] = c;
}

float NavGrid::cost(GridPoint p) const {
    if (!inBounds(p)) return 0.f;
    return costs_[static_cast<size_t>(p.y) * width_ + p.x];
}

void NavGrid::fill(float c) { std::fill(costs_.begin(), costs_.end(), c); }

GridPoint NavGrid::worldToCell(Vec2 w) const {
    return {static_cast<int32_t>(std::floor((w.x - origin.x) / cellSize)),
            static_cast<int32_t>(std::floor((w.y - origin.y) / cellSize))};
}

Vec2 NavGrid::cellToWorld(GridPoint c) const {
    return {origin.x + (c.x + 0.5f) * cellSize, origin.y + (c.y + 0.5f) * cellSize};
}

namespace {

constexpr float kSqrt2 = 1.41421356237f;

struct Open {
    float f;
    float g;
    uint32_t index;
    bool operator>(const Open& o) const { return f > o.f || (f == o.f && g < o.g); }
};

} // namespace

std::vector<GridPoint> findPath(const NavGrid& grid, GridPoint start, GridPoint goal,
                                const PathOptions& opt) {
    if (!grid.walkable(start) || !grid.walkable(goal)) return {};
    if (start == goal) return {start};

    const int32_t w = grid.width(), h = grid.height();
    const size_t n = static_cast<size_t>(w) * h;
    const bool diag = opt.diagonal != DiagonalMode::Never;

    // The cheapest cell cost bounds the heuristic so it stays admissible
    // when costs below 1 exist.
    float minCost = std::numeric_limits<float>::max();
    for (int32_t y = 0; y < h; ++y)
        for (int32_t x = 0; x < w; ++x) {
            float c = grid.cost({x, y});
            if (c > 0.f) minCost = std::min(minCost, c);
        }

    auto heuristic = [&](int32_t x, int32_t y) {
        float dx = std::fabs(float(x - goal.x)), dy = std::fabs(float(y - goal.y));
        float d = diag ? (dx + dy) + (kSqrt2 - 2.f) * std::min(dx, dy)   // octile
                       : dx + dy;                                       // manhattan
        return d * minCost * opt.heuristicWeight;
    };

    std::vector<float> g(n, std::numeric_limits<float>::infinity());
    std::vector<uint32_t> parent(n, UINT32_MAX);
    std::vector<uint8_t> closed(n, 0);
    std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;

    auto idx = [w](int32_t x, int32_t y) { return static_cast<uint32_t>(y) * w + x; };
    const uint32_t startIdx = idx(start.x, start.y), goalIdx = idx(goal.x, goal.y);
    g[startIdx] = 0.f;
    open.push({heuristic(start.x, start.y), 0.f, startIdx});

    static constexpr int dx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static constexpr int dy8[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    const int dirs = diag ? 8 : 4;
    uint32_t expanded = 0;

    while (!open.empty()) {
        Open cur = open.top();
        open.pop();
        if (closed[cur.index]) continue;
        closed[cur.index] = 1;
        if (cur.index == goalIdx) break;
        if (opt.maxExpanded && ++expanded > opt.maxExpanded) return {};

        int32_t cx = static_cast<int32_t>(cur.index % w), cy = static_cast<int32_t>(cur.index / w);
        for (int d = 0; d < dirs; ++d) {
            int32_t nx = cx + dx8[d], ny = cy + dy8[d];
            float c = grid.cost({nx, ny});
            if (c <= 0.f) continue;   // blocked or out of bounds
            bool isDiag = d >= 4;
            if (isDiag) {
                bool a = grid.walkable({cx + dx8[d], cy});
                bool b = grid.walkable({cx, cy + dy8[d]});
                if (opt.diagonal == DiagonalMode::NoCornerCutting && !(a && b)) continue;
                if (opt.diagonal == DiagonalMode::AllowOneCorner && !(a || b)) continue;
            }
            uint32_t ni = idx(nx, ny);
            if (closed[ni]) continue;
            float ng = g[cur.index] + c * (isDiag ? kSqrt2 : 1.f);
            if (ng < g[ni]) {
                g[ni] = ng;
                parent[ni] = cur.index;
                open.push({ng + heuristic(nx, ny), ng, ni});
            }
        }
    }

    if (parent[goalIdx] == UINT32_MAX) return {};
    std::vector<GridPoint> path;
    for (uint32_t i = goalIdx; i != UINT32_MAX; i = parent[i]) {
        path.push_back({static_cast<int32_t>(i % w), static_cast<int32_t>(i / w)});
        if (i == startIdx) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

void FlowField::build(const NavGrid& grid, const std::vector<GridPoint>& goals, DiagonalMode diagonal,
                      float maxCost) {
    width_ = grid.width();
    height_ = grid.height();
    cellSize_ = grid.cellSize;
    origin_ = grid.origin;
    const size_t n = static_cast<size_t>(width_) * height_;
    cost_.assign(n, std::numeric_limits<float>::infinity());
    next_.assign(n, -1);
    if (n == 0) return;

    static constexpr int dx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static constexpr int dy8[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    const int dirs = diagonal == DiagonalMode::Never ? 4 : 8;
    auto idx = [this](int32_t x, int32_t y) { return static_cast<uint32_t>(y) * width_ + x; };
    // Whether a diagonal step between (x, y) and (x + dx, y + dy) is legal.
    auto diagonalOk = [&](int32_t x, int32_t y, int dx, int dy) {
        bool a = grid.walkable({x + dx, y}), b = grid.walkable({x, y + dy});
        if (diagonal == DiagonalMode::NoCornerCutting) return a && b;
        if (diagonal == DiagonalMode::AllowOneCorner) return a || b;
        return true;
    };

    struct Item {
        float cost;
        uint32_t index;
        bool operator>(const Item& o) const { return cost > o.cost || (cost == o.cost && index > o.index); }
    };
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    for (GridPoint g : goals) {
        if (!grid.walkable(g)) continue;
        cost_[idx(g.x, g.y)] = 0.f;
        open.push({0.f, idx(g.x, g.y)});
    }

    // Searching outward from the goals: a step from cell c to its
    // neighbour costs what entering c costs, so the field holds the cost of
    // walking from each cell to a goal.
    while (!open.empty()) {
        Item cur = open.top();
        open.pop();
        if (cur.cost > cost_[cur.index]) continue;
        int32_t cx = static_cast<int32_t>(cur.index % width_), cy = static_cast<int32_t>(cur.index / width_);
        float enter = grid.cost({cx, cy});
        for (int d = 0; d < dirs; ++d) {
            int32_t nx = cx + dx8[d], ny = cy + dy8[d];
            if (!grid.walkable({nx, ny})) continue;
            bool isDiag = d >= 4;
            if (isDiag && !diagonalOk(nx, ny, -dx8[d], -dy8[d])) continue;
            float c = cur.cost + enter * (isDiag ? kSqrt2 : 1.f);
            if (c > maxCost) continue;
            uint32_t ni = idx(nx, ny);
            if (c < cost_[ni]) {
                cost_[ni] = c;
                open.push({c, ni});
            }
        }
    }

    // Each reachable cell points at the neighbour its cheapest way to a
    // goal steps into (the first one, in neighbour order, on ties).
    for (int32_t y = 0; y < height_; ++y)
        for (int32_t x = 0; x < width_; ++x) {
            uint32_t i = idx(x, y);
            if (!std::isfinite(cost_[i]) || cost_[i] == 0.f) continue;
            float best = std::numeric_limits<float>::infinity();
            for (int d = 0; d < dirs; ++d) {
                int32_t nx = x + dx8[d], ny = y + dy8[d];
                if (!grid.walkable({nx, ny})) continue;
                if (d >= 4 && !diagonalOk(x, y, dx8[d], dy8[d])) continue;
                uint32_t ni = idx(nx, ny);
                float total = cost_[ni] + grid.cost({nx, ny}) * (d >= 4 ? kSqrt2 : 1.f);
                if (total < best) {
                    best = total;
                    next_[i] = static_cast<int32_t>(ni);
                }
            }
        }
}

bool FlowField::reachable(GridPoint c) const {
    return c.x >= 0 && c.y >= 0 && c.x < width_ && c.y < height_ &&
           std::isfinite(cost_[static_cast<size_t>(c.y) * width_ + c.x]);
}

float FlowField::cost(GridPoint c) const {
    if (c.x < 0 || c.y < 0 || c.x >= width_ || c.y >= height_) return std::numeric_limits<float>::infinity();
    return cost_[static_cast<size_t>(c.y) * width_ + c.x];
}

GridPoint FlowField::next(GridPoint c) const {
    if (c.x < 0 || c.y < 0 || c.x >= width_ || c.y >= height_) return c;
    int32_t n = next_[static_cast<size_t>(c.y) * width_ + c.x];
    if (n < 0) return c;
    return {n % width_, n / width_};
}

Vec2 FlowField::direction(GridPoint c) const {
    GridPoint n = next(c);
    if (n == c) return {};
    return Vec2{float(n.x - c.x), float(n.y - c.y)}.normalized();
}

Vec2 FlowField::direction(Vec2 world) const {
    if (cellSize_ <= 0.f) return {};
    GridPoint c{static_cast<int32_t>(std::floor((world.x - origin_.x) / cellSize_)),
                static_cast<int32_t>(std::floor((world.y - origin_.y) / cellSize_))};
    return direction(c);
}

float pathCost(const NavGrid& grid, const std::vector<GridPoint>& path) {
    float total = 0.f;
    for (size_t i = 1; i < path.size(); ++i) {
        bool isDiag = path[i].x != path[i - 1].x && path[i].y != path[i - 1].y;
        total += grid.cost(path[i]) * (isDiag ? kSqrt2 : 1.f);
    }
    return total;
}

} // namespace drift
