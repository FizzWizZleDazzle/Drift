#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/gameplay/SpatialHash.hpp>

namespace drift {

// Marks an entity for the SpatialIndex. Bounds are a box of halfSize around
// Transform2D::position + offset; a zero halfSize indexes the point.
struct SpatialBounds {
    Vec2 halfSize = {};
    Vec2 offset = {};
};

// SpatialHash over every entity with Transform2D + SpatialBounds, rebuilt at
// the start of each frame from the positions the previous frame ended with.
class SpatialIndex : public Resource, public SpatialHash {
public:
    DRIFT_RESOURCE(SpatialIndex)
    explicit SpatialIndex(float cellSize = 64.f) : SpatialHash(cellSize) {}
};

inline void spatialIndexRebuild(ResMut<SpatialIndex> index, Query<Transform2D, SpatialBounds> bodies) {
    index->clear();
    bodies.iterWithEntity([&](EntityId e, const Transform2D& t, const SpatialBounds& b) {
        Vec2 c = t.position + b.offset;
        index->insert(e, Rect{c.x - b.halfSize.x, c.y - b.halfSize.y, b.halfSize.x * 2.f, b.halfSize.y * 2.f});
    });
}

class SpatialPlugin : public Plugin {
public:
    explicit SpatialPlugin(float cellSize = 64.f) : cellSize_(cellSize) {}

    void build(App& app) override {
        app.world().registerComponent<SpatialBounds>("SpatialBounds");
        app.initResource<SpatialIndex>(cellSize_);
        app.addSystem<spatialIndexRebuild>("spatial_index_rebuild", Phase::PreUpdate);
    }
    DRIFT_PLUGIN(SpatialPlugin)

private:
    float cellSize_;
};

} // namespace drift
