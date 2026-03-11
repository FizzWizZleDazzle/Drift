// =============================================================================
// Drift Engine - Hello Sprites Example
// =============================================================================
// Demonstrates: window creation, texture loading, sprite rendering, input,
// animated sprites, camera control.
// =============================================================================

#include <drift/drift.h>
#include <drift/renderer.h>
#include <drift/sprite.h>
#include <drift/input.h>
#include <drift/audio.h>
#include <drift/ui.h>
#include <drift/ecs.h>
#include <cmath>

struct GameData {
    DriftWorld*     world;
    DriftCamera     camera;
    DriftTexture    texture;
    DriftSpritesheet sheet;
    DriftAnimation  anim;
    DriftVec2       player_pos;
    float           player_speed;
    float           time;
};

static void update(float dt, void* user_data) {
    GameData* game = static_cast<GameData*>(user_data);
    game->time += dt;

    // --- Input ---
    drift_input_update();

    // Process SDL events (input system needs these)
    // Note: drift_run handles SDL_PollEvent, but we need to wire input processing
    // into the event loop. For this example, we poll manually in the update.

    if (drift_key_pressed(DRIFT_KEY_ESCAPE)) {
        drift_request_quit();
        return;
    }

    // Player movement
    float dx = 0.0f, dy = 0.0f;
    if (drift_key_held(DRIFT_KEY_W) || drift_key_held(DRIFT_KEY_UP))    dy -= 1.0f;
    if (drift_key_held(DRIFT_KEY_S) || drift_key_held(DRIFT_KEY_DOWN))  dy += 1.0f;
    if (drift_key_held(DRIFT_KEY_A) || drift_key_held(DRIFT_KEY_LEFT))  dx -= 1.0f;
    if (drift_key_held(DRIFT_KEY_D) || drift_key_held(DRIFT_KEY_RIGHT)) dx += 1.0f;

    // Normalize diagonal movement
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 0.0f) {
        dx /= len;
        dy /= len;
    }

    game->player_pos.x += dx * game->player_speed * dt;
    game->player_pos.y += dy * game->player_speed * dt;

    // Camera follows player
    drift_camera_set_position(game->camera, game->player_pos);

    // Zoom with mouse wheel
    float wheel = drift_mouse_wheel_delta();
    if (wheel != 0.0f) {
        float zoom = drift_camera_get_zoom(game->camera);
        zoom += wheel * 0.1f;
        if (zoom < 0.1f) zoom = 0.1f;
        if (zoom > 10.0f) zoom = 10.0f;
        drift_camera_set_zoom(game->camera, zoom);
    }

    // --- Render ---
    drift_renderer_begin_frame();

#ifdef DRIFT_DEV
    drift_ui_begin_frame();
    if (drift_key_pressed(DRIFT_KEY_F1)) {
        drift_ui_dev_toggle();
    }
#endif

    // Update animation
    DriftRect frame = {0, 0, 0, 0};
    if (game->anim.id != 0) {
        frame = drift_animation_update(game->anim, dt);
    }

    // Draw a grid of background tiles
    for (int y = -5; y <= 5; ++y) {
        for (int x = -5; x <= 5; ++x) {
            DriftColor tint = ((x + y) & 1) ? DriftColor{200, 200, 220, 255}
                                              : DriftColor{180, 180, 200, 255};
            drift_sprite_draw(game->texture,
                              DriftVec2{x * 64.0f, y * 64.0f},
                              DriftRect{0, 0, 0, 0}, // full texture
                              DriftVec2{1.0f, 1.0f},
                              0.0f,
                              DriftVec2{0, 0},
                              tint,
                              DRIFT_FLIP_NONE,
                              0.0f);
        }
    }

    // Draw animated player sprite
    if (game->sheet.id != 0) {
        DriftTexture sheet_tex = drift_spritesheet_get_texture(game->sheet);
        DriftFlip flip = (dx < 0.0f) ? DRIFT_FLIP_H : DRIFT_FLIP_NONE;

        drift_sprite_draw(sheet_tex,
                          game->player_pos,
                          frame,
                          DriftVec2{2.0f, 2.0f},
                          0.0f,
                          DriftVec2{frame.w / 2.0f, frame.h / 2.0f},
                          DRIFT_COLOR_WHITE,
                          flip,
                          1.0f);
    }

    // Draw some spinning sprites
    for (int i = 0; i < 8; ++i) {
        float angle = game->time + i * 0.785f; // PI/4 spacing
        float radius = 150.0f;
        DriftVec2 pos = {
            game->player_pos.x + cosf(angle) * radius,
            game->player_pos.y + sinf(angle) * radius
        };

        DriftColor tint = {
            static_cast<uint8_t>(128 + 127 * sinf(game->time + i)),
            static_cast<uint8_t>(128 + 127 * cosf(game->time + i * 2)),
            static_cast<uint8_t>(200),
            255
        };

        drift_sprite_draw(game->texture, pos,
                          DriftRect{0, 0, 0, 0},
                          DriftVec2{0.5f, 0.5f},
                          angle,
                          DriftVec2{0, 0},
                          tint,
                          DRIFT_FLIP_NONE,
                          0.5f);
    }

#ifdef DRIFT_DEV
    drift_ui_dev_render();
    drift_ui_end_frame();
    drift_ui_render();
#endif

    drift_renderer_end_frame();
    drift_renderer_present();
}

int main(int /*argc*/, char* /*argv*/[]) {
    DriftConfig config = drift_config_default();
    config.title = "Drift - Hello Sprites";
    config.width = 1280;
    config.height = 720;

    if (drift_init(&config) != DRIFT_OK) {
        return 1;
    }

    drift_renderer_init();
    drift_input_init();
    drift_audio_init();
    drift_ui_init();

    GameData game{};
    game.player_pos = {0.0f, 0.0f};
    game.player_speed = 200.0f;
    game.time = 0.0f;

    // Create camera
    game.camera = drift_camera_create();
    drift_camera_set_active(game.camera);

    // Load texture (will use a placeholder if file doesn't exist)
    game.texture = drift_texture_load("assets/sprites/player.png");

    // Create ECS world
    game.world = drift_ecs_world_create();

    DRIFT_LOG_INFO("Hello Sprites example started. WASD to move, mouse wheel to zoom, ESC to quit.");

    drift_run(update, &game);

    // Cleanup
    if (game.world) drift_ecs_world_destroy(game.world);
    if (game.texture.id) drift_texture_destroy(game.texture);
    drift_camera_destroy(game.camera);

    drift_ui_shutdown();
    drift_audio_shutdown();
    drift_renderer_shutdown();
    drift_shutdown();

    return 0;
}
