// =============================================================================
// Flappy Bird — Drift C# SDK
// =============================================================================
// Game visuals via entities, UI via ImGui (UIResource).
// Uses AssetServer + Commands for spawning/syncing entities.
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
    public const int   WindowW           = ScreenW * WindowScale;
    public const int   WindowH           = ScreenH * WindowScale;

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

    // Textures
    public drift.TextureHandle TexBg, TexBase, TexPipe;
    public drift.TextureHandle[] TexBird = new drift.TextureHandle[3];

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
                    Pipes[i].botEntity = cmd.spawn()
                        .insert(new drift.Transform2D { position = new drift.Vec2(0, 0) })
                        .insert(new drift.Sprite { texture = TexPipe, zOrder = 5f })
                        .id();
                    Pipes[i].topEntity = cmd.spawn()
                        .insert(new drift.Transform2D { position = new drift.Vec2(0, 0) })
                        .insert(new drift.Sprite { texture = TexPipe, zOrder = 5f })
                        .id();
                }
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
            cmd.entity(game.BgEntity).insert(s);
        }

        // Bird
        {
            var t = new drift.Transform2D();
            t.position = new drift.Vec2(C.BirdX, game.BirdY);
            t.rotation = game.BirdRot;
            var s = new drift.Sprite();
            s.texture = game.TexBird[game.BirdFrame];
            s.srcRect = new drift.Rect(0, 0, C.BirdW, C.BirdH);
            s.origin = new drift.Vec2(C.BirdW * 0.5f, C.BirdH * 0.5f);
            s.zOrder = 20f;
            cmd.entity(game.BirdEntity).insert(t).insert(s);
        }

        // Pipes
        for (int i = 0; i < C.MaxPipes; i++)
        {
            var p = game.Pipes[i];
            if (p.botEntity == 0) continue;

            if (!p.active)
            {
                var hidden = new drift.Sprite(); hidden.visible = false;
                cmd.entity(p.botEntity).insert(hidden);
                cmd.entity(p.topEntity).insert(hidden);
                continue;
            }

            float botTop = p.gapY + C.PipeGap * 0.5f;
            float topBottom = p.gapY - C.PipeGap * 0.5f;

            // Bottom
            {
                var t = new drift.Transform2D();
                t.position = new drift.Vec2(p.x, botTop);
                var s = new drift.Sprite();
                s.texture = game.TexPipe;
                s.srcRect = new drift.Rect(0, 0, C.PipeWidth, C.PipeHeight);
                s.zOrder = 5f;
                cmd.entity(p.botEntity).insert(t).insert(s);
            }
            // Top
            {
                var t = new drift.Transform2D();
                t.position = new drift.Vec2(p.x, topBottom);
                var s = new drift.Sprite();
                s.texture = game.TexPipe;
                s.srcRect = new drift.Rect(0, 0, C.PipeWidth, C.PipeHeight);
                s.origin = new drift.Vec2(0, C.PipeHeight);
                s.flip = drift.Flip.V;
                s.zOrder = 5f;
                cmd.entity(p.topEntity).insert(t).insert(s);
            }
        }

        // Base
        {
            float baseX = -(game.BaseScroll % 48f);
            for (int i = 0; i < 2; i++)
            {
                var t = new drift.Transform2D();
                t.position = new drift.Vec2(baseX + i * 336f, C.BaseY);
                var s = new drift.Sprite();
                s.texture = game.TexBase;
                s.srcRect = new drift.Rect(0, 0, 336, C.BaseH);
                s.zOrder = 10f;
                cmd.entity(game.BaseEntities[i]).insert(t).insert(s);
            }
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

            game.SndWing   = assets.loadSound("assets/wing.wav");
            game.SndHit    = assets.loadSound("assets/hit.wav");
            game.SndPoint  = assets.loadSound("assets/point.wav");
            game.SndDie    = assets.loadSound("assets/die.wav");
            game.SndSwoosh = assets.loadSound("assets/swoosh.wav");

            game.Camera = cmd.spawn()
                .insert(new drift.Transform2D { position = new drift.Vec2(C.ScreenW * 0.5f, C.ScreenH * 0.5f) })
                .insert(new drift.Camera { zoom = C.WindowScale, active = true })
                .id();
            game.BgEntity = cmd.spawn()
                .insert(new drift.Transform2D { position = new drift.Vec2(0, 0) })
                .insert(new drift.Sprite { texture = game.TexBg, zOrder = 0f })
                .id();
            game.BirdEntity = cmd.spawn()
                .insert(new drift.Transform2D { position = new drift.Vec2(C.BirdX, C.ScreenH * 0.4f) })
                .insert(new drift.Sprite { texture = game.TexBird[1], zOrder = 20f })
                .id();
            game.BaseEntities[0] = cmd.spawn()
                .insert(new drift.Transform2D { position = new drift.Vec2(0, C.BaseY) })
                .insert(new drift.Sprite { texture = game.TexBase, zOrder = 10f })
                .id();
            game.BaseEntities[1] = cmd.spawn()
                .insert(new drift.Transform2D { position = new drift.Vec2(336, C.BaseY) })
                .insert(new drift.Sprite { texture = game.TexBase, zOrder = 10f })
                .id();

            game.Reset();
        });

        // Update: game logic + entity sync
        app.AddSystem("update", drift.Phase.Update, (a, dt) =>
        {
            var input = a.getInputResource();
            var audio = a.getAudioResource();
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
                    { game.BirdAnimTimer = 0; game.BirdFrame = (game.BirdFrame + 1) % 3; }
                    if (action) game.Flap(audio);
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
                        { p.scored = true; game.Score++; if (game.SndPoint.valid()) audio.playSound(game.SndPoint, 0.7f); }
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
                    { if (game.SndHit.valid()) audio.playSound(game.SndHit, 0.8f); game.HitPlayed = true; }
                    break;

                case GameState.Dead:
                    game.BirdVel += C.Gravity * dt;
                    game.BirdY += game.BirdVel * dt;
                    game.BirdRot = MathF.Min(game.BirdRot + 4f * dt, 1.57f);
                    if (game.BirdY + C.BirdH * 0.5f > C.BaseY)
                    { game.BirdY = C.BaseY - C.BirdH * 0.5f; game.BirdVel = 0; }
                    if (action) { game.Reset(); if (game.SndSwoosh.valid()) audio.playSound(game.SndSwoosh, 0.5f); }
                    break;
            }

            SyncEntities(game, cmd);

            // ImGui UI overlay via UIResource
            var ui = a.getUIResource();
            if (ui != null)
            {
                if (game.State == GameState.Playing || game.State == GameState.Dead)
                {
                    float scoreW = game.Score.ToString().Length * 20f;
                    ui.label(game.Score.ToString(),
                             new drift.Vec2((C.WindowW - scoreW) * 0.5f, C.WindowH * 0.05f));
                }

                if (game.State == GameState.Menu)
                {
                    ui.label("Flappy Bird",
                             new drift.Vec2(C.WindowW * 0.5f - 80, C.WindowH * 0.2f));
                    ui.label("Press Space or Click to Start",
                             new drift.Vec2(C.WindowW * 0.5f - 140, C.WindowH * 0.45f));
                }

                if (game.State == GameState.Dead)
                {
                    ui.label("GAME OVER",
                             new drift.Vec2(C.WindowW * 0.5f - 60, C.WindowH * 0.3f));
                    ui.label("Press Space or Click to Restart",
                             new drift.Vec2(C.WindowW * 0.5f - 150, C.WindowH * 0.42f));
                }
            }
        });

        return app.run();
    }
}
