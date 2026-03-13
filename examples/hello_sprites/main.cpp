// =============================================================================
// Drift Engine - Hello Sprites Example
// =============================================================================
// Demonstrates: Commands, Sprite+Transform2D entities, auto-rendering,
// Camera entity, AssetServer. Zero manual drawSprite calls.
// =============================================================================

#include <drift/App.hpp>
#include <drift/Commands.h>
#include <drift/AssetServer.h>
#include <drift/components/Sprite.h>
#include <drift/components/Camera.h>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>

#include <cmath>

using namespace drift;

static constexpr int GRID_SIZE = 11; // -5..5

struct GameState : public Resource {
    const char* name() const override { return "GameState"; }

    TextureHandle texture;

    // Authoritative state
    Vec2 playerPos = {0.f, 0.f};
    float playerSpeed = 200.f;
    float time = 0.f;

    // Entity IDs
    Entity player = InvalidEntity;
    Entity orbiters[8] = {};
    Entity camera = InvalidEntity;
    Entity grid[GRID_SIZE * GRID_SIZE] = {};
};

void setupGame(ResMut<GameState> game, ResMut<AssetServer> assets,
               Commands& cmd, float) {
    game->texture = assets->loadTexture("assets/sprites/player.png");

    // Spawn camera
    game->camera = cmd.spawnCamera({0.f, 0.f}, 1.f, true);

    // Spawn player
    game->player = cmd.spawnSprite(game->texture, {0.f, 0.f}, 1.f);

    // Spawn orbiters
    for (int i = 0; i < 8; ++i)
        game->orbiters[i] = cmd.spawnSprite(game->texture, {0.f, 0.f}, 0.5f);

    // Spawn grid background tiles
    for (int y = -5; y <= 5; ++y) {
        for (int x = -5; x <= 5; ++x) {
            int idx = (y + 5) * GRID_SIZE + (x + 5);
            Entity e = cmd.spawnSprite(game->texture, {x * 64.f, y * 64.f}, -1.f);
            game->grid[idx] = e;

            // Set tint via Sprite
            Sprite s;
            s.texture = game->texture;
            s.tint = ((x + y) & 1)
                ? Color{200, 200, 220, 255}
                : Color{180, 180, 200, 255};
            s.zOrder = -1.f;
            cmd.setSprite(e, s);
        }
    }
}

void updateGame(Res<InputResource> input, ResMut<GameState> game,
                Commands& cmd, float dt) {
    game->time += dt;

    // Player movement
    Vec2 dir{};
    if (input->keyHeld(Key::W) || input->keyHeld(Key::Up))    dir.y -= 1.f;
    if (input->keyHeld(Key::S) || input->keyHeld(Key::Down))  dir.y += 1.f;
    if (input->keyHeld(Key::A) || input->keyHeld(Key::Left))  dir.x -= 1.f;
    if (input->keyHeld(Key::D) || input->keyHeld(Key::Right)) dir.x += 1.f;
    game->playerPos += dir.normalized() * game->playerSpeed * dt;

    // Sync player entity
    Transform2D playerT;
    playerT.position = game->playerPos;
    cmd.setTransform(game->player, playerT);

    // Camera follows player
    Transform2D camT;
    camT.position = game->playerPos;
    cmd.setTransform(game->camera, camT);

    // Zoom with mouse wheel
    float wheel = input->mouseWheelDelta();
    static float zoom = 1.f;
    if (wheel != 0.f) {
        zoom += wheel * 0.1f;
        if (zoom < 0.1f) zoom = 0.1f;
        if (zoom > 10.f) zoom = 10.f;
        Camera cam;
        cam.zoom = zoom;
        cam.active = true;
        cmd.setCamera(game->camera, cam);
    }

    // Sync orbiters
    for (int i = 0; i < 8; ++i) {
        float angle = game->time + i * 0.785f;
        float radius = 150.f;

        Transform2D t;
        t.position = {
            game->playerPos.x + cosf(angle) * radius,
            game->playerPos.y + sinf(angle) * radius
        };
        t.rotation = angle;
        t.scale = {0.5f, 0.5f};
        cmd.setTransform(game->orbiters[i], t);

        Sprite s;
        s.texture = game->texture;
        s.tint = {
            static_cast<uint8_t>(128 + 127 * sinf(game->time + i)),
            static_cast<uint8_t>(128 + 127 * cosf(game->time + i * 2)),
            200, 255
        };
        s.zOrder = 0.5f;
        cmd.setSprite(game->orbiters[i], s);
    }
}

int main(int /*argc*/, char* /*argv*/[]) {
    App app;
    app.setConfig({.title = "Drift - Hello Sprites", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.addResource<GameState>();
    app.startup<setupGame>();
    app.update<updateGame>();
    return app.run();
}
