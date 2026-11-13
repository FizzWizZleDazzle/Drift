#pragma once

// Shapes from an image's alpha channel: the outlines of its solid parts
// (holes included), simplified to a few vertices, and convex pieces small
// enough for physics polygons. TextureCollider2D uses these through
// TextureShapeCache; call them directly to draw outlines or to build
// shapes yourself.
//
// Points are in pixels with (0, 0) at the top-left corner of the area read;
// pixel (x, y) covers x..x+1 and y..y+1. Solid outlines have positive
// signed area (outlineArea) and holes negative, which is the winding Box2D
// polygons and chain loops expect.

#include <drift/Types.hpp>

#include <cstdint>
#include <vector>

namespace drift {

class Image;

// An alpha channel in memory: `width` by `height` values, `pixelStride`
// bytes apart along a row and `rowStride` bytes from one row to the next.
struct AlphaView {
    const uint8_t* data = nullptr;
    int32_t width = 0;
    int32_t height = 0;
    int32_t pixelStride = 1;
    int32_t rowStride = 0;
};

// The alpha of `region` of an image ({} for all of it), clipped to the
// image. Valid while the image is unchanged.
AlphaView alphaOf(const Image& image, Rect region = {});

struct AlphaShapeOptions {
    float threshold = 0.5f;   // a pixel is solid when its alpha, 0 to 1, is at least this
    float tolerance = 1.f;    // how far in pixels a simplified outline may stray from the traced one
    float minArea = 4.f;      // solid parts and holes smaller than this, in square pixels, are dropped
};

using Outline = std::vector<Vec2>;

// Every outline of the solid pixels, traced with marching squares through
// the pixel centres: one loop per solid part and one per hole, with a
// point on every pixel edge the outline crosses. Pixels touching only at a
// corner belong to the same part.
std::vector<Outline> traceAlphaOutlines(AlphaView alpha, float threshold);

// The loop with as few points as keep it within `tolerance` pixels of the
// original (Douglas-Peucker, run on both halves of the closed loop). Fewer
// than 3 points means nothing is left.
Outline simplifyOutline(const Outline& loop, float tolerance);

// The signed area of a closed loop: positive for solid outlines, negative
// for holes.
float outlineArea(const Outline& loop);

// Traced, small parts and holes dropped, and simplified.
std::vector<Outline> alphaOutlines(AlphaView alpha, const AlphaShapeOptions& options = {});

// Convex pieces that cover the solid outlines minus their holes, each with
// at most `maxVertices` points (Box2D takes 8): holes are joined to their
// outline by a bridge, the result is cut into triangles by ear clipping and
// neighbouring triangles are merged while the piece stays convex
// (Hertel-Mehlhorn).
std::vector<Outline> convexPieces(const std::vector<Outline>& outlines, int32_t maxVertices = 8);

} // namespace drift
