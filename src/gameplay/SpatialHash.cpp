#include <drift/gameplay/SpatialHash.hpp>
#include <drift/math/Intersect.hpp>

#include <cmath>
#include <limits>

namespace drift {

SpatialHash::SpatialHash(float cellSize) {
    setCellSize(cellSize);
}

void SpatialHash::setCellSize(float cellSize) {
    cellSize_ = cellSize > 0.f ? cellSize : 64.f;
    invCell_ = 1.f / cellSize_;
    items_.clear();
    cells_.clear();
}

void SpatialHash::clear() {
    items_.clear();
    // Keep bucket storage between frames unless it has grown far past use.
    if (cells_.size() > 4096) {
        cells_.clear();
        return;
    }
    for (auto& [k, v] : cells_) v.clear();
}

SpatialHash::CellRange SpatialHash::range(const Rect& r) const {
    auto cell = [this](float v) {
        float c = std::floor(v * invCell_);
        constexpr float lim = 1e9f;
        return static_cast<int32_t>(c < -lim ? -lim : (c > lim ? lim : c));
    };
    return {cell(r.x), cell(r.y), cell(r.x + r.w), cell(r.y + r.h)};
}

void SpatialHash::insert(EntityId id, const Rect& bounds) {
    uint32_t index = static_cast<uint32_t>(items_.size());
    items_.push_back({id, bounds});
    CellRange c = range(bounds);
    for (int32_t y = c.y0; y <= c.y1; ++y)
        for (int32_t x = c.x0; x <= c.x1; ++x)
            cells_[key(x, y)].push_back(index);
}

// Calls fn(item) once per item stored in a cell the area touches. An item
// in several of those cells is reported only from the first shared cell,
// so no per-query bookkeeping is needed.
template<typename Fn>
void SpatialHash::visit(const Rect& area, Fn&& fn) const {
    CellRange q = range(area);
    for (int32_t y = q.y0; y <= q.y1; ++y) {
        for (int32_t x = q.x0; x <= q.x1; ++x) {
            auto it = cells_.find(key(x, y));
            if (it == cells_.end()) continue;
            for (uint32_t index : it->second) {
                const Item& item = items_[index];
                CellRange r = range(item.bounds);
                int32_t fx = r.x0 > q.x0 ? r.x0 : q.x0;
                int32_t fy = r.y0 > q.y0 ? r.y0 : q.y0;
                if (fx != x || fy != y) continue;
                fn(item);
            }
        }
    }
}

namespace {

// Closed-interval overlap, so zero-size (point) bounds on an edge count.
bool touches(const Rect& a, const Rect& b) {
    return a.x <= b.x + b.w && a.x + a.w >= b.x && a.y <= b.y + b.h && a.y + a.h >= b.y;
}

} // namespace

void SpatialHash::queryRect(const Rect& area, std::vector<EntityId>& out) const {
    visit(area, [&](const Item& item) {
        if (touches(item.bounds, area)) out.push_back(item.id);
    });
}

void SpatialHash::queryRadius(Vec2 center, float radius, std::vector<EntityId>& out) const {
    Rect area{center.x - radius, center.y - radius, radius * 2.f, radius * 2.f};
    Circle circle{center, radius};
    visit(area, [&](const Item& item) {
        if (overlaps(circle, item.bounds)) out.push_back(item.id);
    });
}

EntityId SpatialHash::nearest(Vec2 point, float maxDistance, EntityId ignore) const {
    EntityId best = InvalidEntityId;
    float bestD2 = maxDistance * maxDistance;
    Rect area{point.x - maxDistance, point.y - maxDistance, maxDistance * 2.f, maxDistance * 2.f};
    visit(area, [&](const Item& item) {
        if (item.id == ignore) return;
        float d2 = distanceSq(closestPoint(item.bounds, point), point);
        if (d2 <= bestD2) {
            bestD2 = d2;
            best = item.id;
        }
    });
    return best;
}

} // namespace drift
