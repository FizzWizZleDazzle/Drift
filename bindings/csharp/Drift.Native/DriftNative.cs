// =============================================================================
// Drift.Native - Auto-generated P/Invoke bindings (Layer 1)
// =============================================================================
// 1:1 mapping of every C function in include/drift/*.h
// Hand-written for now; a Python generator can produce this from headers.
// =============================================================================

using System;
using System.Runtime.InteropServices;

namespace Drift.Native
{
    // ---- Handle types ----
    [StructLayout(LayoutKind.Sequential)]
    public struct DriftAssetHandle { public uint Id; }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftTexture { public uint Id; }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftSpritesheet { public uint Id; }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftAnimation { public uint Id; }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftSound { public uint Id; }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftPlayingSound { public uint Id; }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftFont { public uint Id; }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftTilemap { public uint Id; }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftEmitter { public uint Id; }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftPlugin { public uint Id; }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftCamera { public uint Id; }

    // ---- Math types ----
    [StructLayout(LayoutKind.Sequential)]
    public struct DriftVec2
    {
        public float X, Y;
        public DriftVec2(float x, float y) { X = x; Y = y; }
        public static DriftVec2 Zero => new(0, 0);
        public static DriftVec2 One => new(1, 1);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftRect
    {
        public float X, Y, W, H;
        public DriftRect(float x, float y, float w, float h) { X = x; Y = y; W = w; H = h; }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftColor
    {
        public byte R, G, B, A;
        public DriftColor(byte r, byte g, byte b, byte a = 255) { R = r; G = g; B = b; A = a; }
        public static DriftColor White => new(255, 255, 255, 255);
        public static DriftColor Black => new(0, 0, 0, 255);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftColorF
    {
        public float R, G, B, A;
        public DriftColorF(float r, float g, float b, float a = 1.0f) { R = r; G = g; B = b; A = a; }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftConfig
    {
        public IntPtr Title; // const char*
        public int Width;
        public int Height;
        [MarshalAs(UnmanagedType.U1)] public bool Fullscreen;
        [MarshalAs(UnmanagedType.U1)] public bool Vsync;
        [MarshalAs(UnmanagedType.U1)] public bool Resizable;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftBodyDef
    {
        public int Type;
        public DriftVec2 Position;
        public float Rotation;
        [MarshalAs(UnmanagedType.U1)] public bool FixedRotation;
        public float LinearDamping;
        public float AngularDamping;
        public float GravityScale;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftShapeDef
    {
        public float Density;
        public float Friction;
        public float Restitution;
        [MarshalAs(UnmanagedType.U1)] public bool IsSensor;
        public uint FilterCategory;
        public uint FilterMask;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftBody
    {
        public int Index;
        public short World;
        public ushort Revision;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftShape
    {
        public int Index;
        public short World;
        public ushort Revision;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct DriftEmitterDef
    {
        public DriftTexture Texture;
        public DriftRect SrcRect;
        public float SpawnRate;
        public float LifetimeMin;
        public float LifetimeMax;
        public float SpeedMin;
        public float SpeedMax;
        public float AngleMin;
        public float AngleMax;
        public DriftColorF ColorStart;
        public DriftColorF ColorEnd;
        public float SizeStart;
        public float SizeEnd;
        public float Gravity;
        public uint MaxParticles;
    }

    // ---- Enums ----
    public enum DriftResult
    {
        Ok = 0,
        ErrorInitFailed,
        ErrorInvalidParam,
        ErrorNotFound,
        ErrorOutOfMemory,
        ErrorIo,
        ErrorFormat,
        ErrorGpu,
        ErrorAudio,
        ErrorAlreadyExists,
    }

    public enum DriftAssetType
    {
        Texture = 0,
        Sound,
        Font,
        Spritesheet,
        Tilemap,
    }

    public enum DriftFlip
    {
        None = 0,
        H = 1,
        V = 2,
    }

    public enum DriftBodyType
    {
        Static = 0,
        Dynamic,
        Kinematic,
    }

    public enum DriftKey
    {
        Unknown = 0,
        A = 4, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Space = 44,
        Right = 79, Left = 80, Down = 81, Up = 82,
        Escape = 41,
        F1 = 58,
    }

    // ---- Native function delegates ----
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void DriftUpdateFn(float dt, IntPtr userData);

    // ---- P/Invoke bindings ----
    public static class DriftNative
    {
        private const string Lib = "drift";

        // Core
        [DllImport(Lib)] public static extern DriftResult drift_init(ref DriftConfig config);
        [DllImport(Lib)] public static extern void drift_shutdown();
        [DllImport(Lib)] public static extern DriftResult drift_run(DriftUpdateFn updateFn, IntPtr userData);
        [DllImport(Lib)] public static extern float drift_get_delta_time();
        [DllImport(Lib)] public static extern float drift_get_fps();
        [DllImport(Lib)] public static extern ulong drift_get_frame_count();
        [DllImport(Lib)] public static extern double drift_get_time();
        [DllImport(Lib)] public static extern void drift_request_quit();

        // Renderer
        [DllImport(Lib)] public static extern DriftResult drift_renderer_init();
        [DllImport(Lib)] public static extern void drift_renderer_shutdown();
        [DllImport(Lib)] public static extern void drift_renderer_begin_frame();
        [DllImport(Lib)] public static extern void drift_renderer_end_frame();
        [DllImport(Lib)] public static extern void drift_renderer_present();
        [DllImport(Lib)] public static extern DriftTexture drift_texture_load([MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(Lib)] public static extern void drift_texture_destroy(DriftTexture texture);
        [DllImport(Lib)] public static extern void drift_sprite_draw(DriftTexture texture, DriftVec2 position, DriftRect srcRect, DriftVec2 scale, float rotation, DriftVec2 origin, DriftColor tint, DriftFlip flip, float zOrder);

        // Camera
        [DllImport(Lib)] public static extern DriftCamera drift_camera_create();
        [DllImport(Lib)] public static extern void drift_camera_destroy(DriftCamera camera);
        [DllImport(Lib)] public static extern void drift_camera_set_active(DriftCamera camera);
        [DllImport(Lib)] public static extern void drift_camera_set_position(DriftCamera camera, DriftVec2 position);
        [DllImport(Lib)] public static extern void drift_camera_set_zoom(DriftCamera camera, float zoom);

        // Input
        [DllImport(Lib)] public static extern void drift_input_init();
        [DllImport(Lib)] public static extern void drift_input_update();
        [DllImport(Lib)] public static extern bool drift_key_pressed(DriftKey key);
        [DllImport(Lib)] public static extern bool drift_key_held(DriftKey key);
        [DllImport(Lib)] public static extern bool drift_key_released(DriftKey key);
        [DllImport(Lib)] public static extern DriftVec2 drift_mouse_position();

        // Audio
        [DllImport(Lib)] public static extern DriftResult drift_audio_init();
        [DllImport(Lib)] public static extern void drift_audio_shutdown();
        [DllImport(Lib)] public static extern DriftSound drift_sound_load([MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(Lib)] public static extern DriftPlayingSound drift_sound_play(DriftSound sound, float volume, float pan);

        // Physics
        [DllImport(Lib)] public static extern DriftResult drift_physics_init(DriftVec2 gravity);
        [DllImport(Lib)] public static extern void drift_physics_shutdown();
        [DllImport(Lib)] public static extern void drift_physics_step(float dt);
        [DllImport(Lib)] public static extern DriftBody drift_body_create(ref DriftBodyDef def);
        [DllImport(Lib)] public static extern void drift_body_destroy(DriftBody body);
        [DllImport(Lib)] public static extern DriftVec2 drift_body_get_position(DriftBody body);
        [DllImport(Lib)] public static extern void drift_body_apply_impulse(DriftBody body, DriftVec2 impulse);

        // Font
        [DllImport(Lib)] public static extern DriftFont drift_font_load([MarshalAs(UnmanagedType.LPUTF8Str)] string path, float sizePx);
        [DllImport(Lib)] public static extern void drift_text_draw(DriftFont font, [MarshalAs(UnmanagedType.LPUTF8Str)] string text, DriftVec2 position, DriftColor color, float scale);

        // UI
        [DllImport(Lib)] public static extern DriftResult drift_ui_init();
        [DllImport(Lib)] public static extern void drift_ui_shutdown();
        [DllImport(Lib)] public static extern bool drift_ui_button([MarshalAs(UnmanagedType.LPUTF8Str)] string label, DriftRect rect);
        [DllImport(Lib)] public static extern void drift_ui_label([MarshalAs(UnmanagedType.LPUTF8Str)] string text, DriftVec2 pos);
        [DllImport(Lib)] public static extern void drift_ui_progress_bar(float fraction, DriftRect rect);

        // Tilemap
        [DllImport(Lib)] public static extern DriftTilemap drift_tilemap_load([MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(Lib)] public static extern void drift_tilemap_destroy(DriftTilemap map);
        [DllImport(Lib)] public static extern void drift_tilemap_draw(DriftTilemap map);

        // Particles
        [DllImport(Lib)] public static extern DriftEmitter drift_emitter_create(ref DriftEmitterDef def);
        [DllImport(Lib)] public static extern void drift_emitter_destroy(DriftEmitter emitter);
        [DllImport(Lib)] public static extern void drift_emitter_burst(DriftEmitter emitter, int count);
        [DllImport(Lib)] public static extern void drift_particles_update(float dt);
        [DllImport(Lib)] public static extern void drift_particles_render();
    }
}
