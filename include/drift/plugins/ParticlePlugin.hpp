#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/resources/ParticleResource.hpp>
#include <drift/resources/ParticleSystemResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>
#include <drift/components/ParticleEmitter.hpp>
#include <drift/Query.hpp>

namespace drift {

// New ECS-based particle system functions (defined in ParticleSystem.cpp)
void particleSystemUpdate(ResMut<ParticleSystemResource> particles,
                          Res<Time> time,
                          QueryMut<ParticleEmitter, Transform2D> emitters);

void particleSystemRender(Res<ParticleSystemResource> particles,
                          ResMut<RendererResource> renderer,
                          Query<ParticleEmitter, Transform2D> emitters);

// Legacy system functions
inline void particles_update(ResMut<ParticleResource> particles, Res<Time> time) { particles->update(time->delta); }
inline void particles_render(ResMut<ParticleResource> particles) { particles->render(); }

class ParticlePlugin : public Plugin {
public:
    void build(App& app) override {
        // Legacy resource (for backward compatibility)
        app.addResource<ParticleResource>(*app.getResource<RendererResource>());
        app.addSystem<particles_update>("particles_update", Phase::PostUpdate);
        app.addSystem<particles_render>("particles_render", Phase::Render);

        // New ECS-based particle system
        app.initResource<ParticleSystemResource>();
        app.addSystem<particleSystemUpdate>("particle_system_update", Phase::PostUpdate);
        app.addSystem<particleSystemRender>("particle_system_render", Phase::Render);
    }
    DRIFT_PLUGIN(ParticlePlugin)
};

} // namespace drift
