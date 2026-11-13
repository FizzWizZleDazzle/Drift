#pragma once

#include <drift/Handle.hpp>
#include <drift/ImageShapes.hpp>
#include <drift/Resource.hpp>

#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace drift {

class App;
class Image;

// The shapes of textures' solid pixels (drift/ImageShapes.hpp), worked out
// once per texture, frame and settings and shared by everything that asks:
// a hundred enemies with one sprite trace it once. TextureCollider2D uses
// it; ask it yourself to draw the outlines or to make shapes by hand.
//
// A texture's alpha is kept, one byte a pixel, from the first time one of
// its frames is asked for until rebuild(). It comes from reading the
// texture back from the GPU, or from the image given to rebuild().
class TextureShapeCache : public Resource {
public:
    explicit TextureShapeCache(App& app) : app_(&app) {}
    DRIFT_RESOURCE(TextureShapeCache)
    bool mainThreadOnly() const override { return true; }   // reads textures

    class Shapes {
    public:
        // Outlines in pixels from the frame's top-left corner; solid ones
        // have positive signed area and holes negative.
        std::vector<Outline> outlines;
        // The frame's size in pixels.
        Vec2 size;
        // Convex pieces of at most 8 points covering the outlines, worked
        // out the first time they are asked for.
        const std::vector<Outline>& pieces() const;

    private:
        mutable bool hasPieces_ = false;
        mutable std::vector<Outline> pieces_;
    };

    // The shapes of `frame` of the texture ({} for all of it), or null when
    // the texture cannot be read. Valid until rebuild() of the texture.
    const Shapes* get(TextureHandle texture, Rect frame, const AlphaShapeOptions& options);

    // Forgets the texture's shapes after its pixels changed, such as a hole
    // carved in terrain: every TextureCollider2D showing it is made again
    // next frame. Pass the image you wrote into the texture to take its
    // alpha from there; without one the texture is read back from the GPU,
    // which waits for the GPU and costs more for a large texture.
    void rebuild(TextureHandle texture, const Image* pixels = nullptr);

    // Goes up with every rebuild() of the texture.
    uint32_t version(TextureHandle texture) const;

private:
    using Key = std::tuple<int32_t, int32_t, int32_t, int32_t, float, float, float>;
    struct Entry {
        bool read = false;
        int32_t width = 0;
        int32_t height = 0;
        std::vector<uint8_t> alpha;
        std::map<Key, std::unique_ptr<Shapes>> shapes;
    };
    static void keepAlpha(Entry& e, const Image& img);
    App* app_;
    std::unordered_map<uint32_t, Entry> entries_;
    std::unordered_map<uint32_t, uint32_t> versions_;
};

} // namespace drift
