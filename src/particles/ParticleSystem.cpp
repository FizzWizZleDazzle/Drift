#include <drift/particles/ParticlePool.hpp>
#include <drift/particles/EmitterConfig.hpp>
#include <drift/components/GlobalTransform.hpp>
#include <drift/components/ParticleEmitter.hpp>
#include <drift/resources/ParticleSystemResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>
#include <drift/Resource.hpp>
#include <drift/Query.hpp>
#include <drift/Types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace drift {

// ---------------------------------------------------------------------------
// RNG helper
// ---------------------------------------------------------------------------
// The high 24 bits: an LCG's low bits repeat with short periods, so draws
// taken from them in a row (speed, then angle) fall on a few lines.
static float rngFloat(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / static_cast<float>(0x00FFFFFFu);
}

static float rngRange(uint32_t& state, float lo, float hi) {
    return lo + rngFloat(state) * (hi - lo);
}

// ---------------------------------------------------------------------------
// Spawn a single particle
// ---------------------------------------------------------------------------
static void spawnParticle(EmitterState& state, const EmitterConfig& config,
                          Vec2 emitterPos, Vec2 inheritedVel = {}) {
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

    // Velocity from speed + angle + inherited emitter velocity
    float spd = config.speed.sample(rng);
    float ang = config.angle.sample(rng);
    state.pool.velocities[idx] = {spd * std::cos(ang) + inheritedVel.x,
                                   spd * std::sin(ang) + inheritedVel.y};

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
                          QueryMut<ParticleEmitter, Transform2D, Optional<GlobalTransform2D>> emitters) {
    float dt = time->delta;

    // Track which emitter entities still exist this frame
    std::vector<EntityId> activeEntities;

    emitters.iterWithEntity([&](EntityId id, ParticleEmitter& emitter, Transform2D& local,
                                GlobalTransform2D* global) {
        activeEntities.push_back(id);
        // An emitter in a hierarchy emits from its place in the world.
        Vec2 worldPos = global ? global->position : local.position;

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
                            spawnParticle(state, emitter.config, worldPos);
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

        Vec2 emitterPos = worldPos;

        // Compute emitter velocity for velocity inheritance
        Vec2 emitterVel = {};
        if (state.hasPrevPosition && dt > 0.f) {
            emitterVel = (emitterPos - state.prevPosition) * (1.f / dt);
        }
        state.prevPosition = emitterPos;
        state.hasPrevPosition = true;

        Vec2 inheritedVel = emitterVel * emitter.config.velocityInheritance;

        if (emitter.playing) {
            // Continuous spawning
            if (emitter.config.spawnRate > 0.f) {
                state.spawnAccumulator += dt * emitter.config.spawnRate;
                while (state.spawnAccumulator >= 1.f && state.pool.count < state.pool.capacity) {
                    spawnParticle(state, emitter.config, emitterPos, inheritedVel);
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
                            spawnParticle(state, emitter.config, emitterPos, inheritedVel);
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

            // Bounds clamping
            if (emitter.config.boundsMin.x != emitter.config.boundsMax.x ||
                emitter.config.boundsMin.y != emitter.config.boundsMax.y) {
                if (pool.positions[i].x < emitter.config.boundsMin.x) {
                    pool.positions[i].x = emitter.config.boundsMin.x;
                    pool.velocities[i].x = 0.f;
                } else if (pool.positions[i].x > emitter.config.boundsMax.x) {
                    pool.positions[i].x = emitter.config.boundsMax.x;
                    pool.velocities[i].x = 0.f;
                }
                if (pool.positions[i].y < emitter.config.boundsMin.y) {
                    pool.positions[i].y = emitter.config.boundsMin.y;
                    pool.velocities[i].y = 0.f;
                } else if (pool.positions[i].y > emitter.config.boundsMax.y) {
                    pool.positions[i].y = emitter.config.boundsMax.y;
                    pool.velocities[i].y = 0.f;
                }
            }

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
    std::sort(activeEntities.begin(), activeEntities.end());
    for (auto it = particles->emitters.begin(); it != particles->emitters.end();) {
        bool found = std::binary_search(activeEntities.begin(), activeEntities.end(), it->first);
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
                          Query<ParticleEmitter, Transform2D, Optional<GlobalTransform2D>> emitters) {
    emitters.iterWithEntity([&](EntityId id, const ParticleEmitter& emitter, const Transform2D& local,
                                const GlobalTransform2D* global) {
        Vec2 worldPos = global ? global->position : local.position;
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

        // drawSprite scales the source rect and takes the origin in source
        // pixels, so a particle `size` pixels wide is the source scaled by
        // size / source width, turning around the source's center.
        Vec2 src = {srcRect.w, srcRect.h};
        if (src.x <= 0.f || src.y <= 0.f) src = renderer->textureSize(tex);
        if (src.x <= 0.f || src.y <= 0.f) src = {1.f, 1.f};
        Vec2 origin = {src.x * 0.5f, src.y * 0.5f};

        // An emitter whose particles no camera is near keeps simulating but
        // queues nothing.
        Vec2 offset = isLocal ? worldPos : Vec2{};
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f, reach = 0.f;
        for (int32_t i = 0; i < pool.count; ++i) {
            x0 = std::min(x0, pool.positions[i].x); x1 = std::max(x1, pool.positions[i].x);
            y0 = std::min(y0, pool.positions[i].y); y1 = std::max(y1, pool.positions[i].y);
            reach = std::max(reach, std::fabs(pool.sizes[i]));
        }
        reach *= 0.75f;   // a turned square reaches half its diagonal from its center
        Rect area = {x0 + offset.x - reach, y0 + offset.y - reach, x1 - x0 + reach * 2.f, y1 - y0 + reach * 2.f};
        if (!renderer->visible(area, renderer->layer())) return;

        for (int32_t i = 0; i < pool.count; ++i) {
            float t = pool.lifetimes[i] > 0.f ? (pool.ages[i] / pool.lifetimes[i]) : 1.f;

            Color tint;
            tint.r = static_cast<uint8_t>(lerpf(pool.colorStarts[i].r, pool.colorEnds[i].r, t) * 255.f);
            tint.g = static_cast<uint8_t>(lerpf(pool.colorStarts[i].g, pool.colorEnds[i].g, t) * 255.f);
            tint.b = static_cast<uint8_t>(lerpf(pool.colorStarts[i].b, pool.colorEnds[i].b, t) * 255.f);
            tint.a = static_cast<uint8_t>(lerpf(pool.colorStarts[i].a, pool.colorEnds[i].a, t) * 255.f);

            float size = pool.sizes[i];

            Vec2 pos = pool.positions[i];
            if (isLocal) {
                // In local space, positions are relative to emitter
                pos.x += worldPos.x;
                pos.y += worldPos.y;
            }

            renderer->drawSprite(tex, pos, srcRect,
                                 {size / src.x, size / src.y}, pool.rotations[i], origin,
                                 tint, Flip::None, z,
                                 config.blendMode);
        }
    });
}

} // namespace drift
