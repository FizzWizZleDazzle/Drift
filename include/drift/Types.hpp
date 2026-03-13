#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>

namespace drift {

// ---- Result ----
enum class Result {
    OK = 0,
    ErrorInitFailed,
    ErrorInvalidParam,
    ErrorNotFound,
    ErrorOutOfMemory,
    ErrorIO,
    ErrorFormat,
    ErrorGPU,
    ErrorAudio,
    ErrorAlreadyExists,
};

// ---- Math types ----
struct Vec2 {
    float x = 0.f, y = 0.f;

    Vec2() = default;
    constexpr Vec2(float x, float y) : x(x), y(y) {}

    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }

    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSq() const { return x * x + y * y; }
    Vec2 normalized() const {
        float len = length();
        return len > 1e-8f ? Vec2{x / len, y / len} : Vec2{0, 0};
    }
    float dot(Vec2 o) const { return x * o.x + y * o.y; }

    static constexpr Vec2 zero() { return {0.f, 0.f}; }
    static constexpr Vec2 one() { return {1.f, 1.f}; }
};

inline Vec2 operator*(float s, Vec2 v) { return {v.x * s, v.y * s}; }

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

struct Vec4 {
    float x = 0.f, y = 0.f, z = 0.f, w = 0.f;
};

struct Rect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;

    Rect() = default;
    constexpr Rect(float x, float y, float w, float h) : x(x), y(y), w(w), h(h) {}

    bool contains(Vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
    bool overlaps(Rect o) const { return x < o.x + o.w && x + w > o.x && y < o.y + o.h && y + h > o.y; }
};

struct Color {
    uint8_t r = 255, g = 255, b = 255, a = 255;

    Color() = default;
    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : r(r), g(g), b(b), a(a) {}

    static constexpr Color white()  { return {255, 255, 255, 255}; }
    static constexpr Color black()  { return {0, 0, 0, 255}; }
    static constexpr Color red()    { return {255, 0, 0, 255}; }
    static constexpr Color green()  { return {0, 255, 0, 255}; }
    static constexpr Color blue()   { return {0, 0, 255, 255}; }
};

struct ColorF {
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;

    ColorF() = default;
    constexpr ColorF(float r, float g, float b, float a = 1.f) : r(r), g(g), b(b), a(a) {}
};

// ---- Enums ----
enum class Flip {
    None = 0,
    H    = 1 << 0,
    V    = 1 << 1,
};

inline Flip operator|(Flip a, Flip b) { return static_cast<Flip>(static_cast<int>(a) | static_cast<int>(b)); }
inline bool operator&(Flip a, Flip b) { return (static_cast<int>(a) & static_cast<int>(b)) != 0; }

enum class Phase {
    Startup = 0,
    PreUpdate,
    Update,
    PostUpdate,
    Extract,
    Render,
    RenderFlush,
};

enum class BodyType {
    Static = 0,
    Dynamic,
    Kinematic,
};

enum class Anchor {
    TopLeft = 0,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

// ---- Key codes (mirrors SDL scancodes) ----
enum class Key {
    Unknown = 0,
    A = 4, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num1 = 30, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9, Num0,
    Return = 40,
    Escape = 41,
    Backspace = 42,
    Tab = 43,
    Space = 44,
    F1 = 58, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Right = 79,
    Left = 80,
    Down = 81,
    Up = 82,
    LCtrl = 224,
    LShift = 225,
    LAlt = 226,
    RCtrl = 228,
    RShift = 229,
    RAlt = 230,
    Max_ = 512,
};

enum class MouseButton {
    Left = 1,
    Middle = 2,
    Right = 3,
};

// ---- Physics types ----
struct CollisionFilter {
    uint32_t category = 0x0001;
    uint32_t mask = 0xFFFF;
};

struct ShapeDef {
    float density = 1.0f;
    float friction = 0.3f;
    float restitution = 0.0f;
    bool isSensor = false;
    CollisionFilter filter;
};

struct BodyDef {
    BodyType type = BodyType::Dynamic;
    Vec2 position;
    float rotation = 0.f;
    bool fixedRotation = false;
    float linearDamping = 0.f;
    float angularDamping = 0.f;
    float gravityScale = 1.f;
};

struct RaycastHit {
    Vec2 point;
    Vec2 normal;
    float fraction = 0.f;
    bool hit = false;
};

struct ContactEvent {
    uint64_t shapeA = 0;
    uint64_t shapeB = 0;
};

struct HitEvent {
    uint64_t shapeA = 0;
    uint64_t shapeB = 0;
    Vec2 normal;
    float speed = 0.f;
};

// ---- Transform2D (ECS built-in component) ----
struct Transform2D {
    Vec2 position;
    float rotation = 0.f;
    Vec2 scale = Vec2::one();
};

// ---- Emitter def ----
struct EmitterDef {
    uint32_t textureId = 0;
    Rect srcRect = {0, 0, 1, 1};
    float spawnRate = 10.f;
    float lifetimeMin = 0.5f;
    float lifetimeMax = 1.5f;
    float speedMin = 10.f;
    float speedMax = 50.f;
    float angleMin = 0.f;
    float angleMax = 6.2831853f;
    ColorF colorStart = {1, 1, 1, 1};
    ColorF colorEnd = {1, 1, 1, 0};
    float sizeStart = 4.f;
    float sizeEnd = 1.f;
    float gravity = 0.f;
    uint32_t maxParticles = 256;
};

// ---- UI theme ----
struct UITheme {
    ColorF primary       = {0.2f, 0.6f, 0.9f, 1.0f};
    ColorF secondary     = {0.1f, 0.1f, 0.15f, 1.0f};
    ColorF background    = {0.05f, 0.05f, 0.08f, 0.9f};
    ColorF text          = {0.95f, 0.95f, 0.95f, 1.0f};
    ColorF button        = {0.2f, 0.2f, 0.3f, 1.0f};
    ColorF buttonHover   = {0.3f, 0.3f, 0.45f, 1.0f};
    ColorF buttonActive  = {0.15f, 0.15f, 0.25f, 1.0f};
    float rounding = 4.f;
    float padding = 8.f;
};

// ---- Panel flags ----
enum class PanelFlags {
    None       = 0,
    NoBg       = 1 << 0,
    Border     = 1 << 1,
    Scrollable = 1 << 2,
};

inline PanelFlags operator|(PanelFlags a, PanelFlags b) { return static_cast<PanelFlags>(static_cast<int>(a) | static_cast<int>(b)); }
inline bool operator&(PanelFlags a, PanelFlags b) { return (static_cast<int>(a) & static_cast<int>(b)) != 0; }

// ---- ECS types ----
using Entity = uint64_t;
constexpr Entity InvalidEntity = 0;
using ComponentId = uint64_t;

// ---- Log levels ----
enum class LogLevel {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
};

} // namespace drift
