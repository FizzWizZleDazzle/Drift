#pragma once

#include <drift/Types.hpp>
#include <drift/Handle.hpp>

namespace drift {

// A 2D camera. The entity's Transform2D position is the point the camera
// looks at. Several active cameras render in ascending `order` (split
// screen, minimaps, picture-in-picture).
struct Camera {
    float zoom = 1.f;
    float rotation = 0.f;
    bool active = false;
    Rect viewport = {0.f, 0.f, 1.f, 1.f};   // normalized screen rect, origin top-left
    int32_t order = 0;
    uint32_t layerMask = AllWorldLayers;    // bit N = draw render layer N
    TextureHandle target;                   // render into this render target instead of the screen
    bool clear = false;                     // clear the viewport before drawing
    ColorF clearColor = {0.f, 0.f, 0.f, 1.f};
};

} // namespace drift
