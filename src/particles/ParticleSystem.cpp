#include <drift/particles/ParticlePool.hpp>
#include <drift/particles/EmitterConfig.hpp>
#include <drift/components/ParticleEmitter.hpp>
#include <drift/resources/ParticleSystemResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>
#include <drift/Resource.hpp>
#include <drift/Query.hpp>
#include <drift/Types.hpp>

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace drift {

// ---------------------------------------------------------------------------
// RNG helper
// ---------------------------------------------------------------------------
static float rngFloat(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
}

static float rngRange(uint32_t& state, float lo, float hi) {
    return lo + rngFloat(state) * (hi - lo);
}

// ---------------------------------------------------------------------------
// Spawn a single particle
// ---------------------------------------------------------------------------
static void spawnParticle(EmitterState& state, const EmitterConfig& config,
                          Vec2 emitterPos) {
    int32_t idx = state.pool.spawn();
    if (idx < 0) return;

    auto& rng = state.rng;

    // Position based on emission shape
    Vec2 pos = emitterPos;
    switch (config.shape) {
        case EmissionShape::Point:
            break;
        case EmissionShape::Circle: {
            float a = rngRange(rng, 0.f, 6.2831853f);
            float r = config.shapeRadius * std::sqrt(rngFloat(rng));
            pos.x += std::cos(a) * r;
            pos.y += std::sin(a) * r;
            break;
        }
        case EmissionShape::Ring: {
            float a = rngRange(rng, 0.f, 6.2831853f);
            pos.x += std::cos(a) * config.shapeRadius;
            pos.y += std::sin(a) * config.shapeRadius;
            break;
        }
        case EmissionShape::Box:
            pos.x += rngRange(rng, -config.shapeExtents.x, config.shapeExtents.x);
            pos.y += rngRange(rng, -config.shapeExtents.y, config.shapeExtents.y);
            break;
        case EmissionShape::Line:
            pos.x += rngRange(rng, -config.shapeExtents.x, config.shapeExtents.x);
            break;
    }

    state.pool.positions[idx] = pos;

    // Velocity from speed + angle
    float spd = config.speed.sample(rng);
    float ang = config.angle.sample(rng);
    state.pool.velocities[idx] = {spd * std::cos(ang), spd * std::sin(ang)};

    // Lifetime
    state.pool.lifetimes[idx] = config.lifetime.sample(rng);
    state.pool.ages[idx] = 0.f;

    // Rotation
    state.pool.rotations[idx] = config.initialRotation.sample(rng);
    state.pool.angularVelocities[idx] = config.angularVelocity.sample(rng);

    // Size
    state.pool.sizeStarts[idx] = config.sizeStart.sample(rng);
    state.pool.sizeEnds[idx] = config.sizeEnd.sample(rng);
    state.pool.sizes[idx] = state.pool.sizeStarts[idx];

    // Color
    state.pool.colorStarts[idx] = config.colorStart;
    state.pool.colorEnds[idx] = config.colorEnd;
}

// ---------------------------------------------------------------------------
// Update system
// ---------------------------------------------------------------------------
void particleSystemUpdate(ResMut<ParticleSystemResource> particles,
                          Res<Time> time,
                          QueryMut<ParticleEmitter, Transform2D> emitters) {
    float dt = time->delta;

    // Track which emitter entities still exist this frame
    std::vector<EntityId> activeEntities;

    emitters.iterWithEntity([&](EntityId id, ParticleEmitter& emitter, Transform2D& transform) {
        activeEntities.push_back(id);

        auto it = particles->emitters.find(id);
        if (it == particles->emitters.end()) {
            // New emitter — initialize state
            EmitterState state;
            state.rng = emitter.config.seed ? emitter.config.seed : static_cast<uint32_t>(id ^ 0xDEADBEEF);
            state.pool.reserve(emitter.config.maxParticles);
            state.burstCyclesDone.resize(emitter.config.bursts.size(), 0);
            state.burstTimers.resize(emitter.config.bursts.size(), 0.f);

            // Pre-warm
            if (emitter.config.preWarmTime > 0.f) {
                float warmDt = 1.f / 60.f;
                float warmTime = emitter.config.preWarmTime;
                while (warmTime > 0.f) {
                    float stepDt = (warmTime < warmDt) ? warmTime : warmDt;
                    // Spawn
                    if (emitter.config.spawnRate > 0.f) {
                        state.spawnAccumulator += stepDt * emitter.config.spawnRate;
                        while (state.spawnAccumulator >= 1.f && state.pool.count < state.pool.capacity) {
                            spawnParticle(state, emitter.config, transform.position);
                            state.spawnAccumulator -= 1.f;
                        }
                    }
                    // Age + physics
                    auto& pool = state.pool;
                    for (int32_t i = pool.count - 1; i >= 0; --i) {
                        pool.ages[i] += stepDt;
                        if (pool.ages[i] >= pool.lifetimes[i]) {
                            pool.kill(i);
                            continue;
                        }
                        pool.velocities[i].x += emitter.config.gravity.x * stepDt;
                        pool.velocities[i].y += emitter.config.gravity.y * stepDt;
                        if (emitter.config.drag > 0.f) {
                            float factor = 1.f - emitter.config.drag * stepDt;
                            if (factor < 0.f) factor = 0.f;
                            pool.velocities[i].x *= factor;
                            pool.velocities[i].y *= factor;
                        }
                        pool.positions[i].x += pool.velocities[i].x * stepDt;
                        pool.positions[i].y += pool.velocities[i].y * stepDt;
                        pool.rotations[i] += pool.angularVelocities[i] * stepDt;
                    }
                    state.elapsed += stepDt;
                    warmTime -= stepDt;
                }
            }

            particles->emitters[id] = std::move(state);
            it = particles->emitters.find(id);
        }

        EmitterState& state = it->second;
        if (state.finished) return;

        Vec2 emitterPos = transform.position;

        if (emitter.playing) {
            // Continuous spawning
            if (emitter.config.spawnRate > 0.f) {
                state.spawnAccumulator += dt * emitter.config.spawnRate;
                while (state.spawnAccumulator >= 1.f && state.pool.count < state.pool.capacity) {
                    spawnParticle(state, emitter.config, emitterPos);
                    state.spawnAccumulator -= 1.f;
                }
            }

            // Process bursts
            for (size_t b = 0; b < emitter.config.bursts.size(); ++b) {
                const auto& burst = emitter.config.bursts[b];
                if (burst.cycles > 0 && state.burstCyclesDone[b] >= burst.cycles) continue;
                if (state.elapsed >= burst.time) {
                    state.burstTimers[b] += dt;
                    float interval = burst.interval > 0.f ? burst.interval : 999999.f;
                    while (state.burstTimers[b] >= interval || state.burstCyclesDone[b] == 0) {
                        for (int32_t c = 0; c < burst.count && state.pool.count < state.pool.capacity; ++c) {
                            spawnParticle(state, emitter.config, emitterPos);
                        }
                        state.burstCyclesDone[b]++;
                        if (burst.cycles > 0 && state.burstCyclesDone[b] >= burst.cycles) break;
                        state.burstTimers[b] -= interval;
                        if (state.burstTimers[b] < 0.f) state.burstTimers[b] = 0.f;
                    }
                }
            }
        }

        // Age particles and kill dead ones (iterate backwards for swap-remove)
        auto& pool = state.pool;
        for (int32_t i = pool.count - 1; i >= 0; --i) {
            pool.ages[i] += dt;
            if (pool.ages[i] >= pool.lifetimes[i]) {
                pool.kill(i);
                continue;
            }

            // Apply gravity
            pool.velocities[i].x += emitter.config.gravity.x * dt;
            pool.velocities[i].y += emitter.config.gravity.y * dt;

            // Apply drag
            if (emitter.config.drag > 0.f) {
                float factor = 1.f - emitter.config.drag * dt;
                if (factor < 0.f) factor = 0.f;
                pool.velocities[i].x *= factor;
                pool.velocities[i].y *= factor;
            }

            // Integrate position
            pool.positions[i].x += pool.velocities[i].x * dt;
            pool.positions[i].y += pool.velocities[i].y * dt;

            // Rotation
            pool.rotations[i] += pool.angularVelocities[i] * dt;

            // Interpolate size
            float t = pool.ages[i] / pool.lifetimes[i];
            pool.sizes[i] = pool.sizeStarts[i] + (pool.sizeEnds[i] - pool.sizeStarts[i]) * t;
        }

        // Local space: offset all particles by emitter movement
        if (emitter.config.space == ParticleSpace::Local) {
            // We don't have previous position stored, so local space means
            // particles are rendered relative to emitter pos in the render pass
        }

        state.elapsed += dt;

        // Check duration
        if (emitter.config.duration > 0.f && state.elapsed >= emitter.config.duration) {
            if (emitter.config.looping) {
                state.elapsed = 0.f;
                // Reset burst tracking
                for (size_t b = 0; b < state.burstCyclesDone.size(); ++b) {
                    state.burstCyclesDone[b] = 0;
                    state.burstTimers[b] = 0.f;
                }
            } else {
                emitter.playing = false;
                if (pool.count == 0) {
                    state.finished = true;
                }
            }
        }

        // One-shot: stop playing once all particles are dead
        if (!emitter.playing && pool.count == 0) {
            state.finished = true;
        }
    });

    // Clean up emitter states for entities that no longer exist
    for (auto it = particles->emitters.begin(); it != particles->emitters.end();) {
        bool found = false;
        for (EntityId eid : activeEntities) {
            if (eid == it->first) { found = true; break; }
        }
        if (!found) {
            it = particles->emitters.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// Render system
// ---------------------------------------------------------------------------
static inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

void particleSystemRender(Res<ParticleSystemResource> particles,
                          ResMut<RendererResource> renderer,
                          Query<ParticleEmitter, Transform2D> emitters) {
    emitters.iterWithEntity([&](EntityId id, const ParticleEmitter& emitter, const Transform2D& transform) {
        auto it = particles->emitters.find(id);
        if (it == particles->emitters.end()) return;

        const EmitterState& state = it->second;
        const ParticlePool& pool = state.pool;
        if (pool.count <= 0) return;

        const EmitterConfig& config = emitter.config;
        TextureHandle tex = config.texture;
        Rect srcRect = config.srcRect;
        float z = config.zOrder;
        bool isLocal = (config.space == ParticleSpace::Local);
        bool isAdditive = (config.blendMode == ParticleBlendMode::Additive);

        for (int32_t i = 0; i < pool.count; ++i) {
            float t = pool.lifetimes[i] > 0.f ? (pool.ages[i] / pool.lifetimes[i]) : 1.f;

            Color tint;
            tint.r = static_cast<uint8_t>(lerpf(pool.colorStarts[i].r, pool.colorEnds[i].r, t) * 255.f);
            tint.g = static_cast<uint8_t>(lerpf(pool.colorStarts[i].g, pool.colorEnds[i].g, t) * 255.f);
            tint.b = static_cast<uint8_t>(lerpf(pool.colorStarts[i].b, pool.colorEnds[i].b, t) * 255.f);
            tint.a = static_cast<uint8_t>(lerpf(pool.colorStarts[i].a, pool.colorEnds[i].a, t) * 255.f);

            float size = pool.sizes[i];
            Vec2 origin = {size * 0.5f, size * 0.5f};

            Vec2 pos = pool.positions[i];
            if (isLocal) {
                // In local space, positions are relative to emitter
                pos.x += transform.position.x;
                pos.y += transform.position.y;
            }

            renderer->drawSprite(tex, pos, srcRect,
                                 {size, size}, pool.rotations[i], origin,
                                 tint, Flip::None, z,
                                 isAdditive);
        }
    });
}

} // namespace drift
