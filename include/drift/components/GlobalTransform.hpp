#pragma once

#include <drift/Types.hpp>
#include <cmath>

namespace drift {

struct GlobalTransform2D {
    Vec2 position;
    float rotation = 0.f;
    Vec2 scale = Vec2::one();

    static GlobalTransform2D compose(const GlobalTransform2D& parent, const Transform2D& local) {
        GlobalTransform2D result;
        float cosR = std::cos(parent.rotation);
        float sinR = std::sin(parent.rotation);
        result.position = {
            parent.position.x + (local.position.x * cosR - local.position.y * sinR) * parent.scale.x,
            parent.position.y + (local.position.x * sinR + local.position.y * cosR) * parent.scale.y
        };
        result.rotation = parent.rotation + local.rotation;
        result.scale = {parent.scale.x * local.scale.x, parent.scale.y * local.scale.y};
        return result;
    }

    static GlobalTransform2D from(const Transform2D& t) {
        return {t.position, t.rotation, t.scale};
    }
};

} // namespace drift
