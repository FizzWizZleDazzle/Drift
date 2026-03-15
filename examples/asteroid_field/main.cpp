// =============================================================================
// Drift Engine - Asteroid Field (Stress Test)
// =============================================================================
// Top-down space scene. Asteroids spawn from edges, drift across. Player ship
// (WASD) shoots bullets (left-click). Bullets destroy asteroids which split
// into fragments. Explosions, trails, camera shake. Press F for frenzy (3x).
// Target: 500-2000 live physics entities at peak.
//
// Bugs targeted:
//   - PhysicsBridge O(n^2) cleanup with 1000+ shapes and 50-100 despawns/frame
//   - Particle emitter O(n^2) cleanup with hundreds of one-shot emitters
//   - Physics body creation/destruction timing (bullets live 1-2 frames)
//   - ThreadPool spin with few systems and many worker threads
// =============================================================================

#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/Events.hpp>
#include <drift/Query.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/Physics.hpp>
#include <drift/components/ParticleEmitter.hpp>
#include <drift/components/TrailRenderer.hpp>
#include <drift/components/CameraController.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/RendererResource.hpp>
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

// Collision filter categories
static constexpr uint32_t CAT_PLAYER   = 0x0001;
static constexpr uint32_t CAT_BULLET   = 0x0002;
static constexpr uint32_t CAT_ASTEROID = 0x0004;

// --- Components ---
struct Asteroid {
    float size;       // visual radius
    int splitLevel;   // 0=large, 1=medium, 2=small (no further split)
};

struct Bullet {
    float lifetime;   // seconds remaining
};

struct PlayerTag {};

// --- Resource ---
struct AsteroidState : public Resource {
    DRIFT_RESOURCE(AsteroidState)

    TextureHandle asteroidTex[3]; // large, medium, small
    TextureHandle bulletTex;
    TextureHandle particleTex;
    TextureHandle playerTex;

    EntityId playerId = InvalidEntityId;
    EntityId cameraId = InvalidEntityId;

    int score = 0;
    bool frenzy = false;
    float spawnTimer = 0.f;
    float spawnInterval = 0.4f; // base interval between asteroid spawns
    float autoFireTimer = 0.f;

    uint32_t rng = 12345u;

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

static Vec2 toWorld(Vec2 screen) {
    return {screen.x - HALF_W, screen.y - HALF_H};
}

// --- Procedural textures ---

static TextureHandle makeCircleTexture(RendererResource* r, int diameter, uint32_t& rng) {
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
                // Gray with random noise for rocky look
                uint8_t base = static_cast<uint8_t>(130 + (randf(rng) * 70));
                px[i + 0] = base;
                px[i + 1] = base;
                px[i + 2] = static_cast<uint8_t>(base * 0.85f);
                // Rough edges via alpha falloff
                float edgeDist = 1.f - sqrtf(d2) / center;
                px[i + 3] = edgeDist < 0.15f
                    ? static_cast<uint8_t>(255.f * (edgeDist / 0.15f) * randf(rng, 0.5f, 1.0f))
                    : 255;
            }
        }
    }
    auto tex = r->createTextureFromPixels(px, diameter, diameter);
    delete[] px;
    return tex;
}

static TextureHandle makeWhiteSquare(RendererResource* r, int size) {
    auto* px = new uint8_t[size * size * 4];
    memset(px, 255, size * size * 4);
    auto tex = r->createTextureFromPixels(px, size, size);
    delete[] px;
    return tex;
}

static TextureHandle makeDiamondTexture(RendererResource* r, int size) {
    auto* px = new uint8_t[size * size * 4];
    memset(px, 0, size * size * 4);
    float center = (size - 1) * 0.5f;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = fabsf(x - center);
            float dy = fabsf(y - center);
            if ((dx + dy) / center < 1.0f) {
                int i = (y * size + x) * 4;
                px[i + 0] = 180;
                px[i + 1] = 210;
                px[i + 2] = 255;
                px[i + 3] = 255;
            }
        }
    }
    auto tex = r->createTextureFromPixels(px, size, size);
    delete[] px;
    return tex;
}

// --- Explosion emitter config ---
static EmitterConfig makeExplosionConfig(TextureHandle tex, float size) {
    EmitterConfig c;
    c.texture = tex;
    c.spawnRate = 3000.f;
    c.maxParticles = static_cast<int32_t>(40 + size * 8);
    c.lifetime = {0.3f, 0.7f};
    c.speed = {60.f, 200.f};
    c.angle = {0.f, TWO_PI};
    c.sizeStart = {3.f, 7.f};
    c.sizeEnd = {1.f, 2.f};
    c.colorStart = {1.f, 0.9f, 0.3f, 1.f};
    c.colorEnd = {1.f, 0.3f, 0.0f, 0.f};
    c.blendMode = ParticleBlendMode::Additive;
    c.duration = 0.08f;
    c.looping = false;
    c.drag = 1.5f;
    c.zOrder = 8.f;
    return c;
}

// =============================================================================
// Systems
// =============================================================================

void asteroidSetup(ResMut<AsteroidState> game, ResMut<RendererResource> renderer,
                   Commands& cmd) {
    // Procedural textures: 3 asteroid sizes, bullet, particle, player
    game->asteroidTex[0] = makeCircleTexture(renderer.get(), 24, game->rng);
    game->asteroidTex[1] = makeCircleTexture(renderer.get(), 12, game->rng);
    game->asteroidTex[2] = makeCircleTexture(renderer.get(), 6, game->rng);
    game->bulletTex = makeWhiteSquare(renderer.get(), 4);
    game->particleTex = makeWhiteSquare(renderer.get(), 4);
    game->playerTex = makeDiamondTexture(renderer.get(), 16);

    renderer->setClearColor({0.02f, 0.02f, 0.06f, 1.f});

    // Camera with shake support
    game->cameraId = cmd.spawn()
        .insert(Transform2D{},
                Camera{.zoom = 1.f, .active = true},
                CameraShake{.decay = 6.f, .frequency = 25.f});

    // Player ship — sensor body so physics detects overlaps for game-over
    game->playerId = cmd.spawn()
        .insert(Transform2D{.position = {0, 0}},
                Sprite{.texture = game->playerTex,
                       .origin = {8.f, 8.f},
                       .tint = {180, 210, 255, 255},
                       .zOrder = 10.f},
                PlayerTag{},
                RigidBody2D{.type = BodyType::Dynamic,
                            .fixedRotation = true,
                            .gravityScale = 0.f},
                CircleCollider2D{.radius = 6.f,
                                 .isSensor = true,
                                 .filter = {CAT_PLAYER, CAT_ASTEROID}});

    // Camera follows player
    cmd.entity(game->cameraId)
        .insert(CameraFollow{.target = game->playerId, .smoothing = 8.f});
}

void asteroidSpawn(Res<Time> time, ResMut<AsteroidState> game, Commands& cmd) {
    float dt = time->delta;
    float interval = game->frenzy ? game->spawnInterval * 0.33f : game->spawnInterval;
    game->spawnTimer += dt;

    while (game->spawnTimer >= interval) {
        game->spawnTimer -= interval;

        // Random edge position with inward velocity
        float edge = randf(game->rng) * 4.f;
        Vec2 pos, vel;
        float speed = randf(game->rng, 40.f, 120.f);

        if (edge < 1.f) {        // top
            pos = {randf(game->rng, -HALF_W, HALF_W), -HALF_H - 30.f};
            vel = {randf(game->rng, -40.f, 40.f), speed};
        } else if (edge < 2.f) { // bottom
            pos = {randf(game->rng, -HALF_W, HALF_W), HALF_H + 30.f};
            vel = {randf(game->rng, -40.f, 40.f), -speed};
        } else if (edge < 3.f) { // left
            pos = {-HALF_W - 30.f, randf(game->rng, -HALF_H, HALF_H)};
            vel = {speed, randf(game->rng, -40.f, 40.f)};
        } else {                  // right
            pos = {HALF_W + 30.f, randf(game->rng, -HALF_H, HALF_H)};
            vel = {-speed, randf(game->rng, -40.f, 40.f)};
        }

        // Mostly large asteroids, some medium
        int level = randf(game->rng) < 0.7f ? 0 : 1;
        float sizes[] = {12.f, 6.f, 3.f};
        float radius = sizes[level];

        cmd.spawn()
            .insert(Transform2D{.position = pos,
                                .rotation = randf(game->rng, 0.f, TWO_PI)},
                    Sprite{.texture = game->asteroidTex[level],
                           .origin = {radius, radius},
                           .zOrder = 5.f},
                    Asteroid{.size = radius, .splitLevel = level},
                    RigidBody2D{.type = BodyType::Dynamic,
                                .fixedRotation = false,
                                .gravityScale = 0.f,
                                .linearDamping = 0.f},
                    CircleCollider2D{.radius = radius,
                                     .restitution = 0.8f,
                                     .filter = {CAT_ASTEROID, CAT_BULLET}},
                    Velocity2D{.linear = vel,
                               .angular = randf(game->rng, -2.f, 2.f)});
    }
}

void playerUpdate(Res<InputResource> input, Res<Time> time,
                  ResMut<AsteroidState> game,
                  QueryMut<Transform2D, With<PlayerTag>> players,
                  Commands& cmd) {
    float dt = time->delta;
    float speed = 250.f;

    // WASD movement
    Vec2 dir{};
    if (input->keyHeld(Key::W) || input->keyHeld(Key::Up))    dir.y -= 1.f;
    if (input->keyHeld(Key::S) || input->keyHeld(Key::Down))  dir.y += 1.f;
    if (input->keyHeld(Key::A) || input->keyHeld(Key::Left))  dir.x -= 1.f;
    if (input->keyHeld(Key::D) || input->keyHeld(Key::Right)) dir.x += 1.f;

    Vec2 playerPos{};
    players.iter([&](Transform2D& t) {
        t.position += dir.normalized() * speed * dt;
        // Clamp to play area
        if (t.position.x < -HALF_W + 12.f) t.position.x = -HALF_W + 12.f;
        if (t.position.x >  HALF_W - 12.f) t.position.x =  HALF_W - 12.f;
        if (t.position.y < -HALF_H + 12.f) t.position.y = -HALF_H + 12.f;
        if (t.position.y >  HALF_H - 12.f) t.position.y =  HALF_H - 12.f;
        playerPos = t.position;
    });

    // Toggle frenzy mode
    if (input->keyPressed(Key::F)) {
        game->frenzy = !game->frenzy;
    }

    // Spawn bullet helper
    auto spawnBullet = [&](Vec2 aimDir) {
        float bulletSpeed = 600.f;
        cmd.spawn()
            .insert(Transform2D{.position = playerPos + aimDir * 12.f},
                    Sprite{.texture = game->bulletTex,
                           .origin = {2.f, 2.f},
                           .tint = {255, 255, 200, 255},
                           .zOrder = 6.f},
                    Bullet{.lifetime = 2.5f},
                    RigidBody2D{.type = BodyType::Dynamic,
                                .fixedRotation = true,
                                .gravityScale = 0.f},
                    CircleCollider2D{.radius = 2.f,
                                     .filter = {CAT_BULLET, CAT_ASTEROID}},
                    Velocity2D{.linear = aimDir * bulletSpeed},
                    TrailRenderer{.width = 2.f,
                                  .lifetime = 0.25f,
                                  .colorStart = {1.f, 1.f, 0.8f, 0.7f},
                                  .colorEnd = {1.f, 0.5f, 0.1f, 0.f},
                                  .minDistance = 4.f,
                                  .maxPoints = 32,
                                  .zOrder = 4.f});
    };

    // Single shot on click
    if (input->mouseButtonPressed(MouseButton::Left)) {
        Vec2 mouseWorld = toWorld(input->mousePosition());
        Vec2 aimDir = (mouseWorld - playerPos);
        if (aimDir.length() > 1.f) {
            spawnBullet(aimDir.normalized());
        }
    }

    // Auto-fire when holding mouse button (always, faster in frenzy)
    if (input->mouseButtonHeld(MouseButton::Left)) {
        float fireRate = game->frenzy ? 0.04f : 0.12f;
        game->autoFireTimer += dt;
        while (game->autoFireTimer >= fireRate) {
            game->autoFireTimer -= fireRate;
            Vec2 mouseWorld = toWorld(input->mousePosition());
            Vec2 aimDir = (mouseWorld - playerPos);
            if (aimDir.length() > 1.f) {
                aimDir = aimDir.normalized();
                // Add spread in frenzy mode
                if (game->frenzy) {
                    float spread = randf(game->rng, -0.15f, 0.15f);
                    float cs = cosf(spread), sn = sinf(spread);
                    aimDir = {aimDir.x * cs - aimDir.y * sn,
                              aimDir.x * sn + aimDir.y * cs};
                }
                spawnBullet(aimDir);
            }
        }
    } else {
        game->autoFireTimer = 0.f;
    }
}

void collisionSystem(EventReader<CollisionStart> collisions,
                     Query<Asteroid> asteroids, Query<Bullet> bullets,
                     ResMut<AsteroidState> game,
                     QueryMut<Transform2D, Asteroid> asteroidData,
                     Commands& cmd) {
    for (auto& event : collisions) {
        EntityId asteroidId = InvalidEntityId;
        EntityId bulletId = InvalidEntityId;

        if (asteroids.contains(event.entityA) && bullets.contains(event.entityB)) {
            asteroidId = event.entityA;
            bulletId = event.entityB;
        } else if (asteroids.contains(event.entityB) && bullets.contains(event.entityA)) {
            asteroidId = event.entityB;
            bulletId = event.entityA;
        }
        if (asteroidId == InvalidEntityId) continue;

        // Find asteroid transform + data
        Vec2 pos{};
        float size = 6.f;
        int splitLevel = 2;
        asteroidData.iterWithEntity([&](EntityId id, Transform2D& t, Asteroid& a) {
            if (id == asteroidId) {
                pos = t.position;
                size = a.size;
                splitLevel = a.splitLevel;
            }
        });

        // Despawn both
        cmd.entity(bulletId).despawn();
        cmd.entity(asteroidId).despawn();

        game->score += (splitLevel + 1) * 10;

        // Explosion particles
        cmd.spawn()
            .insert(Transform2D{.position = pos},
                    ParticleEmitter{.config = makeExplosionConfig(game->particleTex, size),
                                    .oneShot = true});

        // Camera shake proportional to asteroid size
        cmd.entity(game->cameraId)
            .insert(CameraShake{.intensity = 1.5f + size * 0.3f,
                                .decay = 6.f,
                                .frequency = 25.f});

        // Split into smaller asteroids
        if (splitLevel < 2) {
            int newLevel = splitLevel + 1;
            float newSizes[] = {12.f, 6.f, 3.f};
            float newSize = newSizes[newLevel];
            int numFragments = (splitLevel == 0) ? 3 : 2;

            for (int i = 0; i < numFragments; i++) {
                float angle = randf(game->rng, 0.f, TWO_PI);
                float spd = randf(game->rng, 60.f, 160.f);
                Vec2 vel = {cosf(angle) * spd, sinf(angle) * spd};
                Vec2 offset = {cosf(angle) * size, sinf(angle) * size};

                cmd.spawn()
                    .insert(Transform2D{.position = pos + offset,
                                        .rotation = randf(game->rng, 0.f, TWO_PI)},
                            Sprite{.texture = game->asteroidTex[newLevel],
                                   .origin = {newSize, newSize},
                                   .zOrder = 5.f},
                            Asteroid{.size = newSize, .splitLevel = newLevel},
                            RigidBody2D{.type = BodyType::Dynamic,
                                        .fixedRotation = false,
                                        .gravityScale = 0.f},
                            CircleCollider2D{.radius = newSize,
                                             .restitution = 0.8f,
                                             .filter = {CAT_ASTEROID, CAT_BULLET}},
                            Velocity2D{.linear = vel,
                                       .angular = randf(game->rng, -4.f, 4.f)});
            }
        }
    }
}

void asteroidCleanup(Res<Time> time,
                     QueryMut<Transform2D, Bullet> bulletQuery,
                     QueryMut<Transform2D, Asteroid> asteroidQuery,
                     Commands& cmd) {
    float dt = time->delta;
    float margin = 150.f;

    // Expire bullets by lifetime or out-of-bounds
    bulletQuery.iterWithEntity([&](EntityId id, Transform2D& t, Bullet& b) {
        b.lifetime -= dt;
        if (b.lifetime <= 0.f ||
            t.position.x < -HALF_W - margin || t.position.x > HALF_W + margin ||
            t.position.y < -HALF_H - margin || t.position.y > HALF_H + margin) {
            cmd.entity(id).despawn();
        }
    });

    // Remove asteroids that drifted far off-screen
    float farMargin = 300.f;
    asteroidQuery.iterWithEntity([&](EntityId id, Transform2D& t, Asteroid&) {
        if (t.position.x < -HALF_W - farMargin || t.position.x > HALF_W + farMargin ||
            t.position.y < -HALF_H - farMargin || t.position.y > HALF_H + farMargin) {
            cmd.entity(id).despawn();
        }
    });
}

void hud(ResMut<AsteroidState> game, Res<Time> time,
         ResMut<RendererResource> renderer,
         QueryMut<Transform2D, Asteroid> asteroids,
         Query<Bullet> bullets,
         Commands& cmd) {
    // Smooth FPS with exponential moving average
    float instantFps = (time->delta > 0.f) ? 1.f / time->delta : 60.f;
    game->smoothFps += (instantFps - game->smoothFps) * 0.05f;

    // Count entities + auto-cull in one pass
    int asteroidCount = 0, bulletCount = 0;
    int culled = 0;
    int cullTarget = 0;
    if (game->smoothFps < 60.f) {
        float pressure = (60.f - game->smoothFps) / 60.f;
        cullTarget = static_cast<int>(1.f + pressure * 20.f);
    }
    asteroids.iterWithEntity([&](EntityId id, Transform2D&, Asteroid&) {
        asteroidCount++;
        if (culled < cullTarget) {
            cmd.entity(id).despawn();
            culled++;
        }
    });
    game->cullPerFrame = culled;
    bullets.iter([&](const Bullet&) { bulletCount++; });

    int total = asteroidCount + bulletCount;

    // ImGui overlay
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();

    char buf[128];
    snprintf(buf, sizeof(buf), "FPS: %.0f  Entities: %d  Asteroids: %d  Bullets: %d  Score: %d%s",
             game->smoothFps, total, asteroidCount, bulletCount, game->score,
             game->frenzy ? "  [FRENZY]" : "");

    ImU32 fpsColor = game->smoothFps >= 60.f ? IM_COL32(80, 255, 80, 255)
                   : game->smoothFps >= 30.f ? IM_COL32(255, 200, 60, 255)
                                             : IM_COL32(255, 60, 60, 255);

    // Shadow + text
    dl->AddText(font, 18.f, ImVec2(9, 5), IM_COL32(0, 0, 0, 200), buf);
    dl->AddText(font, 18.f, ImVec2(8, 4), fpsColor, buf);

    if (game->cullPerFrame > 0) {
        char cullBuf[64];
        snprintf(cullBuf, sizeof(cullBuf), "AUTO-CULL: -%d/frame", game->cullPerFrame);
        dl->AddText(font, 16.f, ImVec2(9, 25), IM_COL32(0, 0, 0, 200), cullBuf);
        dl->AddText(font, 16.f, ImVec2(8, 24), IM_COL32(255, 80, 80, 255), cullBuf);
    }
}

// =============================================================================
// Main
// =============================================================================

int main() {
    App app;
    app.setConfig({.title = "Drift - Asteroid Field (Stress Test)",
                   .width = static_cast<int32_t>(W),
                   .height = static_cast<int32_t>(H),
                   .vsync = false});
    app.addPlugins<DefaultPlugins>();
    app.initResource<AsteroidState>();

    app.startup<asteroidSetup>("asteroid_setup");
    app.update<asteroidSpawn>("asteroid_spawn");
    app.update<playerUpdate>("player_update");
    app.update<collisionSystem>("collision_system").after("player_update");
    app.update<asteroidCleanup>("asteroid_cleanup").after("collision_system");
    app.update<hud>("hud");

    return app.run();
}
