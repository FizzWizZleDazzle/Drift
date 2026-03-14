#pragma once

#include <drift/Types.hpp>
#include <drift/Handle.hpp>

namespace drift {

struct Sprite {
    TextureHandle texture;
    Rect srcRect = {};           // {} = full texture
    Vec2 origin = {};            // pivot point
    Color tint = Color::white();
    Flip flip = Flip::None;
    float zOrder = 0.f;
    bool visible = true;
};

} // namespace drift
