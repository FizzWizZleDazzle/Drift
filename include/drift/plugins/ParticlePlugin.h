#pragma once

#include <drift/Plugin.h>
#include <drift/App.h>
#include <drift/resources/ParticleResource.h>
#include <drift/resources/RendererResource.h>

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
