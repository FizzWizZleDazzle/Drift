#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/Resource.hpp>
#include <drift/components/TrailRenderer.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>

#include <cmath>
#include <unordered_map>
#include <vector>

namespace drift {

#ifndef SWIG
struct TrailPoint {
    Vec2 position;
    float age;
};

struct TrailState {
    std::vector<TrailPoint> points;
    int32_t head = 0;       // ring buffer write index
    int32_t count = 0;
    int32_t capacity = 0;

    void init(int32_t maxPts) {
        capacity = maxPts;
        points.resize(maxPts);
        head = 0;
        count = 0;
    }

    void addPoint(Vec2 pos) {
        points[head] = {pos, 0.f};
        head = (head + 1) % capacity;
        if (count < capacity) ++count;
    }

    // Get point by age order (0 = oldest)
    TrailPoint& getOrdered(int32_t i) {
        int32_t idx = (head - count + i + capacity) % capacity;
        return points[idx];
    }
};

struct TrailSystemResource : public Resource {
    DRIFT_RESOURCE(TrailSystemResource)

    std::unordered_map<EntityId, TrailState> trails;
};

inline void trailUpdate(ResMut<TrailSystemResource> trailRes,
                         Res<Time> time,
                         Query<TrailRenderer, Transform2D> trailEntities) {
    float dt = time->delta;

    std::vector<EntityId> activeEntities;

    trailEntities.iterWithEntity([&](EntityId id, const TrailRenderer& trail, const Transform2D& transform) {
        activeEntities.push_back(id);

        auto it = trailRes->trails.find(id);
        if (it == trailRes->trails.end()) {
            TrailState state;
            state.init(trail.maxPoints);
            trailRes->trails[id] = std::move(state);
            it = trailRes->trails.find(id);
        }

        TrailState& state = it->second;

        // Age existing points and remove expired ones
        for (int32_t i = 0; i < state.count; ++i) {
            state.getOrdered(i).age += dt;
        }
        // Remove old points from the back (oldest first)
        while (state.count > 0 && state.getOrdered(0).age >= trail.lifetime) {
            --state.count;
        }

        // Check if we should add a new point
        bool shouldAdd = true;
        if (state.count > 0) {
            TrailPoint& newest = state.getOrdered(state.count - 1);
            float dx = transform.position.x - newest.position.x;
            float dy = transform.position.y - newest.position.y;
            if (dx * dx + dy * dy < trail.minDistance * trail.minDistance) {
                shouldAdd = false;
            }
        }

        if (shouldAdd) {
            state.addPoint(transform.position);
        }
    });

    // Clean up trails for removed entities
    for (auto it = trailRes->trails.begin(); it != trailRes->trails.end();) {
        bool found = false;
        for (EntityId eid : activeEntities) {
            if (eid == it->first) { found = true; break; }
        }
        if (!found) {
            it = trailRes->trails.erase(it);
        } else {
            ++it;
        }
    }
}

inline void trailRender(Res<TrailSystemResource> trailRes,
                         ResMut<RendererResource> renderer,
                         Query<TrailRenderer> trailEntities) {
    trailEntities.iterWithEntity([&](EntityId id, const TrailRenderer& trail) {
        auto it = trailRes->trails.find(id);
        if (it == trailRes->trails.end()) return;

        const TrailState& state = it->second;
        if (state.count < 2) return;

        for (int32_t i = 0; i < state.count - 1; ++i) {
            // Access via const_cast since getOrdered is non-const but we know we're reading
            TrailState& mutableState = const_cast<TrailState&>(state);
            const TrailPoint& a = mutableState.getOrdered(i);
            const TrailPoint& b = mutableState.getOrdered(i + 1);

            float tA = a.age / trail.lifetime;
            float tB = b.age / trail.lifetime;
            float tMid = (tA + tB) * 0.5f;

            // Interpolate color
            auto lerpC = [](float a, float b, float t) -> uint8_t {
                float v = a + (b - a) * t;
                return static_cast<uint8_t>(v * 255.f);
            };

            Color color;
            color.r = lerpC(trail.colorStart.r, trail.colorEnd.r, tMid);
            color.g = lerpC(trail.colorStart.g, trail.colorEnd.g, tMid);
            color.b = lerpC(trail.colorStart.b, trail.colorEnd.b, tMid);
            color.a = lerpC(trail.colorStart.a, trail.colorEnd.a, tMid);

            // Width tapers based on age
            float width = trail.width * (1.f - tMid);
            if (width < 0.5f) width = 0.5f;

            renderer->drawLine(a.position, b.position, color, width);
        }
    });
}
#endif

class TrailPlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        app.world().registerComponent<TrailRenderer>("TrailRenderer");
        app.initResource<TrailSystemResource>();
        app.addSystem<trailUpdate>("trail_update", Phase::PostUpdate);
        app.addSystem<trailRender>("trail_render", Phase::Render);
#endif
    }
    DRIFT_PLUGIN(TrailPlugin)
};

} // namespace drift
