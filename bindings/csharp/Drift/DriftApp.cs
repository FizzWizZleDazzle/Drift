// =============================================================================
// Drift SDK - Idiomatic C# API (Layer 2)
// =============================================================================
// Hand-written, Unity-like API built on top of Drift.Native P/Invoke layer.
// =============================================================================

using System;
using System.Numerics;
using System.Runtime.InteropServices;
using Drift.Native;

namespace Drift
{
    // ---- Core App ----
    public class DriftApp : IDisposable
    {
        private bool _disposed;
        private DriftUpdateFn? _nativeCallback;

        public int Width { get; init; } = 1280;
        public int Height { get; init; } = 720;
        public string Title { get; init; } = "Drift Game";
        public bool Vsync { get; init; } = true;
        public bool Fullscreen { get; init; }
        public bool Resizable { get; init; } = true;

        public Action? OnStartup { get; set; }
        public Action<float>? OnUpdate { get; set; }
        public Action? OnShutdown { get; set; }

        public DriftApp() { }

        public void Run()
        {
            var titlePtr = Marshal.StringToCoTaskMemUTF8(Title);
            var config = new DriftConfig
            {
                Title = titlePtr,
                Width = Width,
                Height = Height,
                Fullscreen = Fullscreen,
                Vsync = Vsync,
                Resizable = Resizable,
            };

            try
            {
                var result = DriftNative.drift_init(ref config);
                if (result != DriftResult.Ok)
                    throw new Exception($"Failed to initialize Drift: {result}");

                DriftNative.drift_renderer_init();
                DriftNative.drift_input_init();
                DriftNative.drift_audio_init();
                DriftNative.drift_ui_init();

                OnStartup?.Invoke();

                _nativeCallback = (dt, _) =>
                {
                    DriftNative.drift_renderer_begin_frame();
                    OnUpdate?.Invoke(dt);
                    DriftNative.drift_renderer_end_frame();
                    DriftNative.drift_renderer_present();
                };

                DriftNative.drift_run(_nativeCallback, IntPtr.Zero);

                OnShutdown?.Invoke();
            }
            finally
            {
                Marshal.FreeCoTaskMem(titlePtr);
                Dispose();
            }
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;

            DriftNative.drift_ui_shutdown();
            DriftNative.drift_audio_shutdown();
            DriftNative.drift_renderer_shutdown();
            DriftNative.drift_shutdown();
            GC.SuppressFinalize(this);
        }
    }

    // ---- Input ----
    public static class Input
    {
        public static bool KeyPressed(DriftKey key) => DriftNative.drift_key_pressed(key);
        public static bool KeyHeld(DriftKey key) => DriftNative.drift_key_held(key);
        public static bool KeyReleased(DriftKey key) => DriftNative.drift_key_released(key);

        public static Vector2 MousePosition
        {
            get
            {
                var p = DriftNative.drift_mouse_position();
                return new Vector2(p.X, p.Y);
            }
        }
    }

    // ---- Assets ----
    public static class Assets
    {
        public static Texture LoadTexture(string path)
        {
            var tex = DriftNative.drift_texture_load(path);
            return new Texture(tex);
        }

        public static Sound LoadSound(string path)
        {
            var snd = DriftNative.drift_sound_load(path);
            return new Sound(snd);
        }

        public static Font LoadFont(string path, float sizePx)
        {
            var fnt = DriftNative.drift_font_load(path, sizePx);
            return new Font(fnt);
        }

        public static Tilemap LoadTilemap(string path)
        {
            var tm = DriftNative.drift_tilemap_load(path);
            return new Tilemap(tm);
        }
    }

    // ---- Wrapper types ----
    public class Texture : IDisposable
    {
        internal DriftTexture Handle;
        public Texture(DriftTexture handle) { Handle = handle; }

        public void Draw(Vector2 position, DriftColor? tint = null, float rotation = 0f, float zOrder = 0f)
        {
            DriftNative.drift_sprite_draw(Handle,
                new DriftVec2(position.X, position.Y),
                new DriftRect(0, 0, 0, 0),
                new DriftVec2(1, 1),
                rotation,
                new DriftVec2(0, 0),
                tint ?? DriftColor.White,
                DriftFlip.None,
                zOrder);
        }

        public void Dispose()
        {
            if (Handle.Id != 0) DriftNative.drift_texture_destroy(Handle);
            Handle.Id = 0;
            GC.SuppressFinalize(this);
        }
    }

    public class Sound : IDisposable
    {
        internal DriftSound Handle;
        public Sound(DriftSound handle) { Handle = handle; }
        public void Play(float volume = 1.0f, float pan = 0.0f)
            => DriftNative.drift_sound_play(Handle, volume, pan);
        public void Dispose() { GC.SuppressFinalize(this); }
    }

    public class Font : IDisposable
    {
        internal DriftFont Handle;
        public Font(DriftFont handle) { Handle = handle; }
        public void DrawText(string text, Vector2 position, DriftColor? color = null, float scale = 1.0f)
            => DriftNative.drift_text_draw(Handle, text, new DriftVec2(position.X, position.Y), color ?? DriftColor.White, scale);
        public void Dispose() { GC.SuppressFinalize(this); }
    }

    public class Tilemap : IDisposable
    {
        internal DriftTilemap Handle;
        public Tilemap(DriftTilemap handle) { Handle = handle; }
        public void Draw() => DriftNative.drift_tilemap_draw(Handle);
        public void Dispose()
        {
            if (Handle.Id != 0) DriftNative.drift_tilemap_destroy(Handle);
            Handle.Id = 0;
            GC.SuppressFinalize(this);
        }
    }

    // ---- Audio ----
    public static class Audio
    {
        public static void PlaySound(Sound sound, float volume = 1.0f, float pan = 0.0f)
            => sound.Play(volume, pan);
    }

    // ---- UI ----
    public static class DriftUI
    {
        public static void Label(string text, Vector2? pos = null)
        {
            var p = pos ?? Vector2.Zero;
            DriftNative.drift_ui_label(text, new DriftVec2(p.X, p.Y));
        }

        public static bool Button(string label, DriftRect? rect = null)
        {
            var r = rect ?? new DriftRect(0, 0, 100, 30);
            return DriftNative.drift_ui_button(label, r);
        }

        public static void ProgressBar(float fraction, DriftRect? rect = null)
        {
            var r = rect ?? new DriftRect(0, 0, 200, 20);
            DriftNative.drift_ui_progress_bar(fraction, r);
        }
    }

    // ---- Physics ----
    public static class Physics
    {
        public static void Init(Vector2 gravity)
            => DriftNative.drift_physics_init(new DriftVec2(gravity.X, gravity.Y));

        public static void Shutdown()
            => DriftNative.drift_physics_shutdown();

        public static void Step(float dt)
            => DriftNative.drift_physics_step(dt);
    }

    public class RigidBody : IDisposable
    {
        internal DriftBody Handle;

        public RigidBody(DriftBodyType type, Vector2 position)
        {
            var def = new DriftBodyDef
            {
                Type = (int)type,
                Position = new DriftVec2(position.X, position.Y),
                GravityScale = 1.0f,
            };
            Handle = DriftNative.drift_body_create(ref def);
        }

        public Vector2 Position
        {
            get
            {
                var p = DriftNative.drift_body_get_position(Handle);
                return new Vector2(p.X, p.Y);
            }
        }

        public void ApplyImpulse(Vector2 impulse)
            => DriftNative.drift_body_apply_impulse(Handle, new DriftVec2(impulse.X, impulse.Y));

        public void Dispose()
        {
            DriftNative.drift_body_destroy(Handle);
            GC.SuppressFinalize(this);
        }
    }
}
