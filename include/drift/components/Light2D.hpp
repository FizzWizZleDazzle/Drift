#pragma once

#include <drift/Types.hpp>

#include <cstdint>

namespace drift {

// A light that shines in every direction. Place it with the entity's
// Transform2D. Needs LightingPlugin.
struct PointLight2D {
    ColorF   color = {1.f, 1.f, 1.f, 1.f};
    float    intensity = 1.f;
    float    radius = 200.f;               // world units; no light past this distance
    float    falloff = 2.f;                // 1 = linear fade to the edge, higher = tighter core
    bool     castShadows = true;
    uint32_t layerMask = AllWorldLayers;   // cameras whose layerMask shares a bit see this light
};

// A cone of light. `direction` is in radians and adds to the entity's
// Transform2D rotation, so rotating the entity aims the light.
struct SpotLight2D {
    ColorF   color = {1.f, 1.f, 1.f, 1.f};
    float    intensity = 1.f;
    float    radius = 300.f;
    float    falloff = 2.f;
    float    direction = 0.f;              // radians, 0 = +x
    float    angle = 0.8f;                 // full cone angle in radians
    float    softness = 0.2f;              // 0 = hard cone edge, 1 = fades from the center
    bool     castShadows = true;
    uint32_t layerMask = AllWorldLayers;
};

enum class OccluderShape : uint8_t {
    Box,            // halfSize
    Circle,         // radius
    Polygon,        // points[0..pointCount)
    FromCollider,   // the entity's BoxCollider2D or CircleCollider2D
};

// Blocks light and casts a hard shadow from every light with castShadows.
// Shapes are in the entity's local space: they follow its Transform2D
// position, rotation and scale.
struct LightOccluder2D {
    static constexpr int kMaxPoints = 16;

    OccluderShape shape = OccluderShape::Box;
    Vec2     halfSize = {16.f, 16.f};
    float    radius = 16.f;
    Vec2     points[kMaxPoints] = {};      // polygon, either winding
    uint8_t  pointCount = 0;
    Vec2     offset = {};
    bool     shadowSelf = false;           // false: the occluder's own area stays lit
    uint32_t layerMask = AllWorldLayers;

    static LightOccluder2D box(Vec2 halfSize) {
        LightOccluder2D o;
        o.shape = OccluderShape::Box;
        o.halfSize = halfSize;
        return o;
    }
    static LightOccluder2D circle(float radius) {
        LightOccluder2D o;
        o.shape = OccluderShape::Circle;
        o.radius = radius;
        return o;
    }
    static LightOccluder2D polygon(const Vec2* pts, int count) {
        LightOccluder2D o;
        o.shape = OccluderShape::Polygon;
        o.pointCount = static_cast<uint8_t>(count < kMaxPoints ? count : kMaxPoints);
        for (int i = 0; i < o.pointCount; ++i) o.points[i] = pts[i];
        return o;
    }
    static LightOccluder2D fromCollider() {
        LightOccluder2D o;
        o.shape = OccluderShape::FromCollider;
        return o;
    }
};

} // namespace drift
