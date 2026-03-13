#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/resources/ParticleResource.hpp>
#include <drift/resources/RendererResource.hpp>

namespace drift {

#ifndef SWIG
inline void particles_update(ResMut<ParticleResource> particles, float dt) { particles->update(dt); }
inline void particles_render(ResMut<ParticleResource> particles, float) { particles->render(); }
#endif

class ParticlePlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        app.addResource<ParticleResource>(*app.getResource<RendererResource>());
        app.addSystem<particles_update>("particles_update", Phase::PostUpdate);
        app.addSystem<particles_render>("particles_render", Phase::Render);
#endif
    }
    DRIFT_PLUGIN(ParticlePlugin)
};

} // namespace drift
