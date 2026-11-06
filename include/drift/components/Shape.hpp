#pragma once

#include <drift/Types.hpp>

namespace drift {

enum class ShapeKind : uint8_t {
    Rectangle,
    Circle,
};

// A solid or outlined rectangle or circle drawn at the entity's transform,
// for blocks, walls, markers and prototypes that need no texture. Rotation
// and scale come from the transform.
struct Shape2D {
    ShapeKind kind = ShapeKind::Rectangle;
    Vec2 size = {32.f, 32.f};      // Rectangle: full width and height
    float radius = 16.f;           // Circle
    Color color = Color::white();
    float outline = 0.f;           // 0 = filled; otherwise the line thickness
    float cornerRadius = 0.f;      // Rectangle, filled and unrotated only
    Vec2 anchor = {0.5f, 0.5f};    // Rectangle: the point on the position, 0..1
    float zOrder = 0.f;
    uint8_t layer = 0;             // render layer 0..30, or ScreenLayer
    bool visible = true;
};

} // namespace drift
