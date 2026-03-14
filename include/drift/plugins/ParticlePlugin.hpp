#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/resources/ParticleResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>

namespace drift {

#ifndef SWIG
inline void particles_update(ResMut<ParticleResource> particles, Res<Time> time) { particles->update(time->delta); }
inline void particles_render(ResMut<ParticleResource> particles) { particles->render(); }
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
