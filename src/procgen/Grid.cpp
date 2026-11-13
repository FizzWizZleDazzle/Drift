#include <drift/procgen/Grid.hpp>

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace drift {

void Grid::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t value) {
    int32_t x0 = std::max(0, x), y0 = std::max(0, y);
    int32_t x1 = std::min(width, x + w), y1 = std::min(height, y + h);
    for (int32_t yy = y0; yy < y1; ++yy)
        for (int32_t xx = x0; xx < x1; ++xx) cells[index(xx, yy)] = value;
}

size_t Grid::count(int32_t value) const {
    return static_cast<size_t>(std::count(cells.begin(), cells.end(), value));
}

namespace {

constexpr int kDx[4] = {1, 0, -1, 0};
constexpr int kDy[4] = {0, 1, 0, -1};

// Cells connected to `start` with the same value; marks them in `seen`.
std::vector<GridPoint> region(const Grid& g, GridPoint start, std::vector<uint8_t>& seen) {
    std::vector<GridPoint> out;
    const int32_t value = g.get(start);
    std::vector<GridPoint> stack{start};
    seen[g.index(start.x, start.y)] = 1;
    while (!stack.empty()) {
        GridPoint p = stack.back();
        stack.pop_back();
        out.push_back(p);
        for (int d = 0; d < 4; ++d) {
            GridPoint n{p.x + kDx[d], p.y + kDy[d]};
            if (!g.inBounds(n) || g.get(n) != value) continue;
            uint8_t& s = seen[g.index(n.x, n.y)];
            if (s) continue;
            s = 1;
            stack.push_back(n);
        }
    }
    return out;
}

} // namespace

std::vector<std::vector<GridPoint>> findRegions(const Grid& grid, int32_t value) {
    std::vector<std::vector<GridPoint>> out;
    std::vector<uint8_t> seen(grid.cells.size(), 0);
    for (int32_t y = 0; y < grid.height; ++y)
        for (int32_t x = 0; x < grid.width; ++x) {
            if (seen[grid.index(x, y)] || grid.get(x, y) != value) continue;
            out.push_back(region(grid, {x, y}, seen));
        }
    return out;
}

size_t removeSmallRegions(Grid& grid, int32_t value, size_t minSize, int32_t fill) {
    size_t removed = 0;
    for (const auto& r : findRegions(grid, value)) {
        if (r.size() >= minSize) continue;
        for (GridPoint p : r) grid.set(p, fill);
        ++removed;
    }
    return removed;
}

void keepLargestRegion(Grid& grid, int32_t value, int32_t fill) {
    auto regions = findRegions(grid, value);
    if (regions.size() < 2) return;
    size_t best = 0;
    for (size_t i = 1; i < regions.size(); ++i) {
        if (regions[i].size() > regions[best].size()) best = i;   // first of equal sizes wins
    }
    for (size_t i = 0; i < regions.size(); ++i) {
        if (i == best) continue;
        for (GridPoint p : regions[i]) grid.set(p, fill);
    }
}

size_t connectRegions(Grid& grid, int32_t value, int32_t width) {
    auto regions = findRegions(grid, value);
    if (regions.size() < 2) return 0;
    width = std::max(1, width);
    auto carve = [&](GridPoint p) {
        int32_t lo = -(width - 1) / 2;
        for (int32_t dy = 0; dy < width; ++dy)
            for (int32_t dx = 0; dx < width; ++dx) {
                GridPoint c{p.x + lo + dx, p.y + lo + dy};
                // Keep the outer border intact.
                if (c.x > 0 && c.y > 0 && c.x < grid.width - 1 && c.y < grid.height - 1) grid.set(c, value);
            }
    };

    std::vector<GridPoint> joined = regions[0];
    size_t tunnels = 0;
    for (size_t r = 1; r < regions.size(); ++r) {
        // Nearest pair of cells between this region and the joined ones
        // (Manhattan distance; the first nearest pair wins ties).
        int64_t bestD = std::numeric_limits<int64_t>::max();
        GridPoint a{}, b{};
        for (GridPoint p : regions[r]) {
            for (GridPoint q : joined) {
                int64_t d = std::abs(int64_t(p.x) - q.x) + std::abs(int64_t(p.y) - q.y);
                if (d < bestD) {
                    bestD = d;
                    a = p;
                    b = q;
                }
            }
        }
        // Horizontal leg, then vertical.
        GridPoint c = a;
        while (c.x != b.x) {
            carve(c);
            c.x += b.x > c.x ? 1 : -1;
        }
        while (c.y != b.y) {
            carve(c);
            c.y += b.y > c.y ? 1 : -1;
        }
        carve(c);
        joined.insert(joined.end(), regions[r].begin(), regions[r].end());
        ++tunnels;
    }
    return tunnels;
}

Grid floodFill(const Grid& grid, GridPoint start) {
    Grid out(grid.width, grid.height, 0);
    if (!grid.inBounds(start)) return out;
    std::vector<uint8_t> seen(grid.cells.size(), 0);
    for (GridPoint p : region(grid, start, seen)) out.set(p, 1);
    return out;
}

NavGrid navGridFrom(const Grid& grid, const std::vector<int32_t>& blocked, float cellSize, Vec2 origin) {
    NavGrid nav(grid.width, grid.height, cellSize, origin);
    for (int32_t y = 0; y < grid.height; ++y)
        for (int32_t x = 0; x < grid.width; ++x) {
            int32_t v = grid.get(x, y);
            if (std::find(blocked.begin(), blocked.end(), v) != blocked.end()) nav.setBlocked({x, y});
        }
    return nav;
}

} // namespace drift
