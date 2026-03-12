// =============================================================================
// Flappy Bird — Drift C# SDK
// =============================================================================

using System;
using Drift;

namespace FlappyBirdGame;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static class C
{
    public const int   WindowScale       = 2;
    public const int   ScreenW           = 288;
    public const int   ScreenH           = 512;
    public const float Gravity           = 900f;
    public const float FlapVelocity      = -280f;
    public const float PipeSpeed         = 120f;
    public const float PipeSpawnInterval = 1.8f;
    public const float PipeGap           = 100f;
    public const float PipeWidth         = 52f;
    public const float PipeHeight        = 320f;
    public const float BirdW             = 34f;
    public const float BirdH             = 24f;
    public const float BaseH             = 112f;
    public const float BaseY             = ScreenH - BaseH;
    public const float BirdX             = 60f;
    public const float BirdAnimSpeed     = 0.12f;
    public const int   MaxPipes          = 8;

    public static bool Overlap(float ax, float ay, float aw, float ah,
                               float bx, float by, float bw, float bh)
        => ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

// ---------------------------------------------------------------------------
// Game state as a Resource
// ---------------------------------------------------------------------------

enum GameState { Menu, Playing, Dead }

class FlappyState : drift.Resource
{
    public override string name() => "FlappyState";

    public GameState State = GameState.Menu;
    public float BirdY = C.ScreenH * 0.4f;
    public float BirdVel;
    public float BirdRot;
    public int   BirdFrame = 1;
    public float BirdAnimTimer;
    public float BaseScroll;
    public float PipeTimer;
    public int   Score;
    public bool  HitPlayed;

    public (float x, float gapY, bool scored, bool active)[] Pipes
        = new (float, float, bool, bool)[C.MaxPipes];

    public drift.TextureHandle TexBg, TexBase, TexPipe, TexGameover, TexMessage;
    public drift.TextureHandle[] TexBird = new drift.TextureHandle[3];
    public drift.TextureHandle[] TexNum  = new drift.TextureHandle[10];

    public drift.SoundHandle SndWing, SndHit, SndPoint, SndDie, SndSwoosh;
    public Random Rng = new(42);

    public void Reset()
    {
        State = GameState.Menu;
        BirdY = C.ScreenH * 0.4f;
        BirdVel = 0; BirdRot = 0;
        BirdFrame = 1; BirdAnimTimer = 0;
        BaseScroll = 0; PipeTimer = 0;
        Score = 0; HitPlayed = false;
        for (int i = 0; i < C.MaxPipes; i++) Pipes[i].active = false;
    }

    public void SpawnPipe()
    {
        for (int i = 0; i < C.MaxPipes; i++)
        {
            if (!Pipes[i].active)
            {
                float minY = C.PipeGap * 0.5f + 50f;
                float maxY = C.BaseY - C.PipeGap * 0.5f - 20f;
                Pipes[i] = (C.ScreenW + 20f, minY + (float)Rng.NextDouble() * (maxY - minY), false, true);
                return;
            }
        }
    }

    public void Flap(drift.AudioResource audio)
    {
        BirdVel = C.FlapVelocity;
        if (SndWing.valid()) audio.playSound(SndWing, 0.6f);
    }
}

// ---------------------------------------------------------------------------
// Startup system
// ---------------------------------------------------------------------------

class StartupSystem : System<FlappyState, drift.RendererResource, drift.AudioResource>
{
    protected override void Run(FlappyState game, drift.RendererResource renderer,
                                drift.AudioResource audio, float dt)
    {
        // Configure default camera for pixel-perfect scaling
        var cam = renderer.getDefaultCamera();
        renderer.setCameraPosition(cam,
            new drift.Vec2(C.ScreenW * 0.5f, C.ScreenH * 0.5f));
        renderer.setCameraZoom(cam, C.WindowScale);

        game.TexBg       = renderer.loadTexture("assets/background.png");
        game.TexBase     = renderer.loadTexture("assets/base.png");
        game.TexPipe     = renderer.loadTexture("assets/pipe.png");
        game.TexBird[0]  = renderer.loadTexture("assets/bird-down.png");
        game.TexBird[1]  = renderer.loadTexture("assets/bird-mid.png");
        game.TexBird[2]  = renderer.loadTexture("assets/bird-up.png");
        game.TexGameover = renderer.loadTexture("assets/gameover.png");
        game.TexMessage  = renderer.loadTexture("assets/message.png");
        for (int i = 0; i < 10; i++)
            game.TexNum[i] = renderer.loadTexture($"assets/num{i}.png");

        game.SndWing   = audio.loadSound("assets/wing.wav");
        game.SndHit    = audio.loadSound("assets/hit.wav");
        game.SndPoint  = audio.loadSound("assets/point.wav");
        game.SndDie    = audio.loadSound("assets/die.wav");
        game.SndSwoosh = audio.loadSound("assets/swoosh.wav");

        game.Reset();
    }
}

// ---------------------------------------------------------------------------
// Update system
// ---------------------------------------------------------------------------

class UpdateSystem : System<drift.InputResource, FlappyState, drift.AudioResource>
{
    protected override void Run(drift.InputResource input, FlappyState game,
                                drift.AudioResource audio, float dt)
    {
        bool action = input.keyPressed(drift.Key.Space) ||
                      input.mouseButtonPressed(drift.MouseButton.Left);

        switch (game.State)
        {
            case GameState.Menu:
                game.BirdAnimTimer += dt;
                if (game.BirdAnimTimer >= C.BirdAnimSpeed)
                {
                    game.BirdAnimTimer = 0;
                    game.BirdFrame = (game.BirdFrame + 1) % 3;
                }
                game.BirdY = C.ScreenH * 0.4f + MathF.Sin(game.BirdAnimTimer * 20f) * 8f;
                game.BaseScroll += C.PipeSpeed * dt;
                if (action) { game.State = GameState.Playing; game.Flap(audio); }
                break;

            case GameState.Playing:
                game.BirdVel += C.Gravity * dt;
                game.BirdY += game.BirdVel * dt;
                game.BirdRot = game.BirdVel < 0
                    ? -25f * MathF.PI / 180f
                    : MathF.Min(game.BirdRot + 3f * dt, 1.57f);

                game.BirdAnimTimer += dt;
                if (game.BirdAnimTimer >= C.BirdAnimSpeed)
                {
                    game.BirdAnimTimer = 0;
                    game.BirdFrame = (game.BirdFrame + 1) % 3;
                }
                if (action) game.Flap(audio);
                game.BaseScroll += C.PipeSpeed * dt;

                game.PipeTimer += dt;
                if (game.PipeTimer >= C.PipeSpawnInterval) { game.PipeTimer = 0; game.SpawnPipe(); }

                for (int i = 0; i < C.MaxPipes; i++)
                {
                    if (!game.Pipes[i].active) continue;
                    game.Pipes[i].x -= C.PipeSpeed * dt;
                    if (game.Pipes[i].x < -C.PipeWidth - 10f) { game.Pipes[i].active = false; continue; }

                    if (!game.Pipes[i].scored && game.Pipes[i].x + C.PipeWidth < C.BirdX)
                    {
                        game.Pipes[i].scored = true;
                        game.Score++;
                        if (game.SndPoint.valid()) audio.playSound(game.SndPoint, 0.7f);
                    }

                    float topBottom = game.Pipes[i].gapY - C.PipeGap * 0.5f;
                    float botTop    = game.Pipes[i].gapY + C.PipeGap * 0.5f;

                    if (C.Overlap(C.BirdX - C.BirdW * 0.4f, game.BirdY - C.BirdH * 0.4f,
                                  C.BirdW * 0.8f, C.BirdH * 0.8f,
                                  game.Pipes[i].x, topBottom - C.PipeHeight, C.PipeWidth, C.PipeHeight) ||
                        C.Overlap(C.BirdX - C.BirdW * 0.4f, game.BirdY - C.BirdH * 0.4f,
                                  C.BirdW * 0.8f, C.BirdH * 0.8f,
                                  game.Pipes[i].x, botTop, C.PipeWidth, C.PipeHeight))
                    {
                        game.State = GameState.Dead;
                    }
                }

                if (game.BirdY + C.BirdH * 0.5f >= C.BaseY || game.BirdY < 0)
                    game.State = GameState.Dead;

                if (game.State == GameState.Dead)
                {
                    if (game.SndHit.valid()) audio.playSound(game.SndHit, 0.8f);
                    game.HitPlayed = true;
                }
                break;

            case GameState.Dead:
                game.BirdVel += C.Gravity * dt;
                game.BirdY += game.BirdVel * dt;
                game.BirdRot = MathF.Min(game.BirdRot + 4f * dt, 1.57f);
                if (game.BirdY + C.BirdH * 0.5f > C.BaseY)
                {
                    game.BirdY = C.BaseY - C.BirdH * 0.5f;
                    game.BirdVel = 0;
                }
                if (action)
                {
                    game.Reset();
                    if (game.SndSwoosh.valid()) audio.playSound(game.SndSwoosh, 0.5f);
                }
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// Render system
// ---------------------------------------------------------------------------

class RenderSystem : System<FlappyState, drift.RendererResource>
{
    protected override void Run(FlappyState game, drift.RendererResource renderer, float dt)
    {
        var white = new drift.Color(255, 255, 255, 255);
        var one   = new drift.Vec2(1, 1);
        var zero  = new drift.Vec2(0, 0);

        renderer.drawSprite(game.TexBg, zero,
            new drift.Rect(0, 0, C.ScreenW, C.ScreenH), 0f);

        for (int i = 0; i < C.MaxPipes; i++)
        {
            if (!game.Pipes[i].active) continue;
            float botTop    = game.Pipes[i].gapY + C.PipeGap * 0.5f;
            float topBottom = game.Pipes[i].gapY - C.PipeGap * 0.5f;

            renderer.drawSprite(game.TexPipe,
                new drift.Vec2(game.Pipes[i].x, botTop),
                new drift.Rect(0, 0, C.PipeWidth, C.PipeHeight), 5f);

            renderer.drawSprite(game.TexPipe,
                new drift.Vec2(game.Pipes[i].x, topBottom),
                new drift.Rect(0, 0, C.PipeWidth, C.PipeHeight),
                one, 0, new drift.Vec2(0, C.PipeHeight), white, drift.Flip.V, 5f);
        }

        float baseX = -(game.BaseScroll % 48f);
        for (float bx = baseX; bx < C.ScreenW + 48f; bx += 336f)
        {
            renderer.drawSprite(game.TexBase,
                new drift.Vec2(bx, C.BaseY),
                new drift.Rect(0, 0, 336, C.BaseH), 10f);
        }

        renderer.drawSprite(game.TexBird[game.BirdFrame],
            new drift.Vec2(C.BirdX, game.BirdY),
            new drift.Rect(0, 0, C.BirdW, C.BirdH),
            one, game.BirdRot,
            new drift.Vec2(C.BirdW * 0.5f, C.BirdH * 0.5f),
            white, drift.Flip.None, 20f);

        if (game.State == GameState.Playing || game.State == GameState.Dead)
            DrawScore(renderer, game, white, one, zero);

        if (game.State == GameState.Menu)
        {
            renderer.drawSprite(game.TexMessage,
                new drift.Vec2((C.ScreenW - 184) * 0.5f, C.ScreenH * 0.2f),
                new drift.Rect(0, 0, 184, 267), 60f);
        }

        if (game.State == GameState.Dead)
        {
            renderer.drawSprite(game.TexGameover,
                new drift.Vec2((C.ScreenW - 192) * 0.5f, C.ScreenH * 0.3f),
                new drift.Rect(0, 0, 192, 42), 60f);
        }
    }

    static void DrawScore(drift.RendererResource renderer, FlappyState game,
                          drift.Color white, drift.Vec2 one, drift.Vec2 zero)
    {
        string digits = game.Score.ToString();
        float totalW = digits.Length * 26f;
        float startX = (C.ScreenW - totalW) * 0.5f;

        for (int i = 0; i < digits.Length; i++)
        {
            int d = digits[i] - '0';
            if (d < 0 || d > 9) continue;
            var tex = game.TexNum[d];
            if (!tex.valid()) continue;

            renderer.drawSprite(tex,
                new drift.Vec2(startX + i * 26f, 40f),
                new drift.Rect(0, 0, 24, 36), 50f);
        }
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

class Program
{
    static int Main(string[] args)
    {
        var config = new drift.Config();
        config.title = "Flappy Bird - Drift C#";
        config.width = C.ScreenW * C.WindowScale;
        config.height = C.ScreenH * C.WindowScale;
        config.resizable = false;

        var app = new drift.App();
        app.setConfig(config);
        app.addPlugins(new drift.DefaultPlugins());
        app.AddResource(new FlappyState());
        app.Startup<StartupSystem>();
        app.Update<UpdateSystem>();
        app.Render<RenderSystem>();
        return app.run();
    }
}
