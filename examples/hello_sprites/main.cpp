// =============================================================================
// Drift Engine - Hello Sprites Example
// =============================================================================
// Demonstrates: Commands, Sprite+Transform2D entities, auto-rendering,
// Camera entity, AssetServer, Query system parameters.
// =============================================================================

#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/AssetServer.hpp>
#include <drift/Query.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/Time.hpp>

#include <cmath>

using namespace drift;

static constexpr int GRID_SIZE = 11; // -5..5

// Marker components
struct PlayerTag {};
struct OrbiterTag {};

struct GameState : public Resource {
    TextureHandle texture;

    // Authoritative state
    Vec2 playerPos = {0.f, 0.f};
    float playerSpeed = 200.f;
    float time = 0.f;

    // Entity IDs (for specific entity references)
    EntityId camera = InvalidEntityId;
};

void setupGame(ResMut<GameState> game, ResMut<AssetServer> assets,
               Commands& cmd) {
    game->texture = assets->load<Texture>("assets/sprites/player.png");

    // Spawn camera
    game->camera = cmd.spawn()
        .insert<Transform2D>({.position = {0, 0}})
        .insert<Camera>({.zoom = 1.f, .active = true});

    // Spawn player
    cmd.spawn()
        .insert<Transform2D>({})
        .insert<Sprite>({.texture = game->texture, .zOrder = 1.f})
        .insert<PlayerTag>({});

    // Spawn orbiters
    for (int i = 0; i < 8; ++i)
        cmd.spawn()
            .insert<Transform2D>({})
            .insert<Sprite>({.texture = game->texture, .zOrder = 0.5f})
            .insert<OrbiterTag>({});

    // Spawn grid background tiles
    for (int y = -5; y <= 5; ++y) {
        for (int x = -5; x <= 5; ++x) {
            Sprite s;
            s.texture = game->texture;
            s.tint = ((x + y) & 1)
                ? Color{200, 200, 220, 255}
                : Color{180, 180, 200, 255};
            s.zOrder = -1.f;

            cmd.spawn()
                .insert<Transform2D>({.position = {x * 64.f, y * 64.f}})
                .insert<Sprite>(s);
        }
    }
}

void updateGame(Res<InputResource> input, Res<Time> time,
                ResMut<GameState> game,
                QueryMut<Transform2D, With<PlayerTag>> players,
                QueryMut<Transform2D, Sprite, With<OrbiterTag>> orbiters,
                Commands& cmd) {
    game->time += time->delta;

    // Player movement
    Vec2 dir{};
    if (input->keyHeld(Key::W) || input->keyHeld(Key::Up))    dir.y -= 1.f;
    if (input->keyHeld(Key::S) || input->keyHeld(Key::Down))  dir.y += 1.f;
    if (input->keyHeld(Key::A) || input->keyHeld(Key::Left))  dir.x -= 1.f;
    if (input->keyHeld(Key::D) || input->keyHeld(Key::Right)) dir.x += 1.f;
    game->playerPos += dir.normalized() * game->playerSpeed * time->delta;

    // Query-based: directly update player transform
    players.iter([&](Transform2D& t) {
        t.position = game->playerPos;
    });

    // Camera follows player (EntityId-based for specific reference)
    cmd.entity(game->camera)
        .insert<Transform2D>({.position = game->playerPos});

    // Zoom with mouse wheel
    float wheel = input->mouseWheelDelta();
    static float zoom = 1.f;
    if (wheel != 0.f) {
        zoom += wheel * 0.1f;
        if (zoom < 0.1f) zoom = 0.1f;
        if (zoom > 10.f) zoom = 10.f;
        cmd.entity(game->camera)
            .insert<Camera>({.zoom = zoom, .active = true});
    }

    // Query-based: update all orbiters
    int orbiterIdx = 0;
    orbiters.iter([&](Transform2D& t, Sprite& s) {
        float angle = game->time + orbiterIdx * 0.785f;
        float radius = 150.f;

        t.position = {
            game->playerPos.x + cosf(angle) * radius,
            game->playerPos.y + sinf(angle) * radius
        };
        t.rotation = angle;
        t.scale = {0.5f, 0.5f};

        s.texture = game->texture;
        s.tint = {
            static_cast<uint8_t>(128 + 127 * sinf(game->time + orbiterIdx)),
            static_cast<uint8_t>(128 + 127 * cosf(game->time + orbiterIdx * 2)),
            200, 255
        };
        s.zOrder = 0.5f;

        orbiterIdx++;
    });
}

int main(int /*argc*/, char* /*argv*/[]) {
    App app;
    app.setConfig({.title = "Drift - Hello Sprites", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.addResource<GameState>();

    // Register marker components
    app.world().registerComponent<PlayerTag>("PlayerTag");
    app.world().registerComponent<OrbiterTag>("OrbiterTag");

    app.startup<setupGame>();
    app.update<updateGame>();
    return app.run();
}
