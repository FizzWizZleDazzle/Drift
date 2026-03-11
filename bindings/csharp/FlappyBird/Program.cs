// =============================================================================
// Flappy Bird — Drift C# High-Level SDK
// =============================================================================
// Uses the idiomatic Drift C# SDK (Layer 2) from Drift/DriftApp.cs
// =============================================================================

using System;
using System.Numerics;
using Drift;
using Drift.Native;

const int ScreenW = 288;
const int ScreenH = 512;
const float Gravity = 900f;
const float FlapVelocity = -280f;
const float PipeSpeed = 120f;
const float PipeSpawnInterval = 1.8f;
const float PipeGap = 100f;
const float PipeWidth = 52f;
const float PipeHeight = 320f;
const float BirdW = 34f;
const float BirdH = 24f;
const float BaseH = 112f;
const float BaseY = ScreenH - BaseH;
const float BirdX = 60f;
const float BirdAnimSpeed = 0.12f;
const int MaxPipes = 8;

// --- State (using int constants since top-level statements can't have type declarations) ---
const int StateMenu = 0, StatePlaying = 1, StateDead = 2;

int state = StateMenu;
float birdY = ScreenH * 0.4f;
float birdVel = 0f;
float birdRot = 0f;
int birdFrame = 1;
float birdAnimTimer = 0f;
float baseScroll = 0f;
float pipeTimer = 0f;
int score = 0;
bool hitPlayed = false;

var pipes = new (float x, float gapY, bool scored, bool active)[MaxPipes];
for (int i = 0; i < MaxPipes; i++) pipes[i].active = false;

var rng = new Random(42);

// --- Textures & sounds (loaded in startup) ---
Texture? texBg = null, texBase = null, texPipe = null;
Texture?[] texBird = new Texture?[3];
Texture? texGameover = null, texMessage = null;
Texture?[] texNum = new Texture?[10];
Sound? sndWing = null, sndHit = null, sndPoint = null, sndDie = null, sndSwoosh = null;

// --- Helpers ---
void ResetGame()
{
    state = StateMenu;
    birdY = ScreenH * 0.4f;
    birdVel = 0; birdRot = 0;
    birdFrame = 1; birdAnimTimer = 0;
    baseScroll = 0; pipeTimer = 0;
    score = 0; hitPlayed = false;
    for (int i = 0; i < MaxPipes; i++) pipes[i].active = false;
}

void SpawnPipe()
{
    for (int i = 0; i < MaxPipes; i++)
    {
        if (!pipes[i].active)
        {
            float minY = PipeGap * 0.5f + 50f;
            float maxY = BaseY - PipeGap * 0.5f - 20f;
            pipes[i] = (ScreenW + 20f, minY + (float)rng.NextDouble() * (maxY - minY), false, true);
            return;
        }
    }
}

bool Overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh)
    => ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;

void Flap()
{
    birdVel = FlapVelocity;
    sndWing?.Play(0.6f);
}

void DrawScore(int s)
{
    string digits = s.ToString();
    float totalW = digits.Length * 26f;
    float startX = (ScreenW - totalW) * 0.5f;

    for (int i = 0; i < digits.Length; i++)
    {
        int d = digits[i] - '0';
        texNum[d]?.Draw(new Vector2(startX + i * 26f, 40f), zOrder: 50f);
    }
}

// --- App ---
const int WindowScale = 2;

var app = new DriftApp
{
    Title = "Flappy Bird - Drift C# SDK",
    Width = ScreenW * WindowScale,
    Height = ScreenH * WindowScale,
    Resizable = false,
};

app.OnStartup = () =>
{
    // Camera maps logical 288x512 coords to the larger window.
    var cam = DriftNative.drift_camera_create();
    DriftNative.drift_camera_set_position(cam, new DriftVec2(ScreenW * 0.5f, ScreenH * 0.5f));
    DriftNative.drift_camera_set_zoom(cam, WindowScale);
    DriftNative.drift_camera_set_active(cam);

    texBg       = Assets.LoadTexture("assets/background.png");
    texBase     = Assets.LoadTexture("assets/base.png");
    texPipe     = Assets.LoadTexture("assets/pipe.png");
    texBird[0]  = Assets.LoadTexture("assets/bird-down.png");
    texBird[1]  = Assets.LoadTexture("assets/bird-mid.png");
    texBird[2]  = Assets.LoadTexture("assets/bird-up.png");
    texGameover = Assets.LoadTexture("assets/gameover.png");
    texMessage  = Assets.LoadTexture("assets/message.png");
    for (int i = 0; i < 10; i++)
        texNum[i] = Assets.LoadTexture($"assets/num{i}.png");

    sndWing   = Assets.LoadSound("assets/wing.wav");
    sndHit    = Assets.LoadSound("assets/hit.wav");
    sndPoint  = Assets.LoadSound("assets/point.wav");
    sndDie    = Assets.LoadSound("assets/die.wav");
    sndSwoosh = Assets.LoadSound("assets/swoosh.wav");

    ResetGame();
};

app.OnUpdate = (float dt) =>
{
    bool action = Input.KeyPressed(DriftKey.Space);
    double time = DriftNative.drift_get_time();

    switch (state)
    {
        case 0: // Menu
            birdAnimTimer += dt;
            if (birdAnimTimer >= BirdAnimSpeed)
            {
                birdAnimTimer = 0; birdFrame = (birdFrame + 1) % 3;
            }
            birdY = ScreenH * 0.4f + MathF.Sin((float)time * 3f) * 8f;
            baseScroll += PipeSpeed * dt;
            if (action) { state = StatePlaying; Flap(); }
            break;

        case 1: // Playing
            birdVel += Gravity * dt;
            birdY += birdVel * dt;
            birdRot = birdVel < 0 ? -25f * MathF.PI / 180f : MathF.Min(birdRot + 3f * dt, 1.57f);

            birdAnimTimer += dt;
            if (birdAnimTimer >= BirdAnimSpeed)
            {
                birdAnimTimer = 0; birdFrame = (birdFrame + 1) % 3;
            }
            if (action) Flap();
            baseScroll += PipeSpeed * dt;

            pipeTimer += dt;
            if (pipeTimer >= PipeSpawnInterval) { pipeTimer = 0; SpawnPipe(); }

            for (int i = 0; i < MaxPipes; i++)
            {
                if (!pipes[i].active) continue;
                pipes[i].x -= PipeSpeed * dt;
                if (pipes[i].x < -PipeWidth - 10f) { pipes[i].active = false; continue; }

                if (!pipes[i].scored && pipes[i].x + PipeWidth < BirdX)
                {
                    pipes[i].scored = true;
                    score++;
                    sndPoint?.Play(0.7f);
                }

                float topBottom = pipes[i].gapY - PipeGap * 0.5f;
                float botTop = pipes[i].gapY + PipeGap * 0.5f;

                if (Overlap(BirdX - BirdW * 0.4f, birdY - BirdH * 0.4f, BirdW * 0.8f, BirdH * 0.8f,
                            pipes[i].x, topBottom - PipeHeight, PipeWidth, PipeHeight) ||
                    Overlap(BirdX - BirdW * 0.4f, birdY - BirdH * 0.4f, BirdW * 0.8f, BirdH * 0.8f,
                            pipes[i].x, botTop, PipeWidth, PipeHeight))
                {
                    state = StateDead;
                }
            }

            if (birdY + BirdH * 0.5f >= BaseY || birdY < 0) state = StateDead;
            if (state == StateDead) { sndHit?.Play(0.8f); hitPlayed = true; }
            break;

        case 2: // Dead
            birdVel += Gravity * dt;
            birdY += birdVel * dt;
            birdRot = MathF.Min(birdRot + 4f * dt, 1.57f);
            if (birdY + BirdH * 0.5f > BaseY) { birdY = BaseY - BirdH * 0.5f; birdVel = 0; }
            if (action) { ResetGame(); sndSwoosh?.Play(0.5f); }
            break;
    }

    // --- Draw ---
    texBg?.Draw(Vector2.Zero);

    for (int i = 0; i < MaxPipes; i++)
    {
        if (!pipes[i].active) continue;
        float botTop = pipes[i].gapY + PipeGap * 0.5f;
        float topBottom = pipes[i].gapY - PipeGap * 0.5f;

        // Bottom pipe
        DriftNative.drift_sprite_draw(texPipe!.Handle,
            new DriftVec2(pipes[i].x, botTop), new DriftRect(0, 0, PipeWidth, PipeHeight),
            new DriftVec2(1, 1), 0, new DriftVec2(0, 0),
            DriftColor.White, DriftFlip.None, 5f);

        // Top pipe (flipped)
        DriftNative.drift_sprite_draw(texPipe!.Handle,
            new DriftVec2(pipes[i].x, topBottom), new DriftRect(0, 0, PipeWidth, PipeHeight),
            new DriftVec2(1, 1), 0, new DriftVec2(0, PipeHeight),
            DriftColor.White, DriftFlip.V, 5f);
    }

    // Base
    float baseX = -(baseScroll % 48f);
    for (float bx = baseX; bx < ScreenW + 48f; bx += 336f)
    {
        DriftNative.drift_sprite_draw(texBase!.Handle,
            new DriftVec2(bx, BaseY), new DriftRect(0, 0, 336, BaseH),
            new DriftVec2(1, 1), 0, new DriftVec2(0, 0),
            DriftColor.White, DriftFlip.None, 10f);
    }

    // Bird
    DriftNative.drift_sprite_draw(texBird[birdFrame]!.Handle,
        new DriftVec2(BirdX, birdY), new DriftRect(0, 0, BirdW, BirdH),
        new DriftVec2(1, 1), birdRot, new DriftVec2(BirdW * 0.5f, BirdH * 0.5f),
        DriftColor.White, DriftFlip.None, 20f);

    if (state == StatePlaying || state == StateDead) DrawScore(score);
    if (state == StateMenu) texMessage?.Draw(new Vector2((ScreenW - 184) * 0.5f, ScreenH * 0.2f), zOrder: 60f);
    if (state == StateDead) texGameover?.Draw(new Vector2((ScreenW - 192) * 0.5f, ScreenH * 0.3f), zOrder: 60f);
};

app.Run();
