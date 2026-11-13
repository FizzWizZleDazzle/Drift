#pragma once

#include <drift/Types.hpp>
#include <cmath>

namespace drift {

struct GlobalTransform2D {
    Vec2 position;
    float rotation = 0.f;
    Vec2 scale = Vec2::one();

    // A child's world transform: its local position is scaled by the
    // parent's scale, then rotated and moved with the parent. A parent
    // mirrored on one axis (scale.x * scale.y < 0, as when a rig turns
    // around with scale.x = -1) mirrors its children's rotations too, so
    // the whole rig flips as one picture.
    static GlobalTransform2D compose(const GlobalTransform2D& parent, const Transform2D& local) {
        GlobalTransform2D result;
        float cosR = std::cos(parent.rotation);
        float sinR = std::sin(parent.rotation);
        float lx = local.position.x * parent.scale.x;
        float ly = local.position.y * parent.scale.y;
        result.position = {parent.position.x + lx * cosR - ly * sinR,
                           parent.position.y + lx * sinR + ly * cosR};
        bool mirrored = (parent.scale.x < 0.f) != (parent.scale.y < 0.f);
        result.rotation = parent.rotation + (mirrored ? -local.rotation : local.rotation);
        result.scale = {parent.scale.x * local.scale.x, parent.scale.y * local.scale.y};
        return result;
    }

    // A world point to the local space of this transform (the inverse of
    // what compose does to a child's position).
    Vec2 toLocal(Vec2 p) const {
        float dx = p.x - position.x, dy = p.y - position.y;
        float c = std::cos(rotation), s = std::sin(rotation);
        float rx = dx * c + dy * s, ry = -dx * s + dy * c;
        return {scale.x != 0.f ? rx / scale.x : 0.f, scale.y != 0.f ? ry / scale.y : 0.f};
    }

    // A local point to world space.
    Vec2 toWorld(Vec2 p) const {
        float lx = p.x * scale.x, ly = p.y * scale.y;
        float c = std::cos(rotation), s = std::sin(rotation);
        return {position.x + lx * c - ly * s, position.y + lx * s + ly * c};
    }

    static GlobalTransform2D from(const Transform2D& t) {
        return {t.position, t.rotation, t.scale};
    }
};

} // namespace drift
