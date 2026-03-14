// =============================================================================
// Flappy Bird — Drift C# SDK (Unity-style Component API)
// =============================================================================

global using static Drift.Prelude;
global using Key = drift.Key;
global using BodyType = drift.BodyType;
global using Flip = drift.Flip;

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
}

// ---------------------------------------------------------------------------
// Game state
// ---------------------------------------------------------------------------

enum GameState { Menu, Playing, Dead }

class PipeData
{
    public float x, gapY;
    public bool scored, active;
    public Entity? top, bot;
}

// ---------------------------------------------------------------------------
// Bird component — uses sensor collider for collision detection
// ---------------------------------------------------------------------------

class Bird : Component
{
    public float VelY;
    public float Rot;
    public int Frame = 1;
    public float AnimTimer;
    public drift.TextureHandle[] Textures = new drift.TextureHandle[3];

    protected internal override void OnUpdate(float dt)
    {
        var gc = Entity.Find<GameController>();
        if (gc == null) return;
        var sr = Entity.Get<SpriteRenderer>();

        switch (gc.State.Current)
        {
            case GameState.Menu:
                AnimTimer += dt;
                if (AnimTimer >= C.BirdAnimSpeed)
                { AnimTimer = 0; Frame = (Frame + 1) % 3; }
                Entity.Position = Vec2(C.BirdX,
                    C.ScreenH * 0.4f + MathF.Sin(AnimTimer * 20f) * 8f);
                sr.Texture = Textures[Frame];
                Entity.Rotation = 0f;
                Rot = 0f;
                break;

            case GameState.Playing:
                VelY += C.Gravity * dt;
                var pos = Entity.Position;
                pos.y += VelY * dt;
                Entity.Position = pos;

                Rot = VelY < 0
                    ? -25f * MathF.PI / 180f
                    : MathF.Min(Rot + 3f * dt, 1.57f);

                AnimTimer += dt;
                if (AnimTimer >= C.BirdAnimSpeed)
                { AnimTimer = 0; Frame = (Frame + 1) % 3; }

                sr.Texture = Textures[Frame];
                Entity.Rotation = Rot;
                break;

            case GameState.Dead:
                VelY += C.Gravity * dt;
                var p = Entity.Position;
                p.y += VelY * dt;
                Rot = MathF.Min(Rot + 4f * dt, 1.57f);
                if (p.y + C.BirdH * 0.5f > C.BaseY)
                { p.y = C.BaseY - C.BirdH * 0.5f; VelY = 0; }
                Entity.Position = p;
                Entity.Rotation = Rot;
                break;
        }

        sr.SrcRect = Rect(0, 0, C.BirdW, C.BirdH);
        sr.Origin = Vec2(C.BirdW * 0.5f, C.BirdH * 0.5f);
    }

    // Collision detection via the physics sensor system
    protected internal override void OnSensorEnter(Entity other)
    {
        var gc = Entity.Find<GameController>();
        if (gc == null) return;

        if (gc.State.Current == GameState.Playing)
            gc.Die();
    }

    public void Flap()
    {
        VelY = C.FlapVelocity;
    }

    public void Reset()
    {
        VelY = 0; Rot = 0; Frame = 1; AnimTimer = 0;
        Entity.Position = Vec2(C.BirdX, C.ScreenH * 0.4f);
        Entity.Rotation = 0f;
    }
}

// ---------------------------------------------------------------------------
// Game controller component
// ---------------------------------------------------------------------------

class GameController : Component
{
    public StateMachine<GameState> State = new(GameState.Menu);

    // Assets
    public drift.TextureHandle TexBg, TexBase, TexPipe;
    public drift.SoundHandle SndWing, SndHit, SndPoint, SndDie, SndSwoosh;

    // Entities
    public Entity? BgEntity;
    public Entity? BirdEntity;
    public Entity?[] BaseEntities = new Entity?[2];
    public PipeData[] Pipes = new PipeData[C.MaxPipes];

    public float BaseScroll, PipeTimer;
    public int Score;
    public Random Rng = new(42);

    protected internal override void OnAttach()
    {
        for (int i = 0; i < C.MaxPipes; i++)
            Pipes[i] = new PipeData();
    }

    protected internal override void OnUpdate(float dt)
    {
        var input = Input;
        var audio = Audio;

        bool action = input.keyPressed(Key.Space) ||
                      input.mouseButtonPressed(drift.MouseButton.Left);

        switch (State.Current)
        {
            case GameState.Menu:
                BaseScroll += C.PipeSpeed * dt;
                if (action)
                {
                    State.Set(GameState.Playing);
                    BirdEntity!.Get<Bird>().Flap();
                    if (SndWing.valid()) audio.playSound(SndWing, 0.6f);
                }
                break;

            case GameState.Playing:
                if (action)
                {
                    BirdEntity!.Get<Bird>().Flap();
                    if (SndWing.valid()) audio.playSound(SndWing, 0.6f);
                }
                BaseScroll += C.PipeSpeed * dt;
                PipeTimer += dt;
                if (PipeTimer >= C.PipeSpawnInterval)
                { PipeTimer = 0; SpawnPipe(); }

                // Move pipes and check scoring
                for (int i = 0; i < C.MaxPipes; i++)
                {
                    var p = Pipes[i];
                    if (!p.active) continue;
                    p.x -= C.PipeSpeed * dt;
                    if (p.x < -C.PipeWidth - 10f) { p.active = false; continue; }
                    if (!p.scored && p.x + C.PipeWidth < C.BirdX)
                    {
                        p.scored = true; Score++;
                        if (SndPoint.valid()) audio.playSound(SndPoint, 0.7f);
                    }
                }
                break;

            case GameState.Dead:
                if (action) Reset();
                break;
        }

        SyncEntities();
        DrawUI();
    }

    public void Die()
    {
        if (State.Current != GameState.Playing) return;
        State.Set(GameState.Dead);
        if (SndHit.valid()) Audio.playSound(SndHit, 0.8f);
    }

    void SpawnPipe()
    {
        for (int i = 0; i < C.MaxPipes; i++)
        {
            if (Pipes[i].active) continue;
            var p = Pipes[i];
            float minY = C.PipeGap * 0.5f + 50f;
            float maxY = C.BaseY - C.PipeGap * 0.5f - 20f;
            p.active = true;
            p.x = C.ScreenW + 20f;
            p.gapY = minY + (float)Rng.NextDouble() * (maxY - minY);
            p.scored = false;

            if (p.bot == null)
            {
                p.bot = Entity.Spawn("PipeBot")
                    .With<SpriteRenderer>(s => { s.Texture = TexPipe; s.ZOrder = 5f; })
                    .With<BoxCollider>(c => c.Size = Vec2(C.PipeWidth, C.PipeHeight));

                p.top = Entity.Spawn("PipeTop")
                    .With<SpriteRenderer>(s => { s.Texture = TexPipe; s.ZOrder = 5f; s.Flip = Flip.V; })
                    .With<BoxCollider>(c => c.Size = Vec2(C.PipeWidth, C.PipeHeight));
            }
            return;
        }
    }

    void Reset()
    {
        State.Set(GameState.Menu);
        BirdEntity!.Get<Bird>().Reset();
        BaseScroll = 0; PipeTimer = 0; Score = 0;
        for (int i = 0; i < C.MaxPipes; i++) Pipes[i].active = false;
        if (SndSwoosh.valid()) Audio.playSound(SndSwoosh, 0.5f);
    }

    void SyncEntities()
    {
        // Background
        if (BgEntity != null)
        {
            var sr = BgEntity.Get<SpriteRenderer>();
            sr.SrcRect = Rect(0, 0, C.ScreenW, C.ScreenH);
        }

        // Pipes — update positions, hide inactive
        for (int i = 0; i < C.MaxPipes; i++)
        {
            var p = Pipes[i];
            if (p.bot == null) continue;

            if (!p.active)
            {
                // Move off-screen so colliders don't interfere
                p.bot.Position = Vec2(-9999, -9999);
                p.top!.Position = Vec2(-9999, -9999);
                p.bot.Get<SpriteRenderer>().Visible = false;
                p.top.Get<SpriteRenderer>().Visible = false;
                continue;
            }

            float botTop = p.gapY + C.PipeGap * 0.5f;
            float topBottom = p.gapY - C.PipeGap * 0.5f;

            // Bottom pipe: origin at top-left, extends downward
            p.bot.Position = Vec2(p.x + C.PipeWidth * 0.5f, botTop + C.PipeHeight * 0.5f);
            var bsr = p.bot.Get<SpriteRenderer>();
            bsr.SrcRect = Rect(0, 0, C.PipeWidth, C.PipeHeight);
            bsr.Visible = true;

            // Top pipe: flipped, extends upward
            p.top!.Position = Vec2(p.x + C.PipeWidth * 0.5f, topBottom - C.PipeHeight * 0.5f);
            var tsr = p.top.Get<SpriteRenderer>();
            tsr.SrcRect = Rect(0, 0, C.PipeWidth, C.PipeHeight);
            tsr.Origin = Vec2(0, C.PipeHeight);
            tsr.Visible = true;
        }

        // Base
        float baseX = -(BaseScroll % 48f);
        for (int i = 0; i < 2; i++)
        {
            if (BaseEntities[i] == null) continue;
            BaseEntities[i]!.Position = Vec2(baseX + i * 336f, C.BaseY);
            var sr = BaseEntities[i]!.Get<SpriteRenderer>();
            sr.SrcRect = Rect(0, 0, 336, C.BaseH);
        }
    }

    void DrawUI()
    {
        var ui = UI;
        if (ui == null) return;

        if (State.Current == GameState.Playing || State.Current == GameState.Dead)
        {
            float scoreW = Score.ToString().Length * 20f;
            ui.label(Score.ToString(),
                     Vec2((C.WindowW - scoreW) * 0.5f, C.WindowH * 0.05f));
        }

        if (State.Current == GameState.Menu)
        {
            ui.label("Flappy Bird",
                     Vec2(C.WindowW * 0.5f - 80, C.WindowH * 0.2f));
            ui.label("Press Space or Click to Start",
                     Vec2(C.WindowW * 0.5f - 140, C.WindowH * 0.45f));
        }

        if (State.Current == GameState.Dead)
        {
            ui.label("GAME OVER",
                     Vec2(C.WindowW * 0.5f - 60, C.WindowH * 0.3f));
            ui.label("Press Space or Click to Restart",
                     Vec2(C.WindowW * 0.5f - 150, C.WindowH * 0.42f));
        }
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

class FlappyGame : DriftApp
{
    public override string Title => "Flappy Bird - Drift C#";
    public override int Width => C.WindowW;
    public override int Height => C.WindowH;

    protected override void OnStartup()
    {
        // Game controller (invisible entity that manages game logic)
        var gc = Entity.Spawn("GameController");
        var ctrl = gc.Add<GameController>();

        ctrl.TexBg     = Assets.loadTexture("assets/background.png");
        ctrl.TexBase   = Assets.loadTexture("assets/base.png");
        ctrl.TexPipe   = Assets.loadTexture("assets/pipe.png");
        ctrl.SndWing   = Assets.loadSound("assets/wing.wav");
        ctrl.SndHit    = Assets.loadSound("assets/hit.wav");
        ctrl.SndPoint  = Assets.loadSound("assets/point.wav");
        ctrl.SndDie    = Assets.loadSound("assets/die.wav");
        ctrl.SndSwoosh = Assets.loadSound("assets/swoosh.wav");

        var texBird = new drift.TextureHandle[3];
        texBird[0] = Assets.loadTexture("assets/bird-down.png");
        texBird[1] = Assets.loadTexture("assets/bird-mid.png");
        texBird[2] = Assets.loadTexture("assets/bird-up.png");

        // Camera
        Entity.Spawn("Camera", Vec2(C.ScreenW * 0.5f, C.ScreenH * 0.5f))
            .With<CameraComponent>(c => c.Zoom = C.WindowScale);

        // Background
        ctrl.BgEntity = Entity.Spawn("Background")
            .With<SpriteRenderer>(s => { s.Texture = ctrl.TexBg; s.ZOrder = 0f; });

        // Bird — with sensor collider for physics-based collision detection
        ctrl.BirdEntity = Entity.Spawn("Bird", Vec2(C.BirdX, C.ScreenH * 0.4f))
            .With<SpriteRenderer>(s => { s.Texture = texBird[1]; s.ZOrder = 20f; })
            .With<Bird>(b => b.Textures = texBird)
            .With<RigidBody>(r => { r.Type = BodyType.Dynamic; r.GravityScale = 0f; r.FixedRotation = true; })
            .With<BoxCollider>(c => { c.Size = Vec2(C.BirdW * 0.8f, C.BirdH * 0.8f); c.IsSensor = true; });

        // Ground collider — invisible static body at the base
        Entity.Spawn("Ground", Vec2(C.ScreenW * 0.5f, C.BaseY + 50f))
            .With<BoxCollider>(c => c.Size = Vec2(C.ScreenW * 2f, 100f));

        // Base tiles (visual only)
        for (int i = 0; i < 2; i++)
        {
            ctrl.BaseEntities[i] = Entity.Spawn("Base", Vec2(i * 336f, C.BaseY))
                .With<SpriteRenderer>(s => { s.Texture = ctrl.TexBase; s.ZOrder = 10f; });
        }
    }
}

class Program
{
    static int Main(string[] args) => DriftApp.Run<FlappyGame>(args);
}
