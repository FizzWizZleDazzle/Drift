// =============================================================================
// Flappy Bird — built with Drift 2D Engine
// =============================================================================
// All rendering via Sprite+Transform2D entities. Zero manual drawSprite.
// Game state in FlappyState resource, synced to entities via Commands.
// =============================================================================

#include <drift/App.hpp>
#include <drift/Commands.h>
#include <drift/AssetServer.h>
#include <drift/components/Sprite.h>
#include <drift/components/Camera.h>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/AudioResource.hpp>
#include <drift/resources/Time.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace drift;

// --- Constants ---
static constexpr int   WINDOW_SCALE       = 2;
static constexpr int   SCREEN_W           = 288;
static constexpr int   SCREEN_H           = 512;
static constexpr float GRAVITY            = 900.f;
static constexpr float FLAP_VELOCITY      = -280.f;
static constexpr float PIPE_SPEED         = 120.f;
static constexpr float PIPE_SPAWN_INTERVAL = 1.8f;
static constexpr float PIPE_GAP           = 100.f;
static constexpr float PIPE_WIDTH         = 52.f;
static constexpr float PIPE_HEIGHT        = 320.f;
static constexpr float BIRD_W             = 34.f;
static constexpr float BIRD_H             = 24.f;
static constexpr float BASE_H             = 112.f;
static constexpr float BASE_Y             = static_cast<float>(SCREEN_H) - BASE_H;
static constexpr int   MAX_PIPES          = 8;
static constexpr float BIRD_X             = 60.f;
static constexpr float BIRD_ANIM_SPEED    = 0.12f;
static constexpr int   MAX_SCORE_DIGITS   = 6;

// --- Game state ---
enum GamePhase { STATE_MENU, STATE_PLAYING, STATE_DEAD };

struct PipeState {
    float x = 0.f;
    float gapY = 0.f;
    bool  scored = false;
    bool  active = false;
    EntityId topEntity = InvalidEntityId;
    EntityId botEntity = InvalidEntityId;
};

struct FlappyState : public Resource {
    DRIFT_RESOURCE(FlappyState)

    GamePhase state = STATE_MENU;
    float birdY = SCREEN_H * 0.4f;
    float birdVel = 0.f;
    float birdRot = 0.f;
    int   birdFrame = 1;
    float birdAnimTimer = 0.f;
    float baseScroll = 0.f;
    float pipeTimer = 0.f;
    int   score = 0;
    bool  hitPlayed = false;
    PipeState pipes[MAX_PIPES] = {};

    // Entities
    EntityId camera = InvalidEntityId;
    EntityId bgEntity = InvalidEntityId;
    EntityId birdEntity = InvalidEntityId;
    EntityId baseEntities[2] = {};
    EntityId scoreDigits[MAX_SCORE_DIGITS] = {};
    EntityId menuEntity = InvalidEntityId;
    EntityId gameoverEntity = InvalidEntityId;

    // Textures
    TextureHandle texBg;
    TextureHandle texBase;
    TextureHandle texPipe;
    TextureHandle texBird[3];
    TextureHandle texGameover;
    TextureHandle texMessage;
    TextureHandle texNum[10];

    // Sounds
    SoundHandle sndWing;
    SoundHandle sndHit;
    SoundHandle sndPoint;
    SoundHandle sndDie;
    SoundHandle sndSwoosh;
};

// --- Helpers ---
static bool rectsOverlap(float ax, float ay, float aw, float ah,
                          float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void resetGame(FlappyState& g) {
    g.state         = STATE_MENU;
    g.birdY         = SCREEN_H * 0.4f;
    g.birdVel       = 0.f;
    g.birdRot       = 0.f;
    g.birdFrame     = 1;
    g.birdAnimTimer = 0.f;
    g.baseScroll    = 0.f;
    g.pipeTimer     = 0.f;
    g.score         = 0;
    g.hitPlayed     = false;
    for (int i = 0; i < MAX_PIPES; ++i) g.pipes[i].active = false;
}

static void spawnPipe(FlappyState& g, Commands& cmd) {
    for (int i = 0; i < MAX_PIPES; ++i) {
        if (!g.pipes[i].active) {
            g.pipes[i].active = true;
            g.pipes[i].x = static_cast<float>(SCREEN_W) + 20.f;
            float minY = PIPE_GAP * 0.5f + 50.f;
            float maxY = BASE_Y - PIPE_GAP * 0.5f - 20.f;
            g.pipes[i].gapY = minY + (static_cast<float>(rand()) / RAND_MAX) * (maxY - minY);
            g.pipes[i].scored = false;
            if (g.pipes[i].botEntity == InvalidEntityId) {
                g.pipes[i].botEntity = cmd.spawn()
                    .insert<Transform2D>({})
                    .insert<Sprite>({.texture = g.texPipe, .zOrder = 5.f});
                g.pipes[i].topEntity = cmd.spawn()
                    .insert<Transform2D>({})
                    .insert<Sprite>({.texture = g.texPipe, .zOrder = 5.f});
            }
            return;
        }
    }
}

static void doFlap(FlappyState& g, AudioResource* audio) {
    g.birdVel = FLAP_VELOCITY;
    if (g.sndWing.valid() && audio) audio->playSound(g.sndWing, 0.6f);
}

// --- Startup ---
void flappyStartup(ResMut<FlappyState> game, ResMut<AssetServer> assets,
                    Commands& cmd) {
    // Textures
    game->texBg       = assets->load<Texture>("assets/background.png");
    game->texBase     = assets->load<Texture>("assets/base.png");
    game->texPipe     = assets->load<Texture>("assets/pipe.png");
    game->texBird[0]  = assets->load<Texture>("assets/bird-down.png");
    game->texBird[1]  = assets->load<Texture>("assets/bird-mid.png");
    game->texBird[2]  = assets->load<Texture>("assets/bird-up.png");
    game->texGameover = assets->load<Texture>("assets/gameover.png");
    game->texMessage  = assets->load<Texture>("assets/message.png");
    for (int i = 0; i < 10; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "assets/num%d.png", i);
        game->texNum[i] = assets->load<Texture>(path);
    }

    // Sounds
    game->sndWing   = assets->load<Sound>("assets/wing.wav");
    game->sndHit    = assets->load<Sound>("assets/hit.wav");
    game->sndPoint  = assets->load<Sound>("assets/point.wav");
    game->sndDie    = assets->load<Sound>("assets/die.wav");
    game->sndSwoosh = assets->load<Sound>("assets/swoosh.wav");

    // Camera (pixel-perfect)
    game->camera = cmd.spawn()
        .insert<Transform2D>({.position = {SCREEN_W * 0.5f, SCREEN_H * 0.5f}})
        .insert<Camera>({.zoom = static_cast<float>(WINDOW_SCALE), .active = true});

    // Background
    game->bgEntity = cmd.spawn()
        .insert<Transform2D>({})
        .insert<Sprite>({.texture = game->texBg, .zOrder = 0.f});

    // Bird
    game->birdEntity = cmd.spawn()
        .insert<Transform2D>({.position = {BIRD_X, SCREEN_H * 0.4f}})
        .insert<Sprite>({.texture = game->texBird[1], .zOrder = 20.f});

    // Base tiles
    game->baseEntities[0] = cmd.spawn()
        .insert<Transform2D>({.position = {0, BASE_Y}})
        .insert<Sprite>({.texture = game->texBase, .zOrder = 10.f});
    game->baseEntities[1] = cmd.spawn()
        .insert<Transform2D>({.position = {336, BASE_Y}})
        .insert<Sprite>({.texture = game->texBase, .zOrder = 10.f});

    // Score digits (hidden until needed)
    for (int i = 0; i < MAX_SCORE_DIGITS; ++i) {
        game->scoreDigits[i] = cmd.spawn()
            .insert<Transform2D>({})
            .insert<Sprite>({.texture = game->texNum[0], .zOrder = 50.f, .visible = false});
    }

    // Menu overlay
    game->menuEntity = cmd.spawn()
        .insert<Transform2D>({})
        .insert<Sprite>({.texture = game->texMessage, .zOrder = 60.f});

    // Game over overlay (hidden)
    game->gameoverEntity = cmd.spawn()
        .insert<Transform2D>({})
        .insert<Sprite>({.texture = game->texGameover, .zOrder = 60.f, .visible = false});

    resetGame(*game);
}

// --- Update ---
void flappyUpdate(Res<InputResource> input, Res<Time> time,
                  ResMut<FlappyState> game,
                  ResMut<AudioResource> audio, Commands& cmd) {
    float dt = time->delta;
    bool action = input->keyPressed(Key::Space) ||
                  input->mouseButtonPressed(MouseButton::Left);

    switch (game->state) {
    case STATE_MENU:
        game->birdAnimTimer += dt;
        if (game->birdAnimTimer >= BIRD_ANIM_SPEED) {
            game->birdAnimTimer = 0.f;
            game->birdFrame = (game->birdFrame + 1) % 3;
        }
        game->birdY = SCREEN_H * 0.4f + sinf(game->birdAnimTimer * 20.f) * 8.f;
        game->baseScroll += PIPE_SPEED * dt;
        if (action) { game->state = STATE_PLAYING; doFlap(*game, audio.ptr); }
        break;

    case STATE_PLAYING:
        game->birdVel += GRAVITY * dt;
        game->birdY += game->birdVel * dt;
        if (game->birdVel < 0) {
            game->birdRot = -25.f * (3.14159f / 180.f);
        } else {
            game->birdRot += 3.f * dt;
            if (game->birdRot > 1.57f) game->birdRot = 1.57f;
        }
        game->birdAnimTimer += dt;
        if (game->birdAnimTimer >= BIRD_ANIM_SPEED) {
            game->birdAnimTimer = 0.f;
            game->birdFrame = (game->birdFrame + 1) % 3;
        }
        if (action) doFlap(*game, audio.ptr);
        game->baseScroll += PIPE_SPEED * dt;

        game->pipeTimer += dt;
        if (game->pipeTimer >= PIPE_SPAWN_INTERVAL) {
            game->pipeTimer = 0.f;
            spawnPipe(*game, cmd);
        }

        for (int i = 0; i < MAX_PIPES; ++i) {
            PipeState& p = game->pipes[i];
            if (!p.active) continue;
            p.x -= PIPE_SPEED * dt;
            if (p.x < -PIPE_WIDTH - 10.f) { p.active = false; continue; }
            if (!p.scored && p.x + PIPE_WIDTH < BIRD_X) {
                p.scored = true;
                game->score++;
                if (game->sndPoint.valid()) audio->playSound(game->sndPoint, 0.7f);
            }
            float topBottom = p.gapY - PIPE_GAP * 0.5f;
            float botTop    = p.gapY + PIPE_GAP * 0.5f;
            if (rectsOverlap(BIRD_X - BIRD_W * 0.4f, game->birdY - BIRD_H * 0.4f,
                             BIRD_W * 0.8f, BIRD_H * 0.8f,
                             p.x, topBottom - PIPE_HEIGHT, PIPE_WIDTH, PIPE_HEIGHT) ||
                rectsOverlap(BIRD_X - BIRD_W * 0.4f, game->birdY - BIRD_H * 0.4f,
                             BIRD_W * 0.8f, BIRD_H * 0.8f,
                             p.x, botTop, PIPE_WIDTH, PIPE_HEIGHT)) {
                game->state = STATE_DEAD;
            }
        }
        if (game->birdY + BIRD_H * 0.5f >= BASE_Y || game->birdY < 0)
            game->state = STATE_DEAD;
        if (game->state == STATE_DEAD) {
            if (game->sndHit.valid()) audio->playSound(game->sndHit, 0.8f);
            game->hitPlayed = true;
        }
        break;

    case STATE_DEAD:
        game->birdVel += GRAVITY * dt;
        game->birdY += game->birdVel * dt;
        game->birdRot += 4.f * dt;
        if (game->birdRot > 1.57f) game->birdRot = 1.57f;
        if (game->birdY + BIRD_H * 0.5f > BASE_Y) {
            game->birdY = BASE_Y - BIRD_H * 0.5f;
            game->birdVel = 0.f;
        }
        if (action) {
            resetGame(*game);
            if (game->sndSwoosh.valid()) audio->playSound(game->sndSwoosh, 0.5f);
        }
        break;
    }

    // ---- Sync all entities from state ----

    // Background
    {
        Sprite s;
        s.texture = game->texBg;
        s.srcRect = {0, 0, static_cast<float>(SCREEN_W), static_cast<float>(SCREEN_H)};
        s.zOrder = 0.f;
        cmd.entity(game->bgEntity).insert<Sprite>(s);
    }

    // Bird
    {
        cmd.entity(game->birdEntity)
            .insert<Transform2D>({.position = {BIRD_X, game->birdY}, .rotation = game->birdRot});

        Sprite s;
        s.texture = game->texBird[game->birdFrame];
        s.srcRect = {0, 0, BIRD_W, BIRD_H};
        s.origin = {BIRD_W * 0.5f, BIRD_H * 0.5f};
        s.zOrder = 20.f;
        cmd.entity(game->birdEntity).insert<Sprite>(s);
    }

    // Pipes
    for (int i = 0; i < MAX_PIPES; ++i) {
        PipeState& p = game->pipes[i];
        if (p.botEntity == InvalidEntityId) continue;

        if (!p.active) {
            Sprite hidden;
            hidden.visible = false;
            cmd.entity(p.botEntity).insert<Sprite>(hidden);
            cmd.entity(p.topEntity).insert<Sprite>(hidden);
            continue;
        }

        float botTop    = p.gapY + PIPE_GAP * 0.5f;
        float topBottom = p.gapY - PIPE_GAP * 0.5f;

        // Bottom pipe
        {
            cmd.entity(p.botEntity)
                .insert<Transform2D>({.position = {p.x, botTop}});
            Sprite s;
            s.texture = game->texPipe;
            s.srcRect = {0, 0, PIPE_WIDTH, PIPE_HEIGHT};
            s.zOrder = 5.f;
            cmd.entity(p.botEntity).insert<Sprite>(s);
        }
        // Top pipe (flipped)
        {
            cmd.entity(p.topEntity)
                .insert<Transform2D>({.position = {p.x, topBottom}});
            Sprite s;
            s.texture = game->texPipe;
            s.srcRect = {0, 0, PIPE_WIDTH, PIPE_HEIGHT};
            s.origin = {0, PIPE_HEIGHT};
            s.flip = Flip::V;
            s.zOrder = 5.f;
            cmd.entity(p.topEntity).insert<Sprite>(s);
        }
    }

    // Base (scrolling)
    {
        float baseX = -fmodf(game->baseScroll, 48.f);
        for (int i = 0; i < 2; ++i) {
            cmd.entity(game->baseEntities[i])
                .insert<Transform2D>({.position = {baseX + i * 336.f, BASE_Y}});
            Sprite s;
            s.texture = game->texBase;
            s.srcRect = {0, 0, 336, BASE_H};
            s.zOrder = 10.f;
            cmd.entity(game->baseEntities[i]).insert<Sprite>(s);
        }
    }

    // Score digits
    {
        bool showScore = (game->state == STATE_PLAYING || game->state == STATE_DEAD);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", game->score);
        int len = showScore ? static_cast<int>(strlen(buf)) : 0;
        float totalW = len * 26.f;
        float startX = (SCREEN_W - totalW) * 0.5f;

        for (int i = 0; i < MAX_SCORE_DIGITS; ++i) {
            if (i < len) {
                int digit = buf[i] - '0';
                if (digit < 0 || digit > 9) digit = 0;

                cmd.entity(game->scoreDigits[i])
                    .insert<Transform2D>({.position = {startX + i * 26.f, 40.f}});

                Sprite s;
                s.texture = game->texNum[digit];
                s.srcRect = {0, 0, 24, 36};
                s.zOrder = 50.f;
                s.visible = true;
                cmd.entity(game->scoreDigits[i]).insert<Sprite>(s);
            } else {
                Sprite s;
                s.visible = false;
                cmd.entity(game->scoreDigits[i]).insert<Sprite>(s);
            }
        }
    }

    // Menu overlay
    {
        Sprite s;
        s.texture = game->texMessage;
        s.srcRect = {0, 0, 184, 267};
        s.zOrder = 60.f;
        s.visible = (game->state == STATE_MENU);
        cmd.entity(game->menuEntity).insert<Sprite>(s);

        cmd.entity(game->menuEntity)
            .insert<Transform2D>({.position = {(SCREEN_W - 184) * 0.5f, SCREEN_H * 0.2f}});
    }

    // Game over overlay
    {
        Sprite s;
        s.texture = game->texGameover;
        s.srcRect = {0, 0, 192, 42};
        s.zOrder = 60.f;
        s.visible = (game->state == STATE_DEAD);
        cmd.entity(game->gameoverEntity).insert<Sprite>(s);

        cmd.entity(game->gameoverEntity)
            .insert<Transform2D>({.position = {(SCREEN_W - 192) * 0.5f, SCREEN_H * 0.3f}});
    }
}

int main(int /*argc*/, char* /*argv*/[]) {
    srand(42);

    App app;
    app.setConfig({
        .title = "Flappy Bird - Drift Engine",
        .width = SCREEN_W * WINDOW_SCALE,
        .height = SCREEN_H * WINDOW_SCALE,
        .resizable = false,
    });
    app.addPlugins<DefaultPlugins>();
    app.addResource<FlappyState>();
    app.startup<flappyStartup>("flappy_startup");
    app.update<flappyUpdate>("flappy_update");
    return app.run();
}
