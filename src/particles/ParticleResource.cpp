#include <drift/resources/ParticleResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/Log.hpp>
#include "core/HandlePool.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace drift {

// ---------------------------------------------------------------------------
// Random helpers (simple LCG, per-resource instance)
// ---------------------------------------------------------------------------

struct RNG {
    uint32_t state = 12345u;

    uint32_t next() {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    float floatVal() {
        return static_cast<float>(next() & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
    }

    float range(float lo, float hi) {
        return lo + floatVal() * (hi - lo);
    }
};

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

struct Particle {
    Vec2   position;
    Vec2   velocity;
    ColorF colorStart;
    ColorF colorEnd;
    float  sizeStart;
    float  sizeEnd;
    float  lifetime;
    float  age;
    bool   active = false;
};

struct EmitterData {
    EmitterDef            config;
    Vec2                  position;
    bool                  active = false;
    float                 spawnAccumulator = 0.f;
    std::vector<Particle> pool;
    int32_t               activeCount = 0;
};

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct ParticleResource::Impl {
    RendererResource& renderer;
    HandlePool<EmitterTag, EmitterData> emitters;
    RNG rng;

    Impl(RendererResource& r) : renderer(r) {}

    void spawnParticle(EmitterData& em) {
        Particle* p = nullptr;
        for (auto& particle : em.pool) {
            if (!particle.active) {
                p = &particle;
                break;
            }
        }
        if (!p) return;

        float angle = rng.range(em.config.angleMin, em.config.angleMax);
        float speed = rng.range(em.config.speedMin, em.config.speedMax);

        p->position   = em.position;
        p->velocity.x = speed * std::cos(angle);
        p->velocity.y = speed * std::sin(angle);
        p->colorStart = em.config.colorStart;
        p->colorEnd   = em.config.colorEnd;
        p->sizeStart  = em.config.sizeStart;
        p->sizeEnd    = em.config.sizeEnd;
        p->lifetime   = rng.range(em.config.lifetimeMin, em.config.lifetimeMax);
        p->age        = 0.f;
        p->active     = true;

        ++em.activeCount;
    }
};

static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ParticleResource::ParticleResource(RendererResource& renderer)
    : impl_(new Impl(renderer)) {}

ParticleResource::~ParticleResource() { delete impl_; }

EmitterHandle ParticleResource::createEmitter(const EmitterDef& def) {
    EmitterData data;
    data.config = def;
    data.position = {};
    data.active = false;
    data.spawnAccumulator = 0.f;
    data.activeCount = 0;

    int32_t poolSize = def.maxParticles;
    if (poolSize <= 0) poolSize = 256;
    data.pool.resize(static_cast<size_t>(poolSize));

    return impl_->emitters.create(std::move(data));
}

void ParticleResource::destroyEmitter(EmitterHandle emitter) {
    impl_->emitters.destroy(emitter);
}

void ParticleResource::setEmitterPosition(EmitterHandle emitter, Vec2 pos) {
    auto* em = impl_->emitters.get(emitter);
    if (em) em->position = pos;
}

void ParticleResource::burst(EmitterHandle emitter, int32_t count) {
    auto* em = impl_->emitters.get(emitter);
    if (!em) return;
    for (int32_t i = 0; i < count; ++i) {
        impl_->spawnParticle(*em);
    }
}

void ParticleResource::startEmitter(EmitterHandle emitter) {
    auto* em = impl_->emitters.get(emitter);
    if (!em) return;
    em->active = true;
    em->spawnAccumulator = 0.f;
}

void ParticleResource::stopEmitter(EmitterHandle emitter) {
    auto* em = impl_->emitters.get(emitter);
    if (em) em->active = false;
}

bool ParticleResource::isEmitterActive(EmitterHandle emitter) const {
    auto* em = impl_->emitters.get(emitter);
    if (!em) return false;
    return em->active || em->activeCount > 0;
}

void ParticleResource::update(float dt) {
    impl_->emitters.forEach([&](EmitterHandle, EmitterData& em) {
        if (em.active && em.config.spawnRate > 0.f) {
            em.spawnAccumulator += dt * em.config.spawnRate;
            while (em.spawnAccumulator >= 1.f) {
                impl_->spawnParticle(em);
                em.spawnAccumulator -= 1.f;
            }
        }

        float gravity = em.config.gravity;

        for (Particle& p : em.pool) {
            if (!p.active) continue;

            p.age += dt;
            if (p.age >= p.lifetime) {
                p.active = false;
                --em.activeCount;
                continue;
            }

            p.velocity.y += gravity * dt;
            p.position.x += p.velocity.x * dt;
            p.position.y += p.velocity.y * dt;
        }
    });
}

void ParticleResource::render() {
    impl_->emitters.forEach([&](EmitterHandle, const EmitterData& em) {
        TextureHandle texture = TextureHandle::make(em.config.textureId, 1);
        Rect src = em.config.srcRect;

        for (const Particle& p : em.pool) {
            if (!p.active) continue;

            float t = (p.lifetime > 0.f) ? (p.age / p.lifetime) : 1.f;

            float size = lerp(p.sizeStart, p.sizeEnd, t);

            Color tint;
            tint.r = static_cast<uint8_t>(lerp(p.colorStart.r, p.colorEnd.r, t) * 255.f);
            tint.g = static_cast<uint8_t>(lerp(p.colorStart.g, p.colorEnd.g, t) * 255.f);
            tint.b = static_cast<uint8_t>(lerp(p.colorStart.b, p.colorEnd.b, t) * 255.f);
            tint.a = static_cast<uint8_t>(lerp(p.colorStart.a, p.colorEnd.a, t) * 255.f);

            Vec2 origin = {size * 0.5f, size * 0.5f};

            impl_->renderer.drawSprite(
                texture, p.position, src,
                {size, size}, 0.f, origin, tint, Flip::None, 10.f);
        }
    });
}

int32_t ParticleResource::particleCount() const {
    int32_t total = 0;
    impl_->emitters.forEach([&](EmitterHandle, const EmitterData& em) {
        total += em.activeCount;
    });
    return total;
}

} // namespace drift
