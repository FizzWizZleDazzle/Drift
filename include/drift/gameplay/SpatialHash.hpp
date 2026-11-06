#pragma once

// Uniform-grid spatial hash: buckets items by the cells their bounds touch,
// so "what is near here" queries only look at nearby items instead of all
// of them. Rebuild it each frame (clear + insert) for moving things.
// Queries are const and safe to run from several threads at once.

#include <drift/Types.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace drift {

class SpatialHash {
public:
    explicit SpatialHash(float cellSize = 64.f);

    // Changing the cell size clears the hash.
    void setCellSize(float cellSize);
    float cellSize() const { return cellSize_; }

    void clear();
    void insert(EntityId id, const Rect& bounds);
    void insert(EntityId id, Vec2 point) { insert(id, Rect{point.x, point.y, 0.f, 0.f}); }
    size_t size() const { return items_.size(); }

    // Items whose bounds overlap the rect. Results are appended to `out`,
    // each item at most once.
    void queryRect(const Rect& area, std::vector<EntityId>& out) const;
    // Items whose bounds come within `radius` of `center`.
    void queryRadius(Vec2 center, float radius, std::vector<EntityId>& out) const;
    // The nearest item to `point` within maxDistance (by bounds), or
    // InvalidEntityId. `ignore` is skipped (pass the asking entity).
    EntityId nearest(Vec2 point, float maxDistance, EntityId ignore = InvalidEntityId) const;

    std::vector<EntityId> queryRect(const Rect& area) const { std::vector<EntityId> v; queryRect(area, v); return v; }
    std::vector<EntityId> queryRadius(Vec2 c, float r) const { std::vector<EntityId> v; queryRadius(c, r, v); return v; }

private:
    struct Item {
        EntityId id;
        Rect bounds;
    };
    struct CellRange { int32_t x0, y0, x1, y1; };

    CellRange range(const Rect& r) const;
    static uint64_t key(int32_t x, int32_t y) {
        return (uint64_t(uint32_t(x)) << 32) | uint32_t(y);
    }
    template<typename Fn> void visit(const Rect& area, Fn&& fn) const;

    float cellSize_;
    float invCell_;
    std::vector<Item> items_;
    std::unordered_map<uint64_t, std::vector<uint32_t>> cells_;
};

} // namespace drift
