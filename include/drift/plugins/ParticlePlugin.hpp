#pragma once

#include <drift/Plugin.hpp>
#include <drift/Reflect.hpp>
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
        reflect::enumType<EmissionShape>("EmissionShape", {
            {"Point", EmissionShape::Point}, {"Circle", EmissionShape::Circle},
            {"Ring", EmissionShape::Ring}, {"Box", EmissionShape::Box},
            {"Line", EmissionShape::Line}});
        reflect::enumType<ParticleSpace>("ParticleSpace", {
            {"World", ParticleSpace::World}, {"Local", ParticleSpace::Local}});
        reflect::type<ValueRange>("ValueRange")
            .field("min", &ValueRange::min)
            .field("max", &ValueRange::max);
        reflect::type<BurstEntry>("BurstEntry")
            .field("time", &BurstEntry::time)
            .field("count", &BurstEntry::count)
            .field("cycles", &BurstEntry::cycles)
            .field("interval", &BurstEntry::interval);
        reflect::type<EmitterConfig>("EmitterConfig")
            .field("texture", &EmitterConfig::texture)
            .field("srcRect", &EmitterConfig::srcRect)
            .field("shape", &EmitterConfig::shape)
            .field("shapeRadius", &EmitterConfig::shapeRadius)
            .field("shapeExtents", &EmitterConfig::shapeExtents)
            .field("spawnRate", &EmitterConfig::spawnRate)
            .field("maxParticles", &EmitterConfig::maxParticles)
            .field("bursts", &EmitterConfig::bursts)
            .field("lifetime", &EmitterConfig::lifetime)
            .field("speed", &EmitterConfig::speed)
            .field("angle", &EmitterConfig::angle)
            .field("velocityInheritance", &EmitterConfig::velocityInheritance)
            .field("gravity", &EmitterConfig::gravity)
            .field("drag", &EmitterConfig::drag)
            .field("sizeStart", &EmitterConfig::sizeStart)
            .field("sizeEnd", &EmitterConfig::sizeEnd)
            .field("initialRotation", &EmitterConfig::initialRotation)
            .field("angularVelocity", &EmitterConfig::angularVelocity)
            .field("colorStart", &EmitterConfig::colorStart)
            .field("colorEnd", &EmitterConfig::colorEnd)
            .field("blendMode", &EmitterConfig::blendMode)
            .field("zOrder", &EmitterConfig::zOrder)
            .field("duration", &EmitterConfig::duration)
            .field("looping", &EmitterConfig::looping)
            .field("space", &EmitterConfig::space)
            .field("preWarmTime", &EmitterConfig::preWarmTime)
            .field("boundsMin", &EmitterConfig::boundsMin)
            .field("boundsMax", &EmitterConfig::boundsMax)
            .field("seed", &EmitterConfig::seed);
        reflect::component<ParticleEmitter>(app.world(), "ParticleEmitter")
            .field("config", &ParticleEmitter::config)
            .field("playing", &ParticleEmitter::playing)
            .field("oneShot", &ParticleEmitter::oneShot);

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
