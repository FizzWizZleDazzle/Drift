// =============================================================================
// Flappy Bird — Drift C# SDK
// =============================================================================
// All rendering via entities. Zero manual drawSprite. Uses AssetServer +
// Commands for spawning/syncing entities.
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
    public const int   MaxScoreDigits    = 6;

    public static bool Overlap(float ax, float ay, float aw, float ah,
                               float bx, float by, float bw, float bh)
        => ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

// ---------------------------------------------------------------------------
// Game state as a Resource
// ---------------------------------------------------------------------------

enum GameState { Menu, Playing, Dead }

class PipeData
{
    public float x, gapY;
    public bool scored, active;
    public ulong topEntity, botEntity;
}

class FlappyState : drift.Resource
{
    public override string name() => "FlappyState";

    public GameState State = GameState.Menu;
    public float BirdY = C.ScreenH * 0.4f;
    public float BirdVel, BirdRot;
    public int   BirdFrame = 1;
    public float BirdAnimTimer, BaseScroll, PipeTimer;
    public int   Score;
    public bool  HitPlayed;
    public PipeData[] Pipes = new PipeData[C.MaxPipes];

    // Entities
    public ulong Camera, BgEntity, BirdEntity;
    public ulong[] BaseEntities = new ulong[2];
    public ulong[] ScoreDigits = new ulong[C.MaxScoreDigits];
    public ulong MenuEntity, GameoverEntity;

    // Textures
    public drift.TextureHandle TexBg, TexBase, TexPipe, TexGameover, TexMessage;
    public drift.TextureHandle[] TexBird = new drift.TextureHandle[3];
    public drift.TextureHandle[] TexNum  = new drift.TextureHandle[10];

    // Sounds
    public drift.SoundHandle SndWing, SndHit, SndPoint, SndDie, SndSwoosh;

    public Random Rng = new(42);

    public FlappyState()
    {
        for (int i = 0; i < C.MaxPipes; i++) Pipes[i] = new PipeData();
    }

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

    public void SpawnPipe(drift.Commands cmd)
    {
        for (int i = 0; i < C.MaxPipes; i++)
        {
            if (!Pipes[i].active)
            {
                float minY = C.PipeGap * 0.5f + 50f;
                float maxY = C.BaseY - C.PipeGap * 0.5f - 20f;
                Pipes[i].active = true;
                Pipes[i].x = C.ScreenW + 20f;
                Pipes[i].gapY = minY + (float)Rng.NextDouble() * (maxY - minY);
                Pipes[i].scored = false;
                if (Pipes[i].botEntity == 0)
                {
                    Pipes[i].botEntity = cmd.spawnSprite(TexPipe, new drift.Vec2(0, 0), 5f);
                    Pipes[i].topEntity = cmd.spawnSprite(TexPipe, new drift.Vec2(0, 0), 5f);
                }
                return;
            }
        }
    }

    public void Flap(drift.AssetServer assets)
    {
        BirdVel = C.FlapVelocity;
        if (SndWing.valid()) assets.playSound(SndWing, 0.6f);
    }
}

// ---------------------------------------------------------------------------
// Startup system
// ---------------------------------------------------------------------------

class StartupSystem : System<FlappyState, drift.AssetServer>
{
    protected override void Run(FlappyState game, drift.AssetServer assets, float dt)
    {
        // NOTE: Commands not yet available via C# system params, use App
        // For startup, we just load assets. Entity spawning done in a lambda.
    }
}

// We use a lambda system for startup since we need Commands
// (C# System<> doesn't inject Commands yet)

// ---------------------------------------------------------------------------
// Update system (lambda — needs Commands)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

class Program
{
    static void SyncEntities(FlappyState game, drift.Commands cmd)
    {
        // Background
        {
            var s = new drift.Sprite();
            s.texture = game.TexBg;
            s.srcRect = new drift.Rect(0, 0, C.ScreenW, C.ScreenH);
            s.zOrder = 0f;
            cmd.setSprite(game.BgEntity, s);
        }

        // Bird
        {
            var t = new drift.Transform2D();
            t.position = new drift.Vec2(C.BirdX, game.BirdY);
            t.rotation = game.BirdRot;
            cmd.setTransform(game.BirdEntity, t);

            var s = new drift.Sprite();
            s.texture = game.TexBird[game.BirdFrame];
            s.srcRect = new drift.Rect(0, 0, C.BirdW, C.BirdH);
            s.origin = new drift.Vec2(C.BirdW * 0.5f, C.BirdH * 0.5f);
            s.zOrder = 20f;
            cmd.setSprite(game.BirdEntity, s);
        }

        // Pipes
        for (int i = 0; i < C.MaxPipes; i++)
        {
            var p = game.Pipes[i];
            if (p.botEntity == 0) continue;

            if (!p.active)
            {
                var hidden = new drift.Sprite(); hidden.visible = false;
                cmd.setSprite(p.botEntity, hidden);
                cmd.setSprite(p.topEntity, hidden);
                continue;
            }

            float botTop = p.gapY + C.PipeGap * 0.5f;
            float topBottom = p.gapY - C.PipeGap * 0.5f;

            // Bottom
            {
                var t = new drift.Transform2D();
                t.position = new drift.Vec2(p.x, botTop);
                cmd.setTransform(p.botEntity, t);
                var s = new drift.Sprite();
                s.texture = game.TexPipe;
                s.srcRect = new drift.Rect(0, 0, C.PipeWidth, C.PipeHeight);
                s.zOrder = 5f;
                cmd.setSprite(p.botEntity, s);
            }
            // Top
            {
                var t = new drift.Transform2D();
                t.position = new drift.Vec2(p.x, topBottom);
                cmd.setTransform(p.topEntity, t);
                var s = new drift.Sprite();
                s.texture = game.TexPipe;
                s.srcRect = new drift.Rect(0, 0, C.PipeWidth, C.PipeHeight);
                s.origin = new drift.Vec2(0, C.PipeHeight);
                s.flip = drift.Flip.V;
                s.zOrder = 5f;
                cmd.setSprite(p.topEntity, s);
            }
        }

        // Base
        {
            float baseX = -(game.BaseScroll % 48f);
            for (int i = 0; i < 2; i++)
            {
                var t = new drift.Transform2D();
                t.position = new drift.Vec2(baseX + i * 336f, C.BaseY);
                cmd.setTransform(game.BaseEntities[i], t);
                var s = new drift.Sprite();
                s.texture = game.TexBase;
                s.srcRect = new drift.Rect(0, 0, 336, C.BaseH);
                s.zOrder = 10f;
                cmd.setSprite(game.BaseEntities[i], s);
            }
        }

        // Score
        {
            bool show = game.State == GameState.Playing || game.State == GameState.Dead;
            string digits = show ? game.Score.ToString() : "";
            float totalW = digits.Length * 26f;
            float startX = (C.ScreenW - totalW) * 0.5f;

            for (int i = 0; i < C.MaxScoreDigits; i++)
            {
                if (i < digits.Length)
                {
                    int d = digits[i] - '0';
                    var t = new drift.Transform2D();
                    t.position = new drift.Vec2(startX + i * 26f, 40f);
                    cmd.setTransform(game.ScoreDigits[i], t);
                    var s = new drift.Sprite();
                    s.texture = game.TexNum[d];
                    s.srcRect = new drift.Rect(0, 0, 24, 36);
                    s.zOrder = 50f; s.visible = true;
                    cmd.setSprite(game.ScoreDigits[i], s);
                }
                else
                {
                    var s = new drift.Sprite(); s.visible = false;
                    cmd.setSprite(game.ScoreDigits[i], s);
                }
            }
        }

        // Menu
        {
            var s = new drift.Sprite();
            s.texture = game.TexMessage;
            s.srcRect = new drift.Rect(0, 0, 184, 267);
            s.zOrder = 60f; s.visible = game.State == GameState.Menu;
            cmd.setSprite(game.MenuEntity, s);
            var t = new drift.Transform2D();
            t.position = new drift.Vec2((C.ScreenW - 184) * 0.5f, C.ScreenH * 0.2f);
            cmd.setTransform(game.MenuEntity, t);
        }

        // Game over
        {
            var s = new drift.Sprite();
            s.texture = game.TexGameover;
            s.srcRect = new drift.Rect(0, 0, 192, 42);
            s.zOrder = 60f; s.visible = game.State == GameState.Dead;
            cmd.setSprite(game.GameoverEntity, s);
            var t = new drift.Transform2D();
            t.position = new drift.Vec2((C.ScreenW - 192) * 0.5f, C.ScreenH * 0.3f);
            cmd.setTransform(game.GameoverEntity, t);
        }
    }

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

        var gameState = new FlappyState();
        app.AddResource(gameState);

        // Startup: load assets + spawn entities
        app.AddSystem("startup", drift.Phase.Startup, (a, dt) =>
        {
            var assets = a.getAssetServer();
            var cmd = a.getCommands();
            var game = gameState;

            game.TexBg       = assets.loadTexture("assets/background.png");
            game.TexBase     = assets.loadTexture("assets/base.png");
            game.TexPipe     = assets.loadTexture("assets/pipe.png");
            game.TexBird[0]  = assets.loadTexture("assets/bird-down.png");
            game.TexBird[1]  = assets.loadTexture("assets/bird-mid.png");
            game.TexBird[2]  = assets.loadTexture("assets/bird-up.png");
            game.TexGameover = assets.loadTexture("assets/gameover.png");
            game.TexMessage  = assets.loadTexture("assets/message.png");
            for (int i = 0; i < 10; i++)
                game.TexNum[i] = assets.loadTexture($"assets/num{i}.png");

            game.SndWing   = assets.loadSound("assets/wing.wav");
            game.SndHit    = assets.loadSound("assets/hit.wav");
            game.SndPoint  = assets.loadSound("assets/point.wav");
            game.SndDie    = assets.loadSound("assets/die.wav");
            game.SndSwoosh = assets.loadSound("assets/swoosh.wav");

            game.Camera = cmd.spawnCamera(
                new drift.Vec2(C.ScreenW * 0.5f, C.ScreenH * 0.5f),
                C.WindowScale, true);
            game.BgEntity = cmd.spawnSprite(game.TexBg, new drift.Vec2(0, 0), 0f);
            game.BirdEntity = cmd.spawnSprite(game.TexBird[1],
                new drift.Vec2(C.BirdX, C.ScreenH * 0.4f), 20f);
            game.BaseEntities[0] = cmd.spawnSprite(game.TexBase, new drift.Vec2(0, C.BaseY), 10f);
            game.BaseEntities[1] = cmd.spawnSprite(game.TexBase, new drift.Vec2(336, C.BaseY), 10f);

            for (int i = 0; i < C.MaxScoreDigits; i++)
            {
                game.ScoreDigits[i] = cmd.spawnSprite(game.TexNum[0], new drift.Vec2(0, 0), 50f);
                var s = new drift.Sprite(); s.visible = false;
                cmd.setSprite(game.ScoreDigits[i], s);
            }

            game.MenuEntity = cmd.spawnSprite(game.TexMessage, new drift.Vec2(0, 0), 60f);
            game.GameoverEntity = cmd.spawnSprite(game.TexGameover, new drift.Vec2(0, 0), 60f);
            { var s = new drift.Sprite(); s.visible = false; cmd.setSprite(game.GameoverEntity, s); }

            game.Reset();
        });

        // Update: game logic + entity sync
        app.AddSystem("update", drift.Phase.Update, (a, dt) =>
        {
            var input = a.getInputResource();
            var assets = a.getAssetServer();
            var cmd = a.getCommands();
            var game = gameState;

            bool action = input.keyPressed(drift.Key.Space) ||
                          input.mouseButtonPressed(drift.MouseButton.Left);

            switch (game.State)
            {
                case GameState.Menu:
                    game.BirdAnimTimer += dt;
                    if (game.BirdAnimTimer >= C.BirdAnimSpeed)
                    { game.BirdAnimTimer = 0; game.BirdFrame = (game.BirdFrame + 1) % 3; }
                    game.BirdY = C.ScreenH * 0.4f + MathF.Sin(game.BirdAnimTimer * 20f) * 8f;
                    game.BaseScroll += C.PipeSpeed * dt;
                    if (action) { game.State = GameState.Playing; game.Flap(assets); }
                    break;

                case GameState.Playing:
                    game.BirdVel += C.Gravity * dt;
                    game.BirdY += game.BirdVel * dt;
                    game.BirdRot = game.BirdVel < 0
                        ? -25f * MathF.PI / 180f
                        : MathF.Min(game.BirdRot + 3f * dt, 1.57f);
                    game.BirdAnimTimer += dt;
                    if (game.BirdAnimTimer >= C.BirdAnimSpeed)
                    { game.BirdAnimTimer = 0; game.BirdFrame = (game.BirdFrame + 1) % 3; }
                    if (action) game.Flap(assets);
                    game.BaseScroll += C.PipeSpeed * dt;
                    game.PipeTimer += dt;
                    if (game.PipeTimer >= C.PipeSpawnInterval)
                    { game.PipeTimer = 0; game.SpawnPipe(cmd); }

                    for (int i = 0; i < C.MaxPipes; i++)
                    {
                        var p = game.Pipes[i];
                        if (!p.active) continue;
                        p.x -= C.PipeSpeed * dt;
                        if (p.x < -C.PipeWidth - 10f) { p.active = false; continue; }
                        if (!p.scored && p.x + C.PipeWidth < C.BirdX)
                        { p.scored = true; game.Score++; if (game.SndPoint.valid()) assets.playSound(game.SndPoint, 0.7f); }
                        float topB = p.gapY - C.PipeGap * 0.5f;
                        float botT = p.gapY + C.PipeGap * 0.5f;
                        if (C.Overlap(C.BirdX - C.BirdW * 0.4f, game.BirdY - C.BirdH * 0.4f,
                                      C.BirdW * 0.8f, C.BirdH * 0.8f,
                                      p.x, topB - C.PipeHeight, C.PipeWidth, C.PipeHeight) ||
                            C.Overlap(C.BirdX - C.BirdW * 0.4f, game.BirdY - C.BirdH * 0.4f,
                                      C.BirdW * 0.8f, C.BirdH * 0.8f,
                                      p.x, botT, C.PipeWidth, C.PipeHeight))
                            game.State = GameState.Dead;
                    }
                    if (game.BirdY + C.BirdH * 0.5f >= C.BaseY || game.BirdY < 0)
                        game.State = GameState.Dead;
                    if (game.State == GameState.Dead)
                    { if (game.SndHit.valid()) assets.playSound(game.SndHit, 0.8f); game.HitPlayed = true; }
                    break;

                case GameState.Dead:
                    game.BirdVel += C.Gravity * dt;
                    game.BirdY += game.BirdVel * dt;
                    game.BirdRot = MathF.Min(game.BirdRot + 4f * dt, 1.57f);
                    if (game.BirdY + C.BirdH * 0.5f > C.BaseY)
                    { game.BirdY = C.BaseY - C.BirdH * 0.5f; game.BirdVel = 0; }
                    if (action) { game.Reset(); if (game.SndSwoosh.valid()) assets.playSound(game.SndSwoosh, 0.5f); }
                    break;
            }

            SyncEntities(game, cmd);
        });

        return app.run();
    }
}
