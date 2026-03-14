// =============================================================================
// Drift Engine - Fireball Particles Example
// =============================================================================
// Drag the mouse to aim, release to shoot a fireball. It trails particles as
// it flies and explodes into a shower of sparks when it hits the screen edge.
// =============================================================================

#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/Query.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/ParticleEmitter.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>

#include <cmath>
#include <cstring>

using namespace drift;

static constexpr float W = 1280.f;
static constexpr float H = 720.f;
static constexpr float HALF_W = W * 0.5f;
static constexpr float HALF_H = H * 0.5f;
static constexpr float GRAVITY = 490.f;  // px/s²

struct Fireball { Vec2 velocity; };

struct GameState : public Resource {
    DRIFT_RESOURCE(GameState)

    TextureHandle particleTex;
    TextureHandle fireballTex;
    bool dragging = false;
    Vec2 dragStart;
    Vec2 dragEnd;
};

static Vec2 toWorld(Vec2 screen) {
    return {screen.x - HALF_W, screen.y - HALF_H};
}

// -- Trail emitter config shared by all fireballs --
static EmitterConfig makeTrailConfig(TextureHandle tex) {
    EmitterConfig c;
    c.texture = tex;
    c.spawnRate = 80.f;
    c.maxParticles = 200;
    c.lifetime = {0.3f, 0.6f};
    c.speed = {20.f, 60.f};
    c.angle = {0.f, 6.283f};
    c.sizeStart = {6.f, 10.f};
    c.sizeEnd = {1.f, 2.f};
    c.colorStart = {1.f, 0.9f, 0.2f, 1.f};  // bright yellow
    c.colorEnd = {1.f, 0.3f, 0.0f, 0.f};     // orange, fade out
    c.blendMode = ParticleBlendMode::Additive;
    c.drag = 2.f;
    c.zOrder = 7.f;
    c.velocityInheritance = 1.f;
    return c;
}

// -- Explosion emitter config --
static EmitterConfig makeExplosionConfig(TextureHandle tex) {
    EmitterConfig c;
    c.texture = tex;
    c.spawnRate = 5000.f;       // high rate to fill quickly
    c.maxParticles = 300;
    c.lifetime = {0.8f, 2.0f};
    c.speed = {80.f, 300.f};
    c.angle = {0.f, 6.283f};
    c.sizeStart = {8.f, 14.f};
    c.sizeEnd = {2.f, 4.f};
    c.colorStart = {1.f, 0.95f, 0.3f, 1.f};  // hot yellow
    c.colorEnd = {0.8f, 0.2f, 0.0f, 0.f};     // deep orange, fade
    c.gravity = {0.f, 300.f};
    c.drag = 0.5f;
    c.blendMode = ParticleBlendMode::Additive;
    c.duration = 0.1f;          // spawn for ~100ms then stop
    c.looping = false;
    c.zOrder = 5.f;
    c.boundsMin = {-HALF_W, -HALF_H};
    c.boundsMax = { HALF_W,  HALF_H};
    return c;
}

void setup(ResMut<GameState> game, ResMut<RendererResource> renderer, Commands& cmd) {
    // 4x4 white square texture for particles
    uint8_t white[4 * 4 * 4];
    memset(white, 255, sizeof(white));
    game->particleTex = renderer->createTextureFromPixels(white, 4, 4);

    // 16x16 filled circle texture for fireball sprite
    constexpr int SZ = 16;
    uint8_t circle[SZ * SZ * 4];
    memset(circle, 0, sizeof(circle));
    for (int y = 0; y < SZ; y++) {
        for (int x = 0; x < SZ; x++) {
            float dx = x - 7.5f, dy = y - 7.5f;
            if (dx * dx + dy * dy < 7.5f * 7.5f) {
                int i = (y * SZ + x) * 4;
                circle[i] = circle[i + 1] = circle[i + 2] = circle[i + 3] = 255;
            }
        }
    }
    game->fireballTex = renderer->createTextureFromPixels(circle, SZ, SZ);

    cmd.spawn().insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});
    renderer->setClearColor({0.05f, 0.05f, 0.1f, 1.f});
}

void update(Res<InputResource> input, Res<Time> time,
            ResMut<GameState> game, ResMut<RendererResource> renderer,
            QueryMut<Transform2D, Fireball> fireballs,
            Commands& cmd) {
    Vec2 mouse = toWorld(input->mousePosition());

    // --- Drag aiming ---
    if (input->mouseButtonPressed(MouseButton::Left)) {
        game->dragging = true;
        game->dragStart = mouse;
    }
    if (game->dragging) {
        game->dragEnd = mouse;
        // Draw aim line with dotted appearance
        Vec2 a = game->dragStart, b = game->dragEnd;
        renderer->drawLine(a, b, Color{255, 200, 50, 150}, 2.f);
        renderer->drawCircle(a, 4.f, Color{255, 200, 50, 200});
        renderer->drawCircle(b, 6.f, Color{255, 200, 50, 200});
    }
    if (input->mouseButtonReleased(MouseButton::Left) && game->dragging) {
        game->dragging = false;
        Vec2 dir = game->dragEnd - game->dragStart;

        if (dir.length() > 10.f) {
            cmd.spawn().insert(
                Transform2D{.position = game->dragStart},
                Sprite{.texture = game->fireballTex,
                       .origin = {8.f, 8.f},
                       .tint = Color{255, 180, 50, 255},
                       .zOrder = 5.f},
                Fireball{.velocity = dir * 2.f},
                ParticleEmitter{.config = makeTrailConfig(game->particleTex)});
        }
    }

    // --- Move fireballs with gravity, explode at screen edge ---
    float dt = time->delta;
    fireballs.iterWithEntity([&](EntityId id, Transform2D& t, Fireball& fb) {
        fb.velocity.y += GRAVITY * dt;
        t.position += fb.velocity * dt;

        bool oob = t.position.x < -HALF_W || t.position.x > HALF_W
                || t.position.y < -HALF_H || t.position.y > HALF_H;
        if (!oob) return;

        // Clamp explosion slightly inside so particles are visible
        Vec2 pos{
            fmaxf(-HALF_W + 10.f, fminf(HALF_W - 10.f, t.position.x)),
            fmaxf(-HALF_H + 10.f, fminf(HALF_H - 10.f, t.position.y))
        };

        cmd.spawn().insert(
            Transform2D{.position = pos},
            ParticleEmitter{.config = makeExplosionConfig(game->particleTex)});

        cmd.entity(id).despawn();
    });
}

int main() {
    App app;
    app.setConfig({.title = "Drift - Fireball Particles",
                   .width = static_cast<int32_t>(W),
                   .height = static_cast<int32_t>(H)});
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameState>();
    app.startup<setup>();
    app.update<update>();
    return app.run();
}
