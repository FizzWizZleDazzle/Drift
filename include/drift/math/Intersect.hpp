#pragma once

// Overlap and intersection tests between 2D primitives: axis-aligned boxes
// (Rect), circles, segments and rays. Pure geometry, no physics engine.

#include <drift/Types.hpp>
#include <drift/math/Vector.hpp>

#include <cmath>
#include <optional>
#include <utility>

namespace drift {

struct Circle {
    Vec2 center;
    float radius = 0.f;
};

struct Segment {
    Vec2 a;
    Vec2 b;
};

// A ray hit: `t` is the distance along the ray direction in units of the
// direction's length (for a segment, 0 at a and 1 at b).
struct RayHit {
    float t = 0.f;
    Vec2 point;
    Vec2 normal;
};

// ---- Points ----

inline bool contains(const Rect& r, Vec2 p) { return r.contains(p); }
inline bool contains(const Circle& c, Vec2 p) { return distanceSq(c.center, p) <= c.radius * c.radius; }

inline Vec2 closestPoint(const Rect& r, Vec2 p) {
    return {clamp(p.x, r.x, r.x + r.w), clamp(p.y, r.y, r.y + r.h)};
}

inline Vec2 closestPoint(const Segment& s, Vec2 p) {
    Vec2 ab = s.b - s.a;
    float l2 = ab.lengthSq();
    if (l2 == 0.f) return s.a;
    float t = clamp01(dot(p - s.a, ab) / l2);
    return s.a + ab * t;
}

inline float distance(const Segment& s, Vec2 p) { return distance(closestPoint(s, p), p); }

// ---- Shape vs shape ----

inline bool overlaps(const Rect& a, const Rect& b) { return a.overlaps(b); }

inline bool overlaps(const Circle& a, const Circle& b) {
    float r = a.radius + b.radius;
    return distanceSq(a.center, b.center) <= r * r;
}

inline bool overlaps(const Circle& c, const Rect& r) {
    return distanceSq(closestPoint(r, c.center), c.center) <= c.radius * c.radius;
}
inline bool overlaps(const Rect& r, const Circle& c) { return overlaps(c, r); }

// The overlapping region of two rects, or nothing when they do not overlap.
inline std::optional<Rect> intersection(const Rect& a, const Rect& b) {
    float x0 = std::fmax(a.x, b.x), y0 = std::fmax(a.y, b.y);
    float x1 = std::fmin(a.x + a.w, b.x + b.w), y1 = std::fmin(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0) return std::nullopt;
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

// Minimum translation that pushes `a` out of `b` (zero when they do not overlap).
inline Vec2 penetration(const Rect& a, const Rect& b) {
    auto i = intersection(a, b);
    if (!i) return {};
    float acx = a.x + a.w * 0.5f, acy = a.y + a.h * 0.5f;
    float bcx = b.x + b.w * 0.5f, bcy = b.y + b.h * 0.5f;
    if (i->w < i->h) return {acx < bcx ? -i->w : i->w, 0.f};
    return {0.f, acy < bcy ? -i->h : i->h};
}

// ---- Segments ----

// Intersection point of two segments, if they cross (collinear overlaps
// report no single point and return nothing).
inline std::optional<Vec2> intersect(const Segment& s1, const Segment& s2) {
    Vec2 r = s1.b - s1.a;
    Vec2 s = s2.b - s2.a;
    float denom = cross(r, s);
    if (std::fabs(denom) < 1e-12f) return std::nullopt;
    Vec2 qp = s2.a - s1.a;
    float t = cross(qp, s) / denom;
    float u = cross(qp, r) / denom;
    if (t < 0.f || t > 1.f || u < 0.f || u > 1.f) return std::nullopt;
    return s1.a + r * t;
}

inline bool overlaps(const Segment& a, const Segment& b) { return intersect(a, b).has_value(); }

inline bool overlaps(const Segment& s, const Circle& c) {
    return distanceSq(closestPoint(s, c.center), c.center) <= c.radius * c.radius;
}
inline bool overlaps(const Circle& c, const Segment& s) { return overlaps(s, c); }

// ---- Rays (origin + direction). The direction need not be normalized;
// `maxT` limits the hit to origin + dir * maxT. ----

inline std::optional<RayHit> raycast(Vec2 origin, Vec2 dir, const Rect& r, float maxT = INFINITY) {
    float tmin = 0.f, tmax = maxT;
    Vec2 n{};
    const float lo[2] = {r.x, r.y};
    const float hi[2] = {r.x + r.w, r.y + r.h};
    const float o[2] = {origin.x, origin.y};
    const float d[2] = {dir.x, dir.y};
    for (int axis = 0; axis < 2; ++axis) {
        if (std::fabs(d[axis]) < 1e-12f) {
            if (o[axis] < lo[axis] || o[axis] > hi[axis]) return std::nullopt;
            continue;
        }
        float inv = 1.f / d[axis];
        float t1 = (lo[axis] - o[axis]) * inv;
        float t2 = (hi[axis] - o[axis]) * inv;
        float sgn = -1.f;
        if (t1 > t2) { std::swap(t1, t2); sgn = 1.f; }
        if (t1 > tmin) {
            tmin = t1;
            n = axis == 0 ? Vec2{sgn, 0.f} : Vec2{0.f, sgn};
        }
        tmax = std::fmin(tmax, t2);
        if (tmin > tmax) return std::nullopt;
    }
    return RayHit{tmin, origin + dir * tmin, n};
}

inline std::optional<RayHit> raycast(Vec2 origin, Vec2 dir, const Circle& c, float maxT = INFINITY) {
    Vec2 m = origin - c.center;
    float a = dir.lengthSq();
    if (a == 0.f) return std::nullopt;
    float b = dot(m, dir);
    float cc = m.lengthSq() - c.radius * c.radius;
    if (cc > 0.f && b > 0.f) return std::nullopt;   // outside and pointing away
    float disc = b * b - a * cc;
    if (disc < 0.f) return std::nullopt;
    float t = (-b - std::sqrt(disc)) / a;
    if (t < 0.f) t = 0.f;                            // origin inside the circle
    if (t > maxT) return std::nullopt;
    Vec2 p = origin + dir * t;
    return RayHit{t, p, (p - c.center).normalized()};
}

inline std::optional<RayHit> raycast(Vec2 origin, Vec2 dir, const Segment& s, float maxT = INFINITY) {
    Vec2 e = s.b - s.a;
    float denom = cross(dir, e);
    if (std::fabs(denom) < 1e-12f) return std::nullopt;
    Vec2 qp = s.a - origin;
    float t = cross(qp, e) / denom;
    float u = cross(qp, dir) / denom;
    if (t < 0.f || t > maxT || u < 0.f || u > 1.f) return std::nullopt;
    Vec2 n = perp(e).normalized();
    if (dot(n, dir) > 0.f) n = -n;
    return RayHit{t, origin + dir * t, n};
}

// Segment versions: t runs 0..1 from s.a to s.b.
inline std::optional<RayHit> intersect(const Segment& s, const Rect& r) { return raycast(s.a, s.b - s.a, r, 1.f); }
inline std::optional<RayHit> intersect(const Segment& s, const Circle& c) { return raycast(s.a, s.b - s.a, c, 1.f); }
inline bool overlaps(const Segment& s, const Rect& r) { return intersect(s, r).has_value(); }
inline bool overlaps(const Rect& r, const Segment& s) { return overlaps(s, r); }

} // namespace drift
