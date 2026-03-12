#pragma once

#include <drift/Plugin.h>
#include <drift/App.h>
#include <drift/resources/PhysicsResource.h>

namespace drift {

#ifndef SWIG
inline void physics_step(ResMut<PhysicsResource> physics, float dt) { physics->step(dt); }
#endif

class PhysicsPlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        app.addResource<PhysicsResource>(Vec2{0.f, 9.8f});
        app.addSystem<physics_step>("physics_step", Phase::PostUpdate);
#endif
    }
    DRIFT_PLUGIN(PhysicsPlugin)
};

} // namespace drift
