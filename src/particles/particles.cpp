// drift/src/particles/particles.cpp
// Particle system for Drift 2D engine

#include <drift/particles.h>
#include <drift/renderer.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Random helpers (simple LCG)
// ---------------------------------------------------------------------------

static uint32_t s_rng_state = 12345u;

static uint32_t rng_next()
{
    s_rng_state = s_rng_state * 1664525u + 1013904223u;
    return s_rng_state;
}

static float rng_float()
{
    return static_cast<float>(rng_next() & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
}

static float rng_range(float lo, float hi)
{
    return lo + rng_float() * (hi - lo);
}

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

struct Particle {
    DriftVec2   position;
    DriftVec2   velocity;
    DriftColorF color_start;
    DriftColorF color_end;
    float       size_start;
    float       size_end;
    float       lifetime;
    float       age;
    bool        active;
};

struct EmitterData {
    DriftEmitterDef          config;
    DriftVec2                position;
    bool                     active;
    float                    spawn_accumulator;
    std::vector<Particle>    pool;
    int32_t                  active_count;
};

// ---------------------------------------------------------------------------
// Global storage
// ---------------------------------------------------------------------------

static std::vector<EmitterData> s_emitters;
static uint32_t                 s_next_id = 1;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static EmitterData* emitter_get(DriftEmitter emitter)
{
    uint32_t index = emitter.id - 1;
    if (index >= static_cast<uint32_t>(s_emitters.size())) {
        return nullptr;
    }
    return &s_emitters[index];
}

static void spawn_particle(EmitterData& em)
{
    Particle* p = nullptr;
    for (auto& particle : em.pool) {
        if (!particle.active) {
            p = &particle;
            break;
        }
    }
    if (!p) return;

    float angle = rng_range(em.config.angle_min, em.config.angle_max);
    float speed = rng_range(em.config.speed_min, em.config.speed_max);

    p->position    = em.position;
    p->velocity.x  = speed * std::cos(angle);
    p->velocity.y  = speed * std::sin(angle);
    p->color_start = em.config.color_start;
    p->color_end   = em.config.color_end;
    p->size_start  = em.config.size_start;
    p->size_end    = em.config.size_end;
    p->lifetime    = rng_range(em.config.lifetime_min, em.config.lifetime_max);
    p->age         = 0.0f;
    p->active      = true;

    ++em.active_count;
}

static float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

DriftEmitter drift_emitter_create(const DriftEmitterDef* def)
{
    DriftEmitter handle{};
    if (!def) return handle;

    EmitterData em{};
    em.config            = *def;
    em.position.x        = 0.0f;
    em.position.y        = 0.0f;
    em.active            = false;
    em.spawn_accumulator = 0.0f;
    em.active_count      = 0;

    int32_t pool_size = def->max_particles;
    if (pool_size <= 0) pool_size = 256;

    em.pool.resize(static_cast<size_t>(pool_size));
    for (auto& p : em.pool) {
        p.active = false;
    }

    s_emitters.push_back(std::move(em));
    handle.id = s_next_id++;
    return handle;
}

void drift_emitter_destroy(DriftEmitter emitter)
{
    EmitterData* em = emitter_get(emitter);
    if (!em) return;
    *em = EmitterData{};
}

void drift_emitter_set_position(DriftEmitter emitter, DriftVec2 pos)
{
    EmitterData* em = emitter_get(emitter);
    if (!em) return;
    em->position = pos;
}

void drift_emitter_burst(DriftEmitter emitter, int32_t count)
{
    EmitterData* em = emitter_get(emitter);
    if (!em) return;

    for (int32_t i = 0; i < count; ++i) {
        spawn_particle(*em);
    }
}

void drift_emitter_start(DriftEmitter emitter)
{
    EmitterData* em = emitter_get(emitter);
    if (!em) return;
    em->active = true;
    em->spawn_accumulator = 0.0f;
}

void drift_emitter_stop(DriftEmitter emitter)
{
    EmitterData* em = emitter_get(emitter);
    if (!em) return;
    em->active = false;
}

bool drift_emitter_is_active(DriftEmitter emitter)
{
    EmitterData* em = emitter_get(emitter);
    if (!em) return false;
    return em->active || em->active_count > 0;
}

void drift_particles_update(float dt)
{
    for (EmitterData& em : s_emitters) {
        if (em.active && em.config.spawn_rate > 0.0f) {
            em.spawn_accumulator += dt * em.config.spawn_rate;
            while (em.spawn_accumulator >= 1.0f) {
                spawn_particle(em);
                em.spawn_accumulator -= 1.0f;
            }
        }

        float gravity = em.config.gravity;

        for (Particle& p : em.pool) {
            if (!p.active) continue;

            p.age += dt;
            if (p.age >= p.lifetime) {
                p.active = false;
                --em.active_count;
                continue;
            }

            p.velocity.y += gravity * dt;
            p.position.x += p.velocity.x * dt;
            p.position.y += p.velocity.y * dt;
        }
    }
}

void drift_particles_render(void)
{
    for (const EmitterData& em : s_emitters) {
        DriftTexture texture = em.config.texture;
        DriftRect src        = em.config.src_rect;

        for (const Particle& p : em.pool) {
            if (!p.active) continue;

            float t = (p.lifetime > 0.0f) ? (p.age / p.lifetime) : 1.0f;

            // Interpolate colour and size
            float size = lerp(p.size_start, p.size_end, t);

            DriftColor tint;
            tint.r = static_cast<uint8_t>(lerp(p.color_start.r, p.color_end.r, t) * 255.0f);
            tint.g = static_cast<uint8_t>(lerp(p.color_start.g, p.color_end.g, t) * 255.0f);
            tint.b = static_cast<uint8_t>(lerp(p.color_start.b, p.color_end.b, t) * 255.0f);
            tint.a = static_cast<uint8_t>(lerp(p.color_start.a, p.color_end.a, t) * 255.0f);

            DriftVec2 origin = {size * 0.5f, size * 0.5f};

            drift_sprite_draw(texture,
                              p.position,
                              src,
                              DriftVec2{size, size},
                              0.0f,
                              origin,
                              tint,
                              DRIFT_FLIP_NONE,
                              10.0f); // particles above most things
        }
    }
}

int32_t drift_particles_get_count(void)
{
    int32_t total = 0;
    for (const EmitterData& em : s_emitters) {
        total += em.active_count;
    }
    return total;
}
