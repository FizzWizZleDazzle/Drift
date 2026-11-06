#pragma once

#include <drift/Plugin.hpp>
#include <drift/Reflect.hpp>
#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/Query.hpp>
#include <drift/components/Behavior.hpp>
#include <drift/resources/Time.hpp>

#include <cmath>
#include <unordered_map>
#include <vector>

namespace drift {

// Small declarative behaviors that run as ordinary systems in PostUpdate:
// Lifetime, Oscillate and Follow. Velocity2D (moving without a physics
// body) lives in PhysicsPlugin. Each system declares its access, so the
// scheduler runs them on worker threads beside other systems.

inline void lifetimeSystem(Res<Time> time, QueryMut<Lifetime> lifetimes, Commands& commands) {
    float dt = time->delta;
    lifetimes.iterWithEntity([&](EntityId e, Lifetime& l) {
        l.seconds -= dt;
        if (l.seconds <= 0.f) commands.despawn(e);
    });
}

inline void oscillateSystem(Res<Time> time, QueryMut<Transform2D, Oscillate> movers) {
    float dt = time->delta;
    movers.iter([&](Transform2D& t, Oscillate& o) {
        o.elapsed += dt;
        float s = std::sin(6.2831853f * o.frequency * o.elapsed + o.phase);
        Vec2 offset = o.amplitude * s;
        t.position += offset - o.applied;
        o.applied = offset;
    });
}

inline void followSystem(Res<Time> time, QueryMut<Transform2D, Follow> followers,
                         Query<Transform2D> targets) {
    float dt = time->delta;
    std::unordered_map<EntityId, Vec2> wanted;
    followers.iter([&](Transform2D&, Follow& f) {
        if (f.target != InvalidEntityId) wanted.emplace(f.target, Vec2{});
    });
    if (wanted.empty()) return;
    std::unordered_map<EntityId, Vec2> found;
    targets.iterWithEntity([&](EntityId e, const Transform2D& t) {
        if (wanted.count(e)) found[e] = t.position;
    });
    followers.iter([&](Transform2D& t, Follow& f) {
        auto it = found.find(f.target);
        if (it == found.end()) return;
        Vec2 goal = it->second + f.offset;
        Vec2 d = goal - t.position;
        float len = d.length();
        if (len <= f.distance || len <= 1e-6f) return;
        float step = std::min(f.speed * dt, len - f.distance);
        t.position += d * (step / len);
    });
}

class BehaviorPlugin : public Plugin {
public:
    void build(App& app) override {
        reflect::component<Lifetime>(app.world(), "Lifetime")
            .field("seconds", &Lifetime::seconds);
        reflect::component<Oscillate>(app.world(), "Oscillate")
            .field("amplitude", &Oscillate::amplitude)
            .field("frequency", &Oscillate::frequency)
            .field("phase", &Oscillate::phase)
            .field("elapsed", &Oscillate::elapsed)
            .field("applied", &Oscillate::applied);
        reflect::component<Follow>(app.world(), "Follow")
            .entity("target", &Follow::target)
            .field("speed", &Follow::speed)
            .field("distance", &Follow::distance)
            .field("offset", &Follow::offset);
        app.addSystem<lifetimeSystem>("lifetime", Phase::PostUpdate);
        app.addSystem<oscillateSystem>("oscillate", Phase::PostUpdate);
        app.addSystem<followSystem>("follow", Phase::PostUpdate);
    }
    DRIFT_PLUGIN(BehaviorPlugin)
};

} // namespace drift
