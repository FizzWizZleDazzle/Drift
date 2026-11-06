#pragma once

// Scalar and Vec2 helpers: interpolation, clamping, angles, smoothing.

#include <drift/Types.hpp>

#include <algorithm>
#include <cmath>

namespace drift {

inline constexpr float Pi  = 3.14159265358979323846f;
inline constexpr float Tau = 6.28318530717958647692f;

// ---- Vec2 operators missing from Types.hpp ----

inline Vec2 operator-(Vec2 v) { return {-v.x, -v.y}; }
inline Vec2 operator/(Vec2 v, float s) { return {v.x / s, v.y / s}; }
inline Vec2& operator/=(Vec2& v, float s) { v.x /= s; v.y /= s; return v; }
// Component-wise product (scaling by a per-axis factor).
inline Vec2 hadamard(Vec2 a, Vec2 b) { return {a.x * b.x, a.y * b.y}; }
inline bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Vec2 a, Vec2 b) { return !(a == b); }

// ---- Scalars ----

inline float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float clamp01(float v) { return clamp(v, 0.f, 1.f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
// t such that lerp(a, b, t) == v. Returns 0 when a == b.
inline float inverseLerp(float a, float b, float v) { return a == b ? 0.f : (v - a) / (b - a); }
// Map v from [inLo, inHi] to [outLo, outHi] (not clamped).
inline float remap(float v, float inLo, float inHi, float outLo, float outHi) {
    return lerp(outLo, outHi, inverseLerp(inLo, inHi, v));
}
inline bool approx(float a, float b, float epsilon = 1e-5f) { return std::fabs(a - b) <= epsilon; }
inline float sign(float v) { return v > 0.f ? 1.f : (v < 0.f ? -1.f : 0.f); }

// Move `current` toward `target` by at most `maxDelta`; never overshoots.
inline float moveTowards(float current, float target, float maxDelta) {
    float d = target - current;
    if (std::fabs(d) <= maxDelta) return target;
    return current + sign(d) * maxDelta;
}

// Wrap an angle to (-Pi, Pi].
inline float wrapAngle(float radians) {
    float a = std::fmod(radians + Pi, Tau);
    if (a <= 0.f) a += Tau;
    return a - Pi;
}

// Shortest signed difference from angle a to angle b, in (-Pi, Pi].
inline float angleDelta(float a, float b) { return wrapAngle(b - a); }
// Interpolate between angles along the shorter arc.
inline float lerpAngle(float a, float b, float t) { return a + angleDelta(a, b) * t; }

inline float toRadians(float degrees) { return degrees * (Pi / 180.f); }
inline float toDegrees(float radians) { return radians * (180.f / Pi); }

// Critically damped spring toward `target` (Game Programming Gems 4, 1.10).
// `velocity` carries state between calls; `smoothTime` is roughly the time
// to reach the target. `maxSpeed` caps the speed (infinite by default).
inline float smoothDamp(float current, float target, float& velocity, float smoothTime,
                        float dt, float maxSpeed = INFINITY) {
    smoothTime = std::max(0.0001f, smoothTime);
    float omega = 2.f / smoothTime;
    float x = omega * dt;
    float expo = 1.f / (1.f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target;
    float originalTarget = target;
    float maxChange = maxSpeed * smoothTime;
    change = clamp(change, -maxChange, maxChange);
    target = current - change;
    float temp = (velocity + omega * change) * dt;
    velocity = (velocity - omega * temp) * expo;
    float result = target + (change + temp) * expo;
    // Do not overshoot.
    if ((originalTarget - current > 0.f) == (result > originalTarget)) {
        result = originalTarget;
        velocity = 0.f;
    }
    return result;
}

// Frame-rate independent exponential approach: fraction of the remaining
// distance covered after `dt` seconds with the given rate (1/s).
inline float damp(float current, float target, float rate, float dt) {
    return lerp(current, target, 1.f - std::exp(-rate * dt));
}

// ---- Vec2 ----

// 2D cross product (z of the 3D cross): positive when b is counter-clockwise
// from a in a Y-up frame (clockwise on screen, where Y points down).
inline float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
// Perpendicular vector, rotated +90 degrees: (x, y) -> (-y, x).
inline Vec2 perp(Vec2 v) { return {-v.y, v.x}; }
inline Vec2 rotate(Vec2 v, float radians) {
    float c = std::cos(radians), s = std::sin(radians);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}
// Angle of v from the +X axis, in (-Pi, Pi]. Matches Transform2D::rotation.
inline float angle(Vec2 v) { return std::atan2(v.y, v.x); }
// Signed angle from a to b.
inline float angleBetween(Vec2 a, Vec2 b) { return std::atan2(cross(a, b), dot(a, b)); }
inline Vec2 fromAngle(float radians, float length = 1.f) {
    return {std::cos(radians) * length, std::sin(radians) * length};
}
inline float distance(Vec2 a, Vec2 b) { return (b - a).length(); }
inline float distanceSq(Vec2 a, Vec2 b) { return (b - a).lengthSq(); }
inline Vec2 lerp(Vec2 a, Vec2 b, float t) { return {lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }
inline Vec2 clamp(Vec2 v, Vec2 lo, Vec2 hi) { return {clamp(v.x, lo.x, hi.x), clamp(v.y, lo.y, hi.y)}; }
// Scale v down so its length is at most maxLength.
inline Vec2 clampLength(Vec2 v, float maxLength) {
    float l2 = v.lengthSq();
    if (l2 <= maxLength * maxLength || l2 == 0.f) return v;
    return v * (maxLength / std::sqrt(l2));
}
inline bool approx(Vec2 a, Vec2 b, float epsilon = 1e-5f) {
    return approx(a.x, b.x, epsilon) && approx(a.y, b.y, epsilon);
}
// Reflect v about a surface with unit normal n.
inline Vec2 reflectVector(Vec2 v, Vec2 n) { return v - n * (2.f * dot(v, n)); }
// Project v onto onto (any length, non-zero).
inline Vec2 project(Vec2 v, Vec2 onto) {
    float l2 = onto.lengthSq();
    return l2 == 0.f ? Vec2{} : onto * (dot(v, onto) / l2);
}

// Move `current` toward `target` by at most `maxDistance`; never overshoots.
inline Vec2 moveTowards(Vec2 current, Vec2 target, float maxDistance) {
    Vec2 d = target - current;
    float len = d.length();
    if (len <= maxDistance || len == 0.f) return target;
    return current + d * (maxDistance / len);
}

inline Vec2 smoothDamp(Vec2 current, Vec2 target, Vec2& velocity, float smoothTime,
                       float dt, float maxSpeed = INFINITY) {
    return {smoothDamp(current.x, target.x, velocity.x, smoothTime, dt, maxSpeed),
            smoothDamp(current.y, target.y, velocity.y, smoothTime, dt, maxSpeed)};
}

inline Vec2 damp(Vec2 current, Vec2 target, float rate, float dt) {
    return lerp(current, target, 1.f - std::exp(-rate * dt));
}

// ---- Colors ----

inline Color lerp(Color a, Color b, float t) {
    auto ch = [t](uint8_t x, uint8_t y) {
        return static_cast<uint8_t>(clamp(lerp(float(x), float(y), t) + 0.5f, 0.f, 255.f));
    };
    return {ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), ch(a.a, b.a)};
}

inline ColorF lerp(ColorF a, ColorF b, float t) {
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}

} // namespace drift
