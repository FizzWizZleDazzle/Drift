#pragma once

#include <drift/Types.hpp>

#include <cstdint>
#include <vector>

namespace drift {

// Pixels in memory: RGBA, 8 bits a channel, row by row from the top-left.
// Make one, load one from a file or read a render target into one
// (RendererResource::readTexture), change its pixels, then show it with
// RendererResource::createTextureFromPixels or update a texture with
// RendererResource::updateTexture. Positions outside the image are skipped
// by writes and read as transparent.
class Image {
public:
    Image() = default;
    Image(int32_t width, int32_t height, Color fill = {0, 0, 0, 0});

    // PNG, JPG, BMP, TGA or GIF. An empty image when the file does not load.
    static Image load(const char* path);
    bool savePng(const char* path) const;

    int32_t width() const { return width_; }
    int32_t height() const { return height_; }
    bool empty() const { return pixels_.empty(); }
    uint8_t* data() { return pixels_.data(); }
    const uint8_t* data() const { return pixels_.data(); }

    Color get(int32_t x, int32_t y) const;
    void set(int32_t x, int32_t y, Color c);
    // `c` over the pixel by its alpha.
    void blend(int32_t x, int32_t y, Color c);

    void fill(Color c);
    // Set the pixels to `c` as it is, alpha included; a transparent color
    // cuts a hole.
    void fillRect(Rect r, Color c);
    void fillCircle(Vec2 center, float radius, Color c);
    // From `a` at the left (or top, when vertical) to `b` at the other side.
    void fillGradient(Color a, Color b, bool vertical);

    // Shapes, blended over what is there. Coordinates are pixels.
    void drawLine(Vec2 a, Vec2 b, Color c, float thickness = 1.f);
    void drawRect(Rect r, Color c, float thickness = 1.f);
    void drawCircle(Vec2 center, float radius, Color c);
    void drawCircleOutline(Vec2 center, float radius, Color c, float thickness = 1.f);

    // `src` (or the part of it in `srcRect`; {} for all) with its top-left
    // at (x, y). With `blend` false the pixels are copied as they are.
    void blit(const Image& src, int32_t x, int32_t y, Rect srcRect = {}, bool blend = true);
    Image crop(Rect r) const;
    void flipX();
    void flipY();

private:
    int32_t width_ = 0;
    int32_t height_ = 0;
    std::vector<uint8_t> pixels_;
};

} // namespace drift
