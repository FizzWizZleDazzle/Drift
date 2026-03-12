// =============================================================================
// Drift Engine - Hello Sprites Example
// =============================================================================
// Demonstrates: MinimalPlugins, texture loading, sprite rendering, input,
// camera control.
// =============================================================================

#include <drift/App.h>
#include <drift/plugins/DefaultPlugins.h>
#include <drift/resources/RendererResource.h>
#include <drift/resources/InputResource.h>

#include <cmath>

using namespace drift;

struct GameState : public Resource {
    const char* name() const override { return "GameState"; }

    TextureHandle texture;
    Vec2 playerPos = {0.f, 0.f};
    float playerSpeed = 200.f;
    float time = 0.f;
};

void setupGame(ResMut<GameState> game, ResMut<RendererResource> renderer, float) {
    game->texture = renderer->loadTexture("assets/sprites/player.png");
}

void updateGame(Res<InputResource> input, ResMut<GameState> game,
                ResMut<RendererResource> renderer, float dt) {
    game->time += dt;

    // Player movement
    Vec2 dir{};
    if (input->keyHeld(Key::W) || input->keyHeld(Key::Up))    dir.y -= 1.f;
    if (input->keyHeld(Key::S) || input->keyHeld(Key::Down))  dir.y += 1.f;
    if (input->keyHeld(Key::A) || input->keyHeld(Key::Left))  dir.x -= 1.f;
    if (input->keyHeld(Key::D) || input->keyHeld(Key::Right)) dir.x += 1.f;

    game->playerPos = game->playerPos + dir.normalized() * game->playerSpeed * dt;

    // Camera follows player
    auto cam = renderer->getDefaultCamera();
    renderer->setCameraPosition(cam, game->playerPos);

    // Zoom with mouse wheel
    float wheel = input->mouseWheelDelta();
    if (wheel != 0.f) {
        float zoom = renderer->getCameraZoom(cam);
        zoom += wheel * 0.1f;
        if (zoom < 0.1f) zoom = 0.1f;
        if (zoom > 10.f) zoom = 10.f;
        renderer->setCameraZoom(cam, zoom);
    }
}

void renderGame(Res<GameState> game, ResMut<RendererResource> renderer, float) {
    // Draw a grid of background tiles
    for (int y = -5; y <= 5; ++y) {
        for (int x = -5; x <= 5; ++x) {
            Color tint = ((x + y) & 1)
                ? Color{200, 200, 220, 255}
                : Color{180, 180, 200, 255};
            renderer->drawSprite(game->texture,
                {x * 64.f, y * 64.f}, {}, Vec2::one(), 0.f, {},
                tint, Flip::None, 0.f);
        }
    }

    // Draw player
    renderer->drawSprite(game->texture, game->playerPos, 1.f);

    // Draw spinning sprites around player
    for (int i = 0; i < 8; ++i) {
        float angle = game->time + i * 0.785f;
        float radius = 150.f;
        Vec2 pos = {
            game->playerPos.x + cosf(angle) * radius,
            game->playerPos.y + sinf(angle) * radius
        };

        Color tint = {
            static_cast<uint8_t>(128 + 127 * sinf(game->time + i)),
            static_cast<uint8_t>(128 + 127 * cosf(game->time + i * 2)),
            200, 255
        };

        renderer->drawSprite(game->texture, pos, {},
            {0.5f, 0.5f}, angle, {}, tint, Flip::None, 0.5f);
    }
}

int main(int /*argc*/, char* /*argv*/[]) {
    App app;
    app.setConfig({.title = "Drift - Hello Sprites", .width = 1280, .height = 720});
    app.addPlugins<MinimalPlugins>();
    app.addResource<GameState>();
    app.startup<setupGame>();
    app.update<updateGame>();
    app.render<renderGame>();
    return app.run();
}
