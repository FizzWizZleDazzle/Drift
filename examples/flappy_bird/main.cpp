// =============================================================================
// Flappy Bird — built with Drift 2D Engine
// =============================================================================

#include <drift/App.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/AudioResource.hpp>

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

// --- Game state as a Resource ---
enum GameState { STATE_MENU, STATE_PLAYING, STATE_DEAD };

struct Pipe {
    float x;
    float gapY;
    bool  scored;
    bool  active;
};

struct FlappyState : public Resource {
    const char* name() const override { return "FlappyState"; }

    GameState state = STATE_MENU;
    float     birdY = SCREEN_H * 0.4f;
    float     birdVel = 0.f;
    float     birdRot = 0.f;
    int       birdFrame = 1;
    float     birdAnimTimer = 0.f;
    float     baseScroll = 0.f;
    float     pipeTimer = 0.f;
    Pipe      pipes[MAX_PIPES] = {};
    int       score = 0;
    bool      hitPlayed = false;

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
    g.state          = STATE_MENU;
    g.birdY          = SCREEN_H * 0.4f;
    g.birdVel        = 0.f;
    g.birdRot        = 0.f;
    g.birdFrame      = 1;
    g.birdAnimTimer  = 0.f;
    g.baseScroll     = 0.f;
    g.pipeTimer      = 0.f;
    g.score          = 0;
    g.hitPlayed      = false;
    for (int i = 0; i < MAX_PIPES; ++i) g.pipes[i].active = false;
}

static void spawnPipe(FlappyState& g) {
    for (int i = 0; i < MAX_PIPES; ++i) {
        if (!g.pipes[i].active) {
            g.pipes[i].active = true;
            g.pipes[i].x = static_cast<float>(SCREEN_W) + 20.f;
            float minY = PIPE_GAP * 0.5f + 50.f;
            float maxY = BASE_Y - PIPE_GAP * 0.5f - 20.f;
            g.pipes[i].gapY = minY + (static_cast<float>(rand()) / RAND_MAX) * (maxY - minY);
            g.pipes[i].scored = false;
            return;
        }
    }
}

static void flap(FlappyState& g, AudioResource* audio) {
    g.birdVel = FLAP_VELOCITY;
    if (g.sndWing.valid() && audio) audio->playSound(g.sndWing, 0.6f);
}

static void drawScore(RendererResource* renderer, FlappyState& g, int score) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", score);
    int len = static_cast<int>(strlen(buf));

    float totalW = len * 26.f;
    float startX = (SCREEN_W - totalW) * 0.5f;
    float y = 40.f;

    for (int i = 0; i < len; ++i) {
        int digit = buf[i] - '0';
        if (digit < 0 || digit > 9) continue;
        TextureHandle tex = g.texNum[digit];
        if (!tex.valid()) continue;

        int32_t dw, dh;
        renderer->getTextureSize(tex, &dw, &dh);

        renderer->drawSprite(tex,
            {startX + i * 26.f, y},
            {0, 0, static_cast<float>(dw), static_cast<float>(dh)},
            50.f);
    }
}

// --- Startup system ---
void flappyStartup(ResMut<FlappyState> game, ResMut<RendererResource> renderer,
                    ResMut<AudioResource> audio, float) {
    // Configure default camera for pixel-perfect scaling
    auto cam = renderer->getDefaultCamera();
    renderer->setCameraPosition(cam, {SCREEN_W * 0.5f, SCREEN_H * 0.5f});
    renderer->setCameraZoom(cam, static_cast<float>(WINDOW_SCALE));

    // Load textures
    game->texBg       = renderer->loadTexture("assets/background.png");
    game->texBase     = renderer->loadTexture("assets/base.png");
    game->texPipe     = renderer->loadTexture("assets/pipe.png");
    game->texBird[0]  = renderer->loadTexture("assets/bird-down.png");
    game->texBird[1]  = renderer->loadTexture("assets/bird-mid.png");
    game->texBird[2]  = renderer->loadTexture("assets/bird-up.png");
    game->texGameover = renderer->loadTexture("assets/gameover.png");
    game->texMessage  = renderer->loadTexture("assets/message.png");
    for (int i = 0; i < 10; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "assets/num%d.png", i);
        game->texNum[i] = renderer->loadTexture(path);
    }

    // Load sounds
    game->sndWing   = audio->loadSound("assets/wing.wav");
    game->sndHit    = audio->loadSound("assets/hit.wav");
    game->sndPoint  = audio->loadSound("assets/point.wav");
    game->sndDie    = audio->loadSound("assets/die.wav");
    game->sndSwoosh = audio->loadSound("assets/swoosh.wav");

    resetGame(*game);
}

// --- Update system ---
void flappyUpdate(Res<InputResource> input, ResMut<FlappyState> game,
                  ResMut<AudioResource> audio, float dt) {
    if (input->keyPressed(Key::Escape)) return; // App handles quit

    bool action = input->keyPressed(Key::Space) ||
                  input->mouseButtonPressed(MouseButton::Left);

    switch (game->state) {
    case STATE_MENU:
        game->birdAnimTimer += dt;
        if (game->birdAnimTimer >= BIRD_ANIM_SPEED) {
            game->birdAnimTimer = 0.f;
            game->birdFrame = (game->birdFrame + 1) % 3;
        }
        game->birdY = SCREEN_H * 0.4f + sinf(static_cast<float>(game->birdAnimTimer * 20.f)) * 8.f;
        game->baseScroll += PIPE_SPEED * dt;
        if (action) { game->state = STATE_PLAYING; flap(*game, audio.ptr); }
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
        if (action) flap(*game, audio.ptr);
        game->baseScroll += PIPE_SPEED * dt;

        game->pipeTimer += dt;
        if (game->pipeTimer >= PIPE_SPAWN_INTERVAL) {
            game->pipeTimer = 0.f;
            spawnPipe(*game);
        }

        for (int i = 0; i < MAX_PIPES; ++i) {
            Pipe& p = game->pipes[i];
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
}

// --- Render system ---
void flappyRender(Res<FlappyState> game, ResMut<RendererResource> renderer, float) {
    // Background
    renderer->drawSprite(game->texBg, {0, 0},
        {0, 0, static_cast<float>(SCREEN_W), static_cast<float>(SCREEN_H)}, 0.f);

    // Pipes
    for (int i = 0; i < MAX_PIPES; ++i) {
        const Pipe& p = game->pipes[i];
        if (!p.active) continue;

        float botTop = p.gapY + PIPE_GAP * 0.5f;
        float topBottom = p.gapY - PIPE_GAP * 0.5f;

        // Bottom pipe
        renderer->drawSprite(game->texPipe, {p.x, botTop},
            {0, 0, PIPE_WIDTH, PIPE_HEIGHT}, 5.f);

        // Top pipe (flipped)
        renderer->drawSprite(game->texPipe, {p.x, topBottom},
            {0, 0, PIPE_WIDTH, PIPE_HEIGHT}, Vec2::one(), 0.f,
            {0, PIPE_HEIGHT}, Color::white(), Flip::V, 5.f);
    }

    // Base (scrolling ground)
    float baseX = -fmodf(game->baseScroll, 48.f);
    for (float bx = baseX; bx < SCREEN_W + 48.f; bx += 336.f) {
        renderer->drawSprite(game->texBase, {bx, BASE_Y},
            {0, 0, 336, BASE_H}, 10.f);
    }

    // Bird
    renderer->drawSprite(game->texBird[game->birdFrame],
        {BIRD_X, game->birdY},
        {0, 0, BIRD_W, BIRD_H}, Vec2::one(), game->birdRot,
        {BIRD_W * 0.5f, BIRD_H * 0.5f},
        Color::white(), Flip::None, 20.f);

    // Score
    if (game->state == STATE_PLAYING || game->state == STATE_DEAD) {
        drawScore(renderer.ptr, const_cast<FlappyState&>(*game), game->score);
    }

    // Menu overlay
    if (game->state == STATE_MENU) {
        int32_t mw, mh;
        renderer->getTextureSize(game->texMessage, &mw, &mh);
        renderer->drawSprite(game->texMessage,
            {(SCREEN_W - mw) * 0.5f, SCREEN_H * 0.2f},
            {0, 0, static_cast<float>(mw), static_cast<float>(mh)}, 60.f);
    }

    // Game over overlay
    if (game->state == STATE_DEAD) {
        int32_t gow, goh;
        renderer->getTextureSize(game->texGameover, &gow, &goh);
        renderer->drawSprite(game->texGameover,
            {(SCREEN_W - gow) * 0.5f, SCREEN_H * 0.3f},
            {0, 0, static_cast<float>(gow), static_cast<float>(goh)}, 60.f);
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
    app.render<flappyRender>("flappy_render");
    return app.run();
}
