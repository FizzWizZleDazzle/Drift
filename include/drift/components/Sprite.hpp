#pragma once

#include <drift/Types.hpp>
#include <drift/Handle.hpp>

namespace drift {

struct Sprite {
    TextureHandle texture;
    Rect srcRect = {};           // {} = full texture
    Vec2 origin = {};            // pivot point in source pixels
    Color tint = Color::white();
    Flip flip = Flip::None;
    float zOrder = 0.f;
    bool visible = true;
    BlendMode blend = BlendMode::Alpha;
    MaterialHandle material;     // custom shader; empty = default
    uint8_t layer = 0;           // render layer 0..30, or ScreenLayer for HUD sprites
};

} // namespace drift
