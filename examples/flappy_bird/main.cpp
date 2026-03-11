// =============================================================================
// Flappy Bird — built with Drift 2D Engine
// =============================================================================
// Uses the flat C API directly. The C# high-level SDK version is in
// bindings/csharp/FlappyBird/.
// =============================================================================

#include <drift/drift.h>
#include <drift/renderer.h>
#include <drift/input.h>
#include <drift/audio.h>
#include <drift/ui.h>

static constexpr int WINDOW_SCALE = 2;
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>

// --- Constants ---
static constexpr int   SCREEN_W       = 288;
static constexpr int   SCREEN_H       = 512;
static constexpr float GRAVITY        = 900.0f;
static constexpr float FLAP_VELOCITY  = -280.0f;
static constexpr float PIPE_SPEED     = 120.0f;
static constexpr float PIPE_SPAWN_INTERVAL = 1.8f;
static constexpr float PIPE_GAP       = 100.0f;
static constexpr float PIPE_WIDTH     = 52.0f;
static constexpr float PIPE_HEIGHT    = 320.0f;
static constexpr float BIRD_W         = 34.0f;
static constexpr float BIRD_H         = 24.0f;
static constexpr float BASE_H         = 112.0f;
static constexpr float BASE_Y         = static_cast<float>(SCREEN_H) - BASE_H;
static constexpr int   MAX_PIPES      = 8;
static constexpr float BIRD_X         = 60.0f;
static constexpr float BIRD_ANIM_SPEED = 0.12f;

// --- Game state ---
enum GameState { STATE_MENU, STATE_PLAYING, STATE_DEAD };

struct Pipe {
    float x;
    float gap_y;   // center of the gap
    bool  scored;
    bool  active;
};

struct Game {
    GameState state;
    float     bird_y;
    float     bird_vel;
    float     bird_rot;
    int       bird_frame;
    float     bird_anim_timer;
    float     base_scroll;
    float     pipe_timer;
    Pipe      pipes[MAX_PIPES];
    int       score;

    // Textures
    DriftTexture tex_bg;
    DriftTexture tex_base;
    DriftTexture tex_pipe;
    DriftTexture tex_bird[3];  // down, mid, up
    DriftTexture tex_gameover;
    DriftTexture tex_message;
    DriftTexture tex_num[10];

    // Sounds
    DriftSound snd_wing;
    DriftSound snd_hit;
    DriftSound snd_point;
    DriftSound snd_die;
    DriftSound snd_swoosh;

    bool hit_played;
};

static Game g;

// --- Helpers ---
static void reset_game() {
    g.state          = STATE_MENU;
    g.bird_y         = SCREEN_H * 0.4f;
    g.bird_vel       = 0.0f;
    g.bird_rot       = 0.0f;
    g.bird_frame     = 1;
    g.bird_anim_timer = 0.0f;
    g.base_scroll    = 0.0f;
    g.pipe_timer     = 0.0f;
    g.score          = 0;
    g.hit_played     = false;
    for (int i = 0; i < MAX_PIPES; ++i) g.pipes[i].active = false;
}

static void spawn_pipe() {
    for (int i = 0; i < MAX_PIPES; ++i) {
        if (!g.pipes[i].active) {
            g.pipes[i].active = true;
            g.pipes[i].x = static_cast<float>(SCREEN_W) + 20.0f;
            // Random gap center between top margin and base
            float min_y = PIPE_GAP * 0.5f + 50.0f;
            float max_y = BASE_Y - PIPE_GAP * 0.5f - 20.0f;
            g.pipes[i].gap_y = min_y + (static_cast<float>(rand()) / RAND_MAX) * (max_y - min_y);
            g.pipes[i].scored = false;
            return;
        }
    }
}

static bool rects_overlap(float ax, float ay, float aw, float ah,
                           float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void flap() {
    g.bird_vel = FLAP_VELOCITY;
    if (g.snd_wing.id) drift_sound_play(g.snd_wing, 0.6f, 0.0f);
}

// --- Draw score using number sprites ---
static void draw_score(int score) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", score);
    int len = (int)strlen(buf);

    // Each digit sprite is ~24x36 px (variable width, but roughly)
    float total_w = len * 26.0f;
    float start_x = (SCREEN_W - total_w) * 0.5f;
    float y = 40.0f;

    for (int i = 0; i < len; ++i) {
        int digit = buf[i] - '0';
        if (digit < 0 || digit > 9) continue;
        DriftTexture tex = g.tex_num[digit];
        if (tex.id == 0) continue;

        int32_t dw, dh;
        drift_texture_get_size(tex, &dw, &dh);

        drift_sprite_draw(tex,
                          DriftVec2{start_x + i * 26.0f, y},
                          DriftRect{0, 0, (float)dw, (float)dh},
                          DriftVec2{1.0f, 1.0f},
                          0.0f,
                          DriftVec2{0, 0},
                          DRIFT_COLOR_WHITE,
                          DRIFT_FLIP_NONE,
                          50.0f);
    }
}

// --- Main update ---
static void update(float dt, void* /*user_data*/) {
    if (drift_key_pressed(DRIFT_KEY_ESCAPE)) {
        drift_request_quit();
        return;
    }

    bool action = drift_key_pressed(DRIFT_KEY_SPACE) ||
                  drift_mouse_button_pressed(DRIFT_MOUSE_LEFT);

    // ---- State machine ----
    switch (g.state) {
    case STATE_MENU:
        // Bobbing bird animation
        g.bird_anim_timer += dt;
        if (g.bird_anim_timer >= BIRD_ANIM_SPEED) {
            g.bird_anim_timer = 0.0f;
            g.bird_frame = (g.bird_frame + 1) % 3;
        }
        g.bird_y = SCREEN_H * 0.4f + sinf(drift_get_time() * 3.0f) * 8.0f;
        g.base_scroll += PIPE_SPEED * dt;

        if (action) {
            g.state = STATE_PLAYING;
            flap();
        }
        break;

    case STATE_PLAYING:
        // Bird physics
        g.bird_vel += GRAVITY * dt;
        g.bird_y += g.bird_vel * dt;

        // Rotation: tilt up on flap, tilt down when falling
        if (g.bird_vel < 0) {
            g.bird_rot = -25.0f * (3.14159f / 180.0f);
        } else {
            g.bird_rot += 3.0f * dt;
            if (g.bird_rot > 1.57f) g.bird_rot = 1.57f; // 90 degrees
        }

        // Bird animation
        g.bird_anim_timer += dt;
        if (g.bird_anim_timer >= BIRD_ANIM_SPEED) {
            g.bird_anim_timer = 0.0f;
            g.bird_frame = (g.bird_frame + 1) % 3;
        }

        // Flap
        if (action) flap();

        // Scroll base
        g.base_scroll += PIPE_SPEED * dt;

        // Spawn pipes
        g.pipe_timer += dt;
        if (g.pipe_timer >= PIPE_SPAWN_INTERVAL) {
            g.pipe_timer = 0.0f;
            spawn_pipe();
        }

        // Move & check pipes
        for (int i = 0; i < MAX_PIPES; ++i) {
            Pipe& p = g.pipes[i];
            if (!p.active) continue;

            p.x -= PIPE_SPEED * dt;

            // Deactivate off-screen pipes
            if (p.x < -PIPE_WIDTH - 10.0f) {
                p.active = false;
                continue;
            }

            // Score
            if (!p.scored && p.x + PIPE_WIDTH < BIRD_X) {
                p.scored = true;
                g.score++;
                if (g.snd_point.id) drift_sound_play(g.snd_point, 0.7f, 0.0f);
            }

            // Collision with top pipe
            float top_pipe_bottom = p.gap_y - PIPE_GAP * 0.5f;
            if (rects_overlap(BIRD_X - BIRD_W * 0.4f, g.bird_y - BIRD_H * 0.4f,
                              BIRD_W * 0.8f, BIRD_H * 0.8f,
                              p.x, top_pipe_bottom - PIPE_HEIGHT,
                              PIPE_WIDTH, PIPE_HEIGHT)) {
                g.state = STATE_DEAD;
            }

            // Collision with bottom pipe
            float bot_pipe_top = p.gap_y + PIPE_GAP * 0.5f;
            if (rects_overlap(BIRD_X - BIRD_W * 0.4f, g.bird_y - BIRD_H * 0.4f,
                              BIRD_W * 0.8f, BIRD_H * 0.8f,
                              p.x, bot_pipe_top,
                              PIPE_WIDTH, PIPE_HEIGHT)) {
                g.state = STATE_DEAD;
            }
        }

        // Hit ground or ceiling
        if (g.bird_y + BIRD_H * 0.5f >= BASE_Y || g.bird_y < 0) {
            g.state = STATE_DEAD;
        }

        if (g.state == STATE_DEAD) {
            if (g.snd_hit.id) drift_sound_play(g.snd_hit, 0.8f, 0.0f);
            g.hit_played = true;
        }
        break;

    case STATE_DEAD:
        // Bird falls
        g.bird_vel += GRAVITY * dt;
        g.bird_y += g.bird_vel * dt;
        g.bird_rot += 4.0f * dt;
        if (g.bird_rot > 1.57f) g.bird_rot = 1.57f;

        if (g.bird_y + BIRD_H * 0.5f > BASE_Y) {
            g.bird_y = BASE_Y - BIRD_H * 0.5f;
            g.bird_vel = 0.0f;
        }

        if (action) {
            reset_game();
            if (g.snd_swoosh.id) drift_sound_play(g.snd_swoosh, 0.5f, 0.0f);
        }
        break;
    }

    // ---- Render ----
    drift_renderer_begin_frame();

    // Background (tiled to fill - bg is 288 wide, same as screen)
    drift_sprite_draw(g.tex_bg,
                      DriftVec2{0, 0},
                      DriftRect{0, 0, (float)SCREEN_W, (float)SCREEN_H},
                      DriftVec2{1, 1}, 0, DriftVec2{0, 0},
                      DRIFT_COLOR_WHITE, DRIFT_FLIP_NONE, 0.0f);

    // Pipes
    for (int i = 0; i < MAX_PIPES; ++i) {
        const Pipe& p = g.pipes[i];
        if (!p.active) continue;

        // Bottom pipe (normal orientation)
        float bot_top = p.gap_y + PIPE_GAP * 0.5f;
        drift_sprite_draw(g.tex_pipe,
                          DriftVec2{p.x, bot_top},
                          DriftRect{0, 0, PIPE_WIDTH, PIPE_HEIGHT},
                          DriftVec2{1, 1}, 0, DriftVec2{0, 0},
                          DRIFT_COLOR_WHITE, DRIFT_FLIP_NONE, 5.0f);

        // Top pipe (flipped vertically)
        float top_bottom = p.gap_y - PIPE_GAP * 0.5f;
        drift_sprite_draw(g.tex_pipe,
                          DriftVec2{p.x, top_bottom},
                          DriftRect{0, 0, PIPE_WIDTH, PIPE_HEIGHT},
                          DriftVec2{1, 1}, 0, DriftVec2{0, PIPE_HEIGHT},
                          DRIFT_COLOR_WHITE, DRIFT_FLIP_V, 5.0f);
    }

    // Base (scrolling ground)
    float base_x = -fmodf(g.base_scroll, 48.0f); // 48px repeat in the base texture
    for (float bx = base_x; bx < SCREEN_W + 48.0f; bx += 336.0f) {
        drift_sprite_draw(g.tex_base,
                          DriftVec2{bx, BASE_Y},
                          DriftRect{0, 0, 336, BASE_H},
                          DriftVec2{1, 1}, 0, DriftVec2{0, 0},
                          DRIFT_COLOR_WHITE, DRIFT_FLIP_NONE, 10.0f);
    }

    // Bird
    drift_sprite_draw(g.tex_bird[g.bird_frame],
                      DriftVec2{BIRD_X, g.bird_y},
                      DriftRect{0, 0, BIRD_W, BIRD_H},
                      DriftVec2{1, 1},
                      g.bird_rot,
                      DriftVec2{BIRD_W * 0.5f, BIRD_H * 0.5f},
                      DRIFT_COLOR_WHITE, DRIFT_FLIP_NONE, 20.0f);

    // Score
    if (g.state == STATE_PLAYING || g.state == STATE_DEAD) {
        draw_score(g.score);
    }

    // Menu overlay
    if (g.state == STATE_MENU) {
        int32_t mw, mh;
        drift_texture_get_size(g.tex_message, &mw, &mh);
        drift_sprite_draw(g.tex_message,
                          DriftVec2{(SCREEN_W - mw) * 0.5f, SCREEN_H * 0.2f},
                          DriftRect{0, 0, (float)mw, (float)mh},
                          DriftVec2{1, 1}, 0, DriftVec2{0, 0},
                          DRIFT_COLOR_WHITE, DRIFT_FLIP_NONE, 60.0f);
    }

    // Game over overlay
    if (g.state == STATE_DEAD) {
        int32_t gow, goh;
        drift_texture_get_size(g.tex_gameover, &gow, &goh);
        drift_sprite_draw(g.tex_gameover,
                          DriftVec2{(SCREEN_W - gow) * 0.5f, SCREEN_H * 0.3f},
                          DriftRect{0, 0, (float)gow, (float)goh},
                          DriftVec2{1, 1}, 0, DriftVec2{0, 0},
                          DRIFT_COLOR_WHITE, DRIFT_FLIP_NONE, 60.0f);
    }

    drift_renderer_end_frame();
    drift_renderer_present();
}

int main(int /*argc*/, char* /*argv*/[]) {
    srand(42);

    DriftConfig config = drift_config_default();
    config.title = "Flappy Bird - Drift Engine";
    config.width = SCREEN_W * WINDOW_SCALE;
    config.height = SCREEN_H * WINDOW_SCALE;
    config.resizable = false;

    if (drift_init(&config) != DRIFT_OK) return 1;
    drift_renderer_init();
    drift_input_init();
    drift_audio_init();

    // Camera maps logical 288x512 coords to the larger window.
    DriftCamera cam = drift_camera_create();
    drift_camera_set_position(cam, DriftVec2{SCREEN_W * 0.5f, SCREEN_H * 0.5f});
    drift_camera_set_zoom(cam, static_cast<float>(WINDOW_SCALE));
    drift_camera_set_active(cam);

    // Load textures
    g.tex_bg       = drift_texture_load("assets/background.png");
    g.tex_base     = drift_texture_load("assets/base.png");
    g.tex_pipe     = drift_texture_load("assets/pipe.png");
    g.tex_bird[0]  = drift_texture_load("assets/bird-down.png");
    g.tex_bird[1]  = drift_texture_load("assets/bird-mid.png");
    g.tex_bird[2]  = drift_texture_load("assets/bird-up.png");
    g.tex_gameover = drift_texture_load("assets/gameover.png");
    g.tex_message  = drift_texture_load("assets/message.png");
    for (int i = 0; i < 10; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "assets/num%d.png", i);
        g.tex_num[i] = drift_texture_load(path);
    }

    // Load sounds
    g.snd_wing  = drift_sound_load("assets/wing.wav");
    g.snd_hit   = drift_sound_load("assets/hit.wav");
    g.snd_point = drift_sound_load("assets/point.wav");
    g.snd_die   = drift_sound_load("assets/die.wav");
    g.snd_swoosh = drift_sound_load("assets/swoosh.wav");

    reset_game();

    DRIFT_LOG_INFO("Flappy Bird started! Space/Click to flap, ESC to quit.");

    drift_run(update, nullptr);

    drift_audio_shutdown();
    drift_renderer_shutdown();
    drift_shutdown();
    return 0;
}
