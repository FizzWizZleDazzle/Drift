// Level generators. Only integer math, + - * / and sqrt run here (and this
// file builds without fused multiply-add), so a seed gives the same level on
// every platform.

#include <drift/procgen/Generators.hpp>
#include <drift/math/Noise.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>

namespace drift {

// ---------------------------------------------------------------------------
// Caves
// ---------------------------------------------------------------------------

Grid generateCave(Rng& rng, int32_t width, int32_t height, const CaveOptions& o) {
    Grid g(width, height, GridFloor);
    auto edge = [&](int32_t x, int32_t y) {
        return x == 0 || y == 0 || x == width - 1 || y == height - 1;
    };
    for (int32_t y = 0; y < height; ++y)
        for (int32_t x = 0; x < width; ++x) {
            bool wall = (o.solidEdges && edge(x, y)) || rng.chance(o.fill);
            g.set(x, y, wall ? GridWall : GridFloor);
        }

    Grid next = g;
    for (int32_t s = 0; s < o.steps; ++s) {
        for (int32_t y = 0; y < height; ++y)
            for (int32_t x = 0; x < width; ++x) {
                int32_t walls = 0;
                for (int32_t dy = -1; dy <= 1; ++dy)
                    for (int32_t dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        // Outside counts as wall, so caves close at the edge.
                        if (g.get(x + dx, y + dy, GridWall) == GridWall) ++walls;
                    }
                bool wall = g.get(x, y) == GridWall ? walls >= o.survive : walls >= o.birth;
                if (o.solidEdges && edge(x, y)) wall = true;
                next.set(x, y, wall ? GridWall : GridFloor);
            }
        std::swap(g, next);
    }
    if (o.keepLargest) keepLargestRegion(g, GridFloor, GridWall);
    return g;
}

// ---------------------------------------------------------------------------
// Dungeons
// ---------------------------------------------------------------------------

namespace {

struct Node {
    GridRect area;
    std::unique_ptr<Node> a, b;
    int32_t room = -1;   // index into Dungeon::rooms, leaves only
};

void split(Rng& rng, Node& n, const DungeonOptions& o) {
    const int32_t leafMin = o.minRoom + 2 * o.padding;
    const int32_t leafMax = o.maxRoom + 2 * o.padding;
    const GridRect r = n.area;
    bool canX = r.w >= 2 * leafMin, canY = r.h >= 2 * leafMin;
    bool wantX = r.w > leafMax, wantY = r.h > leafMax;
    if (!(canX && wantX) && !(canY && wantY)) return;

    bool vertical;   // split with a vertical line: left and right halves
    if (canX && wantX && canY && wantY) {
        if (r.w * 4 > r.h * 5) vertical = true;          // wide: cut across
        else if (r.h * 4 > r.w * 5) vertical = false;     // tall
        else vertical = rng.chance(0.5f);
    } else {
        vertical = canX && wantX;
    }

    int32_t size = vertical ? r.w : r.h;
    int32_t at = rng.range(leafMin, size - leafMin);
    n.a = std::make_unique<Node>();
    n.b = std::make_unique<Node>();
    if (vertical) {
        n.a->area = {r.x, r.y, at, r.h};
        n.b->area = {r.x + at, r.y, r.w - at, r.h};
    } else {
        n.a->area = {r.x, r.y, r.w, at};
        n.b->area = {r.x, r.y + at, r.w, r.h - at};
    }
    split(rng, *n.a, o);
    split(rng, *n.b, o);
}

void placeRooms(Rng& rng, Node& n, const DungeonOptions& o, std::vector<GridRect>& rooms) {
    if (n.a) {
        placeRooms(rng, *n.a, o, rooms);
        placeRooms(rng, *n.b, o, rooms);
        return;
    }
    const GridRect r = n.area;
    int32_t maxW = std::min(o.maxRoom, r.w - 2 * o.padding);
    int32_t maxH = std::min(o.maxRoom, r.h - 2 * o.padding);
    if (maxW < 1 || maxH < 1) return;
    int32_t w = rng.range(std::min(o.minRoom, maxW), maxW);
    int32_t h = rng.range(std::min(o.minRoom, maxH), maxH);
    int32_t x = r.x + o.padding + rng.range(0, maxW - w);
    int32_t y = r.y + o.padding + rng.range(0, maxH - h);
    n.room = static_cast<int32_t>(rooms.size());
    rooms.push_back({x, y, w, h});
}

// The rooms under a node.
void roomsUnder(const Node& n, std::vector<int32_t>& out) {
    if (n.room >= 0) out.push_back(n.room);
    if (n.a) roomsUnder(*n.a, out);
    if (n.b) roomsUnder(*n.b, out);
}

void carveCorridor(Rng& rng, Grid& g, GridPoint from, GridPoint to, int32_t width) {
    auto carve = [&](int32_t x, int32_t y) {
        for (int32_t dy = 0; dy < width; ++dy)
            for (int32_t dx = 0; dx < width; ++dx) {
                int32_t cx = x + dx, cy = y + dy;
                if (cx > 0 && cy > 0 && cx < g.width - 1 && cy < g.height - 1) g.set(cx, cy, GridFloor);
            }
    };
    bool horizontalFirst = rng.chance(0.5f);
    GridPoint c = from;
    auto walkX = [&] {
        while (c.x != to.x) { carve(c.x, c.y); c.x += to.x > c.x ? 1 : -1; }
    };
    auto walkY = [&] {
        while (c.y != to.y) { carve(c.x, c.y); c.y += to.y > c.y ? 1 : -1; }
    };
    if (horizontalFirst) { walkX(); walkY(); } else { walkY(); walkX(); }
    carve(c.x, c.y);
}

int64_t distanceSq(GridPoint a, GridPoint b) {
    int64_t dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Joins the two halves of every internal node through their closest rooms.
void connect(Rng& rng, const Node& n, const std::vector<GridRect>& rooms, Grid& g, int32_t width) {
    if (!n.a) return;
    connect(rng, *n.a, rooms, g, width);
    connect(rng, *n.b, rooms, g, width);
    std::vector<int32_t> left, right;
    roomsUnder(*n.a, left);
    roomsUnder(*n.b, right);
    if (left.empty() || right.empty()) return;
    int32_t bestA = left[0], bestB = right[0];
    int64_t best = std::numeric_limits<int64_t>::max();
    for (int32_t a : left)
        for (int32_t b : right) {
            int64_t d = distanceSq(rooms[a].center(), rooms[b].center());
            if (d < best) { best = d; bestA = a; bestB = b; }
        }
    carveCorridor(rng, g, rooms[bestA].center(), rooms[bestB].center(), width);
}

} // namespace

Dungeon generateDungeon(Rng& rng, int32_t width, int32_t height, const DungeonOptions& opt) {
    DungeonOptions o = opt;
    o.minRoom = std::max(1, o.minRoom);
    o.maxRoom = std::max(o.minRoom, o.maxRoom);
    o.padding = std::max(1, o.padding);
    o.corridorWidth = std::max(1, o.corridorWidth);

    Dungeon d;
    d.grid = Grid(width, height, GridWall);
    if (width < 3 || height < 3) return d;

    // The border stays wall: partitions cover the inside only.
    Node root;
    root.area = {1, 1, width - 2, height - 2};
    split(rng, root, o);
    placeRooms(rng, root, o, d.rooms);
    for (const GridRect& r : d.rooms) d.grid.fillRect(r.x, r.y, r.w, r.h, GridFloor);
    connect(rng, root, d.rooms, d.grid, o.corridorWidth);

    // Loops: corridors between a random room and its nearest neighbour
    // that is not already joined to it this way.
    if (d.rooms.size() > 2) {
        for (int32_t i = 0; i < o.extraCorridors; ++i) {
            int32_t a = static_cast<int32_t>(rng.below(static_cast<uint32_t>(d.rooms.size())));
            int32_t b = -1;
            int64_t best = std::numeric_limits<int64_t>::max();
            int64_t second = std::numeric_limits<int64_t>::max();
            int32_t secondIdx = -1;
            for (int32_t j = 0; j < static_cast<int32_t>(d.rooms.size()); ++j) {
                if (j == a) continue;
                int64_t dist = distanceSq(d.rooms[a].center(), d.rooms[j].center());
                if (dist < best) {
                    second = best; secondIdx = b;
                    best = dist; b = j;
                } else if (dist < second) {
                    second = dist; secondIdx = j;
                }
            }
            // The nearest room is usually joined already; the second
            // nearest makes the loop.
            int32_t to = secondIdx >= 0 ? secondIdx : b;
            if (to >= 0) carveCorridor(rng, d.grid, d.rooms[a].center(), d.rooms[to].center(), o.corridorWidth);
        }
    }

    if (o.doors) {
        // A door is a floor cell just outside a room, between two walls
        // along the room's side: the mouth of a corridor.
        auto inRoom = [&](int32_t x, int32_t y) {
            for (const GridRect& r : d.rooms)
                if (r.contains({x, y})) return true;
            return false;
        };
        Grid& g = d.grid;
        for (const GridRect& r : d.rooms) {
            auto check = [&](int32_t x, int32_t y, bool horizontalSide) {
                if (g.get(x, y) != GridFloor || inRoom(x, y)) return;
                bool walls = horizontalSide
                    ? g.get(x - 1, y) == GridWall && g.get(x + 1, y) == GridWall
                    : g.get(x, y - 1) == GridWall && g.get(x, y + 1) == GridWall;
                // One door per corridor mouth: a short corridor touching
                // the room twice keeps the first.
                bool nextToDoor = g.get(x - 1, y) == GridDoor || g.get(x + 1, y) == GridDoor ||
                                  g.get(x, y - 1) == GridDoor || g.get(x, y + 1) == GridDoor;
                if (walls && !nextToDoor) g.set(x, y, GridDoor);
            };
            for (int32_t x = r.x; x < r.x + r.w; ++x) {
                check(x, r.y - 1, true);
                check(x, r.y + r.h, true);
            }
            for (int32_t y = r.y; y < r.y + r.h; ++y) {
                check(r.x - 1, y, false);
                check(r.x + r.w, y, false);
            }
        }
    }
    return d;
}

// ---------------------------------------------------------------------------
// Random walk
// ---------------------------------------------------------------------------

Grid randomWalk(Rng& rng, int32_t width, int32_t height, const WalkOptions& o) {
    Grid g(width, height, GridWall);
    if (width < 3 || height < 3) return g;
    const int32_t brush = std::max(1, o.brush);
    const int32_t minX = 1, minY = 1, maxX = width - 1 - brush, maxY = height - 1 - brush;
    if (maxX < minX || maxY < minY) return g;

    GridPoint start = o.start;
    if (start.x < 0 || start.y < 0) start = {width / 2, height / 2};
    start.x = std::clamp(start.x, minX, maxX);
    start.y = std::clamp(start.y, minY, maxY);

    const size_t interior = static_cast<size_t>(width - 2) * static_cast<size_t>(height - 2);
    const size_t target = static_cast<size_t>(std::clamp(o.coverage, 0.f, 1.f) * static_cast<float>(interior));
    const size_t maxSteps = interior * 50;

    struct Walker { GridPoint p; int dir; };
    static constexpr int dx[4] = {1, 0, -1, 0};
    static constexpr int dy[4] = {0, 1, 0, -1};
    std::vector<Walker> walkers(static_cast<size_t>(std::max(1, o.walkers)));
    for (Walker& w : walkers) w = {start, static_cast<int>(rng.below(4))};

    size_t open = 0;
    auto carve = [&](GridPoint p) {
        for (int32_t y = 0; y < brush; ++y)
            for (int32_t x = 0; x < brush; ++x) {
                if (g.get(p.x + x, p.y + y) != GridWall) continue;
                g.set(p.x + x, p.y + y, GridFloor);
                ++open;
            }
    };
    carve(start);
    for (size_t step = 0; step < maxSteps && open < target; ++step) {
        for (Walker& w : walkers) {
            if (rng.chance(o.turnChance)) w.dir = static_cast<int>(rng.below(4));
            GridPoint n{w.p.x + dx[w.dir], w.p.y + dy[w.dir]};
            if (n.x < minX || n.y < minY || n.x > maxX || n.y > maxY) {
                w.dir = static_cast<int>(rng.below(4));   // bounce off the edge
                continue;
            }
            w.p = n;
            carve(n);
            if (open >= target) break;
        }
    }
    return g;
}

// ---------------------------------------------------------------------------
// Poisson-disk sampling
// ---------------------------------------------------------------------------

namespace {

// Bridson's algorithm over a background grid of r / sqrt(2) cells, each
// holding at most one point. seed() starts growth from a point; grow()
// spreads from every active point until none can place another.
class Poisson {
public:
    Poisson(Rng& rng, const Rect& area, float r, int32_t attempts, const std::function<bool(Vec2)>& accept)
        : rng_(rng), area_(area), r_(r), r2_(r * r), attempts_(std::max(1, attempts)), accept_(accept) {
        cell_ = r / std::sqrt(2.f);
        gw_ = std::max(1, static_cast<int32_t>(std::ceil(area.w / cell_)));
        gh_ = std::max(1, static_cast<int32_t>(std::ceil(area.h / cell_)));
        slots_.assign(static_cast<size_t>(gw_) * static_cast<size_t>(gh_), -1);
    }

    bool fits(Vec2 p) const {
        if (p.x < area_.x || p.y < area_.y || p.x >= area_.x + area_.w || p.y >= area_.y + area_.h) return false;
        int32_t sx, sy;
        slotOf(p, sx, sy);
        for (int32_t y = std::max(0, sy - 2); y <= std::min(gh_ - 1, sy + 2); ++y)
            for (int32_t x = std::max(0, sx - 2); x <= std::min(gw_ - 1, sx + 2); ++x) {
                int32_t i = slots_[static_cast<size_t>(y) * gw_ + x];
                if (i < 0) continue;
                float dx = points_[i].x - p.x, dy = points_[i].y - p.y;
                if (dx * dx + dy * dy < r2_) return false;
            }
        return !accept_ || accept_(p);
    }

    bool seed(Vec2 p) {
        if (!fits(p)) return false;
        add(p);
        return true;
    }

    void grow() {
        while (!active_.empty()) {
            uint32_t pick = rng_.below(static_cast<uint32_t>(active_.size()));
            Vec2 base = points_[active_[pick]];
            bool found = false;
            for (int32_t k = 0; k < attempts_ && !found; ++k) {
                // A uniform point in the ring r..2r around base, by
                // rejection from the enclosing square (no sin or cos, so
                // every platform agrees).
                Vec2 off;
                float d2;
                do {
                    off = {rng_.range(-2.f * r_, 2.f * r_), rng_.range(-2.f * r_, 2.f * r_)};
                    d2 = off.x * off.x + off.y * off.y;
                } while (d2 < r2_ || d2 > 4.f * r2_);
                found = seed({base.x + off.x, base.y + off.y});
            }
            if (!found) {
                active_[pick] = active_.back();
                active_.pop_back();
            }
        }
    }

    std::vector<Vec2> take() { return std::move(points_); }

private:
    void slotOf(Vec2 p, int32_t& sx, int32_t& sy) const {
        sx = std::clamp(static_cast<int32_t>((p.x - area_.x) / cell_), 0, gw_ - 1);
        sy = std::clamp(static_cast<int32_t>((p.y - area_.y) / cell_), 0, gh_ - 1);
    }
    void add(Vec2 p) {
        int32_t sx, sy;
        slotOf(p, sx, sy);
        slots_[static_cast<size_t>(sy) * gw_ + sx] = static_cast<int32_t>(points_.size());
        active_.push_back(static_cast<int32_t>(points_.size()));
        points_.push_back(p);
    }

    Rng& rng_;
    Rect area_;
    float r_, r2_, cell_;
    int32_t attempts_, gw_, gh_;
    std::function<bool(Vec2)> accept_;
    std::vector<int32_t> slots_;
    std::vector<int32_t> active_;
    std::vector<Vec2> points_;
};

} // namespace

std::vector<Vec2> poissonDisk(Rng& rng, const Rect& area, float r, int32_t attempts,
                              const std::function<bool(Vec2)>& accept) {
    if (r <= 0.f || area.w <= 0.f || area.h <= 0.f) return {};
    Poisson p(rng, area, r, attempts, accept);
    // The first point goes anywhere accepted; a tight mask may take tries.
    for (int32_t i = 0; i < std::max(1, attempts) * 10; ++i) {
        if (p.seed({rng.range(area.x, area.x + area.w), rng.range(area.y, area.y + area.h)})) break;
    }
    p.grow();
    return p.take();
}

std::vector<Vec2> poissonDiskOnGrid(Rng& rng, const Grid& grid, int32_t value, float minDistance,
                                    int32_t attempts) {
    if (minDistance <= 0.f || grid.width <= 0 || grid.height <= 0) return {};
    Rect area{0.f, 0.f, static_cast<float>(grid.width), static_cast<float>(grid.height)};
    auto inside = [&](Vec2 v) {
        return grid.get(static_cast<int32_t>(std::floor(v.x)), static_cast<int32_t>(std::floor(v.y)),
                        value == GridWall ? GridFloor : GridWall) == value;
    };
    Poisson p(rng, area, minDistance, attempts, inside);
    // Seed from the matching cells in random order, so pockets the growth
    // cannot reach (rooms behind thin corridors, islands) get points too.
    std::vector<GridPoint> cells;
    for (int32_t y = 0; y < grid.height; ++y)
        for (int32_t x = 0; x < grid.width; ++x)
            if (grid.get(x, y) == value) cells.push_back({x, y});
    rng.shuffle(std::span<GridPoint>(cells));
    for (GridPoint c : cells) {
        Vec2 at{static_cast<float>(c.x) + rng.next01(), static_cast<float>(c.y) + rng.next01()};
        if (p.seed(at)) p.grow();
    }
    return p.take();
}

// ---------------------------------------------------------------------------
// Noise
// ---------------------------------------------------------------------------

Grid gridFromNoise(uint32_t seed, int32_t width, int32_t height, const NoiseOptions& options,
                   float threshold, int32_t above, int32_t below) {
    Grid g(width, height, below);
    Noise noise(seed);
    for (int32_t y = 0; y < height; ++y)
        for (int32_t x = 0; x < width; ++x) {
            float v = noise.sample(options, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
            g.set(x, y, v >= threshold ? above : below);
        }
    return g;
}

} // namespace drift
