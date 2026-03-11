// =============================================================================
// Drift Engine - Platformer Example
// =============================================================================
// Demonstrates: tilemap, physics, player controller, particles, UI health bar,
// camera follow, collision events, sprite animation.
// =============================================================================

#include <drift/drift.h>
#include <drift/renderer.h>
#include <drift/sprite.h>
#include <drift/input.h>
#include <drift/audio.h>
#include <drift/physics.h>
#include <drift/tilemap.h>
#include <drift/particles.h>
#include <drift/font.h>
#include <drift/ui.h>
#include <drift/ecs.h>
#include <cmath>
#include <cstdio>

// --- Game constants ---
static constexpr float GRAVITY        = 9.8f * 64.0f; // pixels/s^2
static constexpr float MOVE_SPEED     = 200.0f;
static constexpr float JUMP_IMPULSE   = -400.0f;
static constexpr float MAX_HEALTH     = 100.0f;

struct Player {
    DriftBody body;
    DriftShape shape;
    DriftVec2 spawn_pos;
    float health;
    bool on_ground;
    bool facing_left;
};

struct GameData {
    DriftWorld*     world;
    DriftCamera     camera;
    DriftTilemap    tilemap;
    DriftTexture    player_tex;
    DriftEmitter    dust_emitter;
    DriftFont       font;
    Player          player;
    float           time;
};

static void setup_tilemap_collision(GameData* game) {
    // Get collision rectangles from the tilemap and create static physics bodies
    DriftRect rects[1024];
    int32_t count = drift_tilemap_get_collision_rects(game->tilemap, rects, 1024);

    for (int32_t i = 0; i < count; ++i) {
        DriftBodyDef bdef = drift_body_def_default();
        bdef.type = DRIFT_BODY_STATIC;
        bdef.position = {rects[i].x + rects[i].w * 0.5f, rects[i].y + rects[i].h * 0.5f};

        DriftBody body = drift_body_create(&bdef);

        DriftShapeDef sdef = drift_shape_def_default();
        sdef.friction = 0.5f;
        drift_shape_add_box(body, rects[i].w * 0.5f, rects[i].h * 0.5f, &sdef);
    }

    DRIFT_LOG_INFO("Created %d collision bodies from tilemap", count);
}

static void setup_player(GameData* game) {
    game->player.spawn_pos = {128.0f, 128.0f};
    game->player.health = MAX_HEALTH;
    game->player.on_ground = false;
    game->player.facing_left = false;

    // Create player physics body
    DriftBodyDef bdef = drift_body_def_default();
    bdef.type = DRIFT_BODY_DYNAMIC;
    bdef.position = game->player.spawn_pos;
    bdef.fixed_rotation = true;

    game->player.body = drift_body_create(&bdef);

    DriftShapeDef sdef = drift_shape_def_default();
    sdef.friction = 0.3f;
    sdef.density = 1.0f;
    game->player.shape = drift_shape_add_box(game->player.body, 8.0f, 16.0f, &sdef);

    // Create a sensor at the feet for ground detection
    DriftShapeDef sensor_def = drift_shape_def_default();
    sensor_def.is_sensor = true;
    drift_shape_add_box(game->player.body, 6.0f, 2.0f, &sensor_def);
}

static void update(float dt, void* user_data) {
    GameData* game = static_cast<GameData*>(user_data);
    game->time += dt;

    drift_input_update();

    if (drift_key_pressed(DRIFT_KEY_ESCAPE)) {
        drift_request_quit();
        return;
    }

    // --- Player input ---
    float move_x = 0.0f;
    if (drift_key_held(DRIFT_KEY_A) || drift_key_held(DRIFT_KEY_LEFT))  move_x -= 1.0f;
    if (drift_key_held(DRIFT_KEY_D) || drift_key_held(DRIFT_KEY_RIGHT)) move_x += 1.0f;

    if (move_x < 0.0f) game->player.facing_left = true;
    if (move_x > 0.0f) game->player.facing_left = false;

    // Apply horizontal velocity
    DriftVec2 vel = drift_body_get_velocity(game->player.body);
    vel.x = move_x * MOVE_SPEED;
    drift_body_set_velocity(game->player.body, vel);

    // Jump
    if ((drift_key_pressed(DRIFT_KEY_SPACE) || drift_key_pressed(DRIFT_KEY_W) ||
         drift_key_pressed(DRIFT_KEY_UP)) && game->player.on_ground) {
        drift_body_apply_impulse(game->player.body, {0.0f, JUMP_IMPULSE});
        game->player.on_ground = false;

        // Spawn dust particles on jump
        DriftVec2 player_pos = drift_body_get_position(game->player.body);
        drift_emitter_set_position(game->dust_emitter, {player_pos.x, player_pos.y + 16.0f});
        drift_emitter_burst(game->dust_emitter, 10);
    }

    // --- Physics step ---
    drift_physics_step(dt);

    // Check sensor events for ground detection
    int32_t sensor_begin_count = drift_physics_get_sensor_begin_count();
    if (sensor_begin_count > 0) {
        game->player.on_ground = true;
    }
    int32_t sensor_end_count = drift_physics_get_sensor_end_count();
    if (sensor_end_count > 0) {
        game->player.on_ground = false;
    }

    // --- Update particles ---
    drift_particles_update(dt);

    // --- Camera follow ---
    DriftVec2 player_pos = drift_body_get_position(game->player.body);
    DriftVec2 cam_pos = drift_camera_get_position(game->camera);
    float lerp_speed = 5.0f;
    cam_pos.x += (player_pos.x - cam_pos.x) * lerp_speed * dt;
    cam_pos.y += (player_pos.y - cam_pos.y) * lerp_speed * dt;
    drift_camera_set_position(game->camera, cam_pos);

    // Respawn if fallen off
    if (player_pos.y > 1000.0f) {
        drift_body_set_position(game->player.body, game->player.spawn_pos);
        drift_body_set_velocity(game->player.body, {0, 0});
        game->player.health = MAX_HEALTH;
    }

    // --- Render ---
    drift_renderer_begin_frame();

#ifdef DRIFT_DEV
    drift_ui_begin_frame();
    if (drift_key_pressed(DRIFT_KEY_F1)) {
        drift_ui_dev_toggle();
    }
#endif

    // Draw tilemap
    if (game->tilemap.id != 0) {
        drift_tilemap_draw(game->tilemap);
    }

    // Draw player
    DriftFlip flip = game->player.facing_left ? DRIFT_FLIP_H : DRIFT_FLIP_NONE;
    drift_sprite_draw(game->player_tex,
                      player_pos,
                      DriftRect{0, 0, 0, 0},
                      DriftVec2{1.0f, 1.0f},
                      0.0f,
                      DriftVec2{8.0f, 16.0f}, // center-bottom origin
                      DRIFT_COLOR_WHITE,
                      flip,
                      5.0f);

    // Draw particles
    drift_particles_render();

    // --- UI ---
    // Health bar
    drift_ui_panel_begin(DriftRect{10, 10, 200, 40}, DRIFT_PANEL_NONE);
    drift_ui_label("HP", DriftVec2{15, 15});
    drift_ui_progress_bar(game->player.health / MAX_HEALTH, DriftRect{50, 15, 150, 20});
    drift_ui_panel_end();

    // Score/info text
    if (game->font.id != 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Pos: %.0f, %.0f", player_pos.x, player_pos.y);
        drift_text_draw(game->font, buf, DriftVec2{10, 60}, DRIFT_COLOR_WHITE, 1.0f);
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
    config.title = "Drift - Platformer";
    config.width = 1280;
    config.height = 720;

    if (drift_init(&config) != DRIFT_OK) {
        return 1;
    }

    drift_renderer_init();
    drift_input_init();
    drift_audio_init();
    drift_physics_init({0.0f, GRAVITY});
    drift_ui_init();

    GameData game{};
    game.time = 0.0f;

    // Camera
    game.camera = drift_camera_create();
    drift_camera_set_active(game.camera);
    drift_camera_set_zoom(game.camera, 2.0f);

    // ECS world
    game.world = drift_ecs_world_create();

    // Load resources
    game.player_tex = drift_texture_load("assets/sprites/player.png");
    game.tilemap = drift_tilemap_load("assets/levels/level1.tmj");
    game.font = drift_font_load("assets/fonts/default.ttf", 16.0f);

    // Dust particle emitter
    DriftEmitterDef dust_def = drift_emitter_def_default();
    dust_def.spawn_rate = 0.0f; // burst only
    dust_def.lifetime_min = 0.2f;
    dust_def.lifetime_max = 0.5f;
    dust_def.speed_min = 20.0f;
    dust_def.speed_max = 60.0f;
    dust_def.angle_min = -3.14159f;
    dust_def.angle_max = 0.0f; // upward semicircle
    dust_def.color_start = {0.8f, 0.8f, 0.7f, 1.0f};
    dust_def.color_end   = {0.8f, 0.8f, 0.7f, 0.0f};
    dust_def.size_start = 3.0f;
    dust_def.size_end = 1.0f;
    dust_def.gravity = -50.0f;
    game.dust_emitter = drift_emitter_create(&dust_def);

    // Setup physics
    setup_player(&game);
    if (game.tilemap.id != 0) {
        setup_tilemap_collision(&game);
    }

    DRIFT_LOG_INFO("Platformer example started. A/D to move, Space to jump, ESC to quit.");

    drift_run(update, &game);

    // Cleanup
    drift_emitter_destroy(game.dust_emitter);
    if (game.font.id) drift_font_destroy(game.font);
    if (game.tilemap.id) drift_tilemap_destroy(game.tilemap);
    if (game.player_tex.id) drift_texture_destroy(game.player_tex);
    if (game.world) drift_ecs_world_destroy(game.world);
    drift_camera_destroy(game.camera);
    drift_body_destroy(game.player.body);

    drift_ui_shutdown();
    drift_physics_shutdown();
    drift_audio_shutdown();
    drift_renderer_shutdown();
    drift_shutdown();

    return 0;
}
