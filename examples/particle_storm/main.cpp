// =============================================================================
// Drift Engine - Particle Storm (Stress Test)
// =============================================================================
// Click to spawn emitter orbs. Number keys 1-5 spawn wave patterns (ring,
// spiral, grid, rain, vortex) of 50-100 emitters each. Backspace mass-despawns
// all. Mouse wheel zooms camera. Target: 200-500 emitters, 10k-50k particles.
//
// Bugs targeted:
//   - Query mutex contention: 4+ parallel systems all blocking on same mutex
//   - changeTicks_/addTicks_ race: color_cycle writes while change_monitor reads
//   - Particle emitter O(n^2) cleanup: Backspace despawns 500 emitters at once
//   - ThreadPool spin: many read-only systems batched together
// =============================================================================

#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/Query.hpp>
#include <drift/QueryTraits.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/ParticleEmitter.hpp>
#include <drift/components/Hierarchy.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/ParticleSystemResource.hpp>
#include <drift/resources/Time.hpp>

#include <imgui.h>

#include <cmath>
#include <cstring>
#include <cstdio>

using namespace drift;

static constexpr float W = 1280.f;
static constexpr float H = 720.f;
static constexpr float HALF_W = W * 0.5f;
static constexpr float HALF_H = H * 0.5f;
static constexpr float PI = 3.14159265f;
static constexpr float TWO_PI = 6.28318530f;

// --- Components ---

struct OrbiterTag {};

struct OrbitState {
    float angle;
    float radius;
    float speed;    // radians per second
    Vec2 center;
};

struct ColorPhase {
    float hue;
    float speed;    // hue rotation speed (cycles per second)
};

// --- Resource ---

struct StormState : public Resource {
    DRIFT_RESOURCE(StormState)

    TextureHandle particleTex;
    TextureHandle orbTex;

    EntityId cameraId = InvalidEntityId;
    float cameraZoom = 1.f;

    int patternCount = 0; // how many pattern spawns have occurred
    uint32_t rng = 54321u;

    // FPS tracking + auto-cull
    float smoothFps = 60.f;
    int cullPerFrame = 0;
};

// Simple LCG random
static float randf(uint32_t& rng) {
    rng = rng * 1664525u + 1013904223u;
    return static_cast<float>(rng & 0xFFFF) / 65535.f;
}

static float randf(uint32_t& rng, float lo, float hi) {
    return lo + randf(rng) * (hi - lo);
}

// HSV to ColorF (s=1, v=1)
static ColorF hueToColor(float hue, float alpha = 1.f) {
    float h = fmodf(hue, 1.f);
    if (h < 0.f) h += 1.f;
    float r, g, b;
    float x = 1.f - fabsf(fmodf(h * 6.f, 2.f) - 1.f);
    if      (h < 1.f/6.f) { r = 1; g = x; b = 0; }
    else if (h < 2.f/6.f) { r = x; g = 1; b = 0; }
    else if (h < 3.f/6.f) { r = 0; g = 1; b = x; }
    else if (h < 4.f/6.f) { r = 0; g = x; b = 1; }
    else if (h < 5.f/6.f) { r = x; g = 0; b = 1; }
    else                   { r = 1; g = 0; b = x; }
    return {r, g, b, alpha};
}

// --- Procedural textures ---

static TextureHandle makeWhiteSquare(RendererResource* r, int size) {
    auto* px = new uint8_t[size * size * 4];
    memset(px, 255, size * size * 4);
    auto tex = r->createTextureFromPixels(px, size, size);
    delete[] px;
    return tex;
}

static TextureHandle makeCircleTexture(RendererResource* r, int diameter) {
    auto* px = new uint8_t[diameter * diameter * 4];
    memset(px, 0, diameter * diameter * 4);
    float center = (diameter - 1) * 0.5f;
    float r2 = center * center;
    for (int y = 0; y < diameter; y++) {
        for (int x = 0; x < diameter; x++) {
            float dx = x - center, dy = y - center;
            float d2 = dx * dx + dy * dy;
            if (d2 < r2) {
                int i = (y * diameter + x) * 4;
                float edgeDist = 1.f - sqrtf(d2) / center;
                uint8_t a = static_cast<uint8_t>(255.f * fminf(edgeDist * 3.f, 1.f));
                px[i + 0] = px[i + 1] = px[i + 2] = 255;
                px[i + 3] = a;
            }
        }
    }
    auto tex = r->createTextureFromPixels(px, diameter, diameter);
    delete[] px;
    return tex;
}

// --- Emitter config factories ---

static EmitterConfig makeRingConfig(TextureHandle tex, float hue) {
    EmitterConfig c;
    c.texture = tex;
    c.shape = EmissionShape::Point;
    c.spawnRate = 40.f;
    c.maxParticles = 100;
    c.lifetime = {0.5f, 1.2f};
    c.speed = {30.f, 80.f};
    c.angle = {0.f, TWO_PI};
    c.sizeStart = {4.f, 6.f};
    c.sizeEnd = {1.f, 2.f};
    c.colorStart = hueToColor(hue, 1.f);
    c.colorEnd = hueToColor(hue + 0.1f, 0.f);
    c.blendMode = ParticleBlendMode::Additive;
    c.drag = 0.5f;
    c.zOrder = 5.f;
    return c;
}

static EmitterConfig makeSpiralConfig(TextureHandle tex, float hue) {
    EmitterConfig c;
    c.texture = tex;
    c.shape = EmissionShape::Point;
    c.spawnRate = 80.f;
    c.maxParticles = 200;
    c.lifetime = {0.8f, 2.0f};
    c.speed = {10.f, 40.f};
    c.angle = {0.f, TWO_PI};
    c.sizeStart = {3.f, 5.f};
    c.sizeEnd = {1.f, 1.f};
    c.colorStart = hueToColor(hue, 1.f);
    c.colorEnd = hueToColor(hue + 0.3f, 0.f);
    c.blendMode = ParticleBlendMode::Additive;
    c.drag = 2.f;
    c.zOrder = 5.f;
    return c;
}

static EmitterConfig makeGridConfig(TextureHandle tex, float hue) {
    EmitterConfig c;
    c.texture = tex;
    c.shape = EmissionShape::Box;
    c.shapeExtents = {8.f, 8.f};
    c.spawnRate = 30.f;
    c.maxParticles = 80;
    c.lifetime = {1.0f, 2.5f};
    c.speed = {5.f, 15.f};
    c.angle = {0.f, TWO_PI};
    c.sizeStart = {5.f, 8.f};
    c.sizeEnd = {2.f, 3.f};
    c.colorStart = hueToColor(hue, 0.9f);
    c.colorEnd = hueToColor(hue + 0.2f, 0.f);
    c.blendMode = ParticleBlendMode::Alpha;
    c.drag = 0.3f;
    c.zOrder = 4.f;
    return c;
}

static EmitterConfig makeRainConfig(TextureHandle tex, float hue) {
    EmitterConfig c;
    c.texture = tex;
    c.shape = EmissionShape::Line;
    c.shapeExtents = {60.f, 0.f};
    c.spawnRate = 60.f;
    c.maxParticles = 150;
    c.lifetime = {1.0f, 3.0f};
    c.speed = {5.f, 20.f};
    c.angle = {PI * 0.4f, PI * 0.6f}; // roughly downward
    c.sizeStart = {2.f, 4.f};
    c.sizeEnd = {1.f, 1.f};
    c.gravity = {0.f, 80.f};
    c.colorStart = hueToColor(hue, 0.8f);
    c.colorEnd = hueToColor(hue, 0.f);
    c.blendMode = ParticleBlendMode::Additive;
    c.boundsMin = {-HALF_W, -HALF_H};
    c.boundsMax = {HALF_W, HALF_H};
    c.zOrder = 5.f;
    return c;
}

static EmitterConfig makeVortexConfig(TextureHandle tex, float hue) {
    EmitterConfig c;
    c.texture = tex;
    c.shape = EmissionShape::Circle;
    c.shapeRadius = 12.f;
    c.spawnRate = 50.f;
    c.maxParticles = 120;
    c.lifetime = {0.6f, 1.5f};
    c.speed = {20.f, 60.f};
    c.angle = {0.f, TWO_PI};
    c.sizeStart = {3.f, 5.f};
    c.sizeEnd = {1.f, 2.f};
    c.colorStart = hueToColor(hue, 1.f);
    c.colorEnd = hueToColor(hue + 0.5f, 0.f);
    c.blendMode = ParticleBlendMode::Additive;
    c.space = ParticleSpace::Local;
    c.drag = 1.f;
    c.initialRotation = {0.f, TWO_PI};
    c.angularVelocity = {-3.f, 3.f};
    c.zOrder = 6.f;
    return c;
}

// --- Spawn a single emitter orb ---

static void spawnOrb(Commands& cmd, StormState& state, Vec2 pos,
                     EmitterConfig config, float orbitRadius, float orbitSpeed,
                     float hue) {
    cmd.spawn()
        .insert(Transform2D{.position = pos},
                Sprite{.texture = state.orbTex,
                       .origin = {4.f, 4.f},
                       .tint = Color::white(),
                       .zOrder = 9.f},
                OrbiterTag{},
                OrbitState{.angle = randf(state.rng, 0.f, TWO_PI),
                           .radius = orbitRadius,
                           .speed = orbitSpeed,
                           .center = pos},
                ColorPhase{.hue = hue,
                           .speed = randf(state.rng, 0.2f, 0.8f)},
                ParticleEmitter{.config = config});
}

// =============================================================================
// Systems
// =============================================================================

void stormSetup(ResMut<StormState> state, ResMut<RendererResource> renderer,
                Commands& cmd) {
    state->particleTex = makeWhiteSquare(renderer.get(), 4);
    state->orbTex = makeCircleTexture(renderer.get(), 8);

    renderer->setClearColor({0.01f, 0.01f, 0.03f, 1.f});

    state->cameraId = cmd.spawn()
        .insert(Transform2D{},
                Camera{.zoom = 1.f, .active = true});
}

void patternSpawn(Res<InputResource> input, ResMut<StormState> state,
                  QueryMut<Transform2D, With<OrbiterTag>> orbiters,
                  Commands& cmd) {
    // Click to spawn single emitter
    if (input->mouseButtonPressed(MouseButton::Left)) {
        Vec2 pos = {input->mousePosition().x - HALF_W,
                    input->mousePosition().y - HALF_H};
        float hue = randf(state->rng);
        EmitterConfig cfg = makeRingConfig(state->particleTex, hue);
        spawnOrb(cmd, *state, pos, cfg, randf(state->rng, 20.f, 60.f),
                 randf(state->rng, 0.5f, 2.f), hue);
    }

    // Right-click spawns a cluster of 5
    if (input->mouseButtonPressed(MouseButton::Right)) {
        Vec2 pos = {input->mousePosition().x - HALF_W,
                    input->mousePosition().y - HALF_H};
        float baseHue = randf(state->rng);
        for (int i = 0; i < 5; i++) {
            float hue = baseHue + i * 0.12f;
            float angle = TWO_PI * i / 5.f;
            Vec2 offset = {cosf(angle) * 30.f, sinf(angle) * 30.f};
            EmitterConfig cfg = makeSpiralConfig(state->particleTex, hue);
            spawnOrb(cmd, *state, pos + offset, cfg,
                     randf(state->rng, 30.f, 80.f),
                     randf(state->rng, 0.8f, 2.5f), hue);
        }
    }

    // Number keys: spawn patterns
    using ConfigMaker = EmitterConfig(*)(TextureHandle, float);
    struct PatternDef {
        Key key;
        int count;
        ConfigMaker maker;
        const char* name;
    };
    PatternDef patterns[] = {
        {Key::Num1, 60,  makeRingConfig,    "Ring"},
        {Key::Num2, 50,  makeSpiralConfig,  "Spiral"},
        {Key::Num3, 80,  makeGridConfig,    "Grid"},
        {Key::Num4, 70,  makeRainConfig,    "Rain"},
        {Key::Num5, 100, makeVortexConfig,  "Vortex"},
    };

    for (auto& p : patterns) {
        if (!input->keyPressed(p.key)) continue;

        state->patternCount++;
        float baseHue = randf(state->rng);

        for (int i = 0; i < p.count; i++) {
            float t = static_cast<float>(i) / static_cast<float>(p.count);
            float hue = baseHue + t * 0.5f;

            // Position varies by pattern
            Vec2 pos{};
            float orbitR = 0.f, orbitSpd = 0.f;

            if (p.key == Key::Num1) {
                // Ring: circle around center
                float angle = TWO_PI * t;
                float ringR = 150.f + randf(state->rng, -20.f, 20.f);
                pos = {cosf(angle) * ringR, sinf(angle) * ringR};
                orbitR = randf(state->rng, 10.f, 40.f);
                orbitSpd = randf(state->rng, 1.f, 3.f);
            } else if (p.key == Key::Num2) {
                // Spiral: Archimedean spiral
                float angle = t * TWO_PI * 4.f;
                float r = 30.f + t * 250.f;
                pos = {cosf(angle) * r, sinf(angle) * r};
                orbitR = randf(state->rng, 5.f, 25.f);
                orbitSpd = randf(state->rng, 1.5f, 4.f);
            } else if (p.key == Key::Num3) {
                // Grid: rows and columns
                int cols = 10;
                int row = i / cols;
                int col = i % cols;
                float spacing = 50.f;
                pos = {(col - cols * 0.5f) * spacing,
                       (row - (p.count / cols) * 0.5f) * spacing};
                orbitR = randf(state->rng, 5.f, 15.f);
                orbitSpd = randf(state->rng, 0.5f, 1.5f);
            } else if (p.key == Key::Num4) {
                // Rain: spread across top
                pos = {randf(state->rng, -HALF_W + 40.f, HALF_W - 40.f),
                       randf(state->rng, -HALF_H, -HALF_H + 100.f)};
                orbitR = randf(state->rng, 2.f, 10.f);
                orbitSpd = randf(state->rng, 0.3f, 1.f);
            } else {
                // Vortex: clustered near center with wide orbit
                float angle = TWO_PI * t;
                float r = randf(state->rng, 20.f, 100.f);
                pos = {cosf(angle) * r, sinf(angle) * r};
                orbitR = randf(state->rng, 40.f, 120.f);
                orbitSpd = randf(state->rng, 1.f, 3.f) * (randf(state->rng) > 0.5f ? 1.f : -1.f);
            }

            EmitterConfig cfg = p.maker(state->particleTex, hue);
            spawnOrb(cmd, *state, pos, cfg, orbitR, orbitSpd, hue);
        }
    }

    // Backspace: mass-despawn all orbiters (stress test for cleanup paths)
    if (input->keyPressed(Key::Backspace)) {
        orbiters.iterWithEntity([&](EntityId id, Transform2D&) {
            cmd.entity(id).despawn();
        });
    }

    // Escape to quit
    if (input->keyPressed(Key::Escape)) {
        // Let the app handle this via default behavior
    }
}

void orbitUpdate(Res<Time> time,
                 QueryMut<Transform2D, OrbitState> orbiters) {
    float dt = time->delta;

    orbiters.iter([&](Transform2D& t, OrbitState& orbit) {
        orbit.angle += orbit.speed * dt;
        t.position = orbit.center + Vec2{cosf(orbit.angle) * orbit.radius,
                                          sinf(orbit.angle) * orbit.radius};
    });
}

void colorCycle(Res<Time> time,
                QueryMut<Sprite, ParticleEmitter, ColorPhase> emitters) {
    float dt = time->delta;

    emitters.iter([&](Sprite& sprite, ParticleEmitter& emitter, ColorPhase& phase) {
        phase.hue += phase.speed * dt;

        // Update sprite tint
        ColorF cf = hueToColor(phase.hue, 1.f);
        sprite.tint = Color{static_cast<uint8_t>(cf.r * 255),
                            static_cast<uint8_t>(cf.g * 255),
                            static_cast<uint8_t>(cf.b * 255),
                            255};

        // Update emitter colors (triggers change ticks)
        emitter.config.colorStart = hueToColor(phase.hue, 1.f);
        emitter.config.colorEnd = hueToColor(phase.hue + 0.15f, 0.f);
    });
}

// This system reads Changed<Sprite> concurrently with colorCycle writing Sprite,
// intentionally exercising the changeTick race condition in the scheduler.
void changeMonitor(Query<Transform2D, Changed<Sprite>> changedSprites,
                   Res<Time> time) {
    int count = 0;
    changedSprites.iter([&](const Transform2D&) {
        count++;
    });
    // The count isn't displayed; the purpose is concurrent query access
    (void)time;
    (void)count;
}

// Additional read-only system to increase parallel query contention
void positionMonitor(Query<Transform2D, With<OrbiterTag>> orbiters) {
    float sumX = 0.f, sumY = 0.f;
    int count = 0;
    orbiters.iter([&](const Transform2D& t) {
        sumX += t.position.x;
        sumY += t.position.y;
        count++;
    });
    (void)sumX;
    (void)sumY;
    (void)count;
}

void cameraControl(Res<InputResource> input, ResMut<StormState> state,
                   Commands& cmd) {
    // Mouse wheel zoom
    float wheel = input->mouseWheelDelta();
    if (wheel != 0.f) {
        state->cameraZoom += wheel * 0.1f;
        if (state->cameraZoom < 0.1f) state->cameraZoom = 0.1f;
        if (state->cameraZoom > 5.f) state->cameraZoom = 5.f;
        cmd.entity(state->cameraId)
            .insert(Camera{.zoom = state->cameraZoom, .active = true});
    }

    // WASD camera pan
    Vec2 pan{};
    if (input->keyHeld(Key::W) || input->keyHeld(Key::Up))    pan.y -= 1.f;
    if (input->keyHeld(Key::S) || input->keyHeld(Key::Down))  pan.y += 1.f;
    if (input->keyHeld(Key::A) || input->keyHeld(Key::Left))  pan.x -= 1.f;
    if (input->keyHeld(Key::D) || input->keyHeld(Key::Right)) pan.x += 1.f;
    if (pan.lengthSq() > 0.f) {
        static Vec2 camPos{};
        camPos += pan.normalized() * 200.f * 0.016f; // approximate dt
        cmd.entity(state->cameraId)
            .insert(Transform2D{.position = camPos});
    }
}

void stormHud(ResMut<StormState> state, Res<Time> time,
              ResMut<RendererResource> renderer,
              QueryMut<Transform2D, With<OrbiterTag>> orbiters,
              Res<ParticleSystemResource> particles,
              Commands& cmd) {
    // Smooth FPS
    float instantFps = (time->delta > 0.f) ? 1.f / time->delta : 60.f;
    state->smoothFps += (instantFps - state->smoothFps) * 0.05f;

    // Count emitters + auto-cull in one pass
    int emitterCount = 0;
    int culled = 0;
    int cullTarget = 0;
    if (state->smoothFps < 60.f) {
        float pressure = (60.f - state->smoothFps) / 60.f;
        cullTarget = static_cast<int>(1.f + pressure * 15.f);
    }
    orbiters.iterWithEntity([&](EntityId id, Transform2D&) {
        emitterCount++;
        if (culled < cullTarget) {
            cmd.entity(id).despawn();
            culled++;
        }
    });
    state->cullPerFrame = culled;

    // Count particles from the particle system resource
    int particleCount = 0;
    for (auto& [id, emState] : particles->emitters) {
        particleCount += emState.pool.count;
    }

    // ImGui overlay
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();

    char buf[128];
    snprintf(buf, sizeof(buf), "FPS: %.0f  Emitters: %d  Particles: %d  Patterns: %d",
             state->smoothFps, emitterCount, particleCount, state->patternCount);

    ImU32 fpsColor = state->smoothFps >= 60.f ? IM_COL32(80, 255, 80, 255)
                   : state->smoothFps >= 30.f ? IM_COL32(255, 200, 60, 255)
                                              : IM_COL32(255, 60, 60, 255);

    dl->AddText(font, 18.f, ImVec2(9, 5), IM_COL32(0, 0, 0, 200), buf);
    dl->AddText(font, 18.f, ImVec2(8, 4), fpsColor, buf);

    if (state->cullPerFrame > 0) {
        char cullBuf[64];
        snprintf(cullBuf, sizeof(cullBuf), "AUTO-CULL: -%d/frame", state->cullPerFrame);
        dl->AddText(font, 16.f, ImVec2(9, 25), IM_COL32(0, 0, 0, 200), cullBuf);
        dl->AddText(font, 16.f, ImVec2(8, 24), IM_COL32(255, 80, 80, 255), cullBuf);
    }

    // Controls hint
    const char* hint = "1-5: Patterns  Click: Spawn  RClick: Cluster  Backspace: Clear  Scroll: Zoom";
    dl->AddText(font, 14.f, ImVec2(9, H - 19), IM_COL32(0, 0, 0, 160), hint);
    dl->AddText(font, 14.f, ImVec2(8, H - 20), IM_COL32(180, 180, 200, 200), hint);
}

// =============================================================================
// Main
// =============================================================================

int main() {
    App app;
    app.setConfig({.title = "Drift - Particle Storm (Stress Test)",
                   .width = static_cast<int32_t>(W),
                   .height = static_cast<int32_t>(H),
                   .vsync = false});
    app.addPlugins<DefaultPlugins>();
    app.initResource<StormState>();

    app.startup<stormSetup>("storm_setup");
    app.update<patternSpawn>("pattern_spawn");
    app.update<orbitUpdate>("orbit_update");
    app.update<colorCycle>("color_cycle");
    app.update<changeMonitor>("change_monitor");    // concurrent read vs colorCycle writes
    app.update<positionMonitor>("position_monitor"); // additional parallel query
    app.update<cameraControl>("camera_control");
    app.update<stormHud>("storm_hud");

    return app.run();
}
