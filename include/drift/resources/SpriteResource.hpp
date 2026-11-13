#pragma once

#include <drift/Resource.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>
#include <drift/components/SpriteAnimator.hpp>

#include <string>
#include <vector>

namespace drift {

class RendererResource;

class SpriteResource : public Resource {
public:
    explicit SpriteResource(RendererResource& renderer);
    ~SpriteResource() override;

    DRIFT_RESOURCE(SpriteResource)
    bool mainThreadOnly() const override { return true; }

    // Spritesheets
    SpritesheetHandle createSpritesheet(TextureHandle texture, int32_t frameW, int32_t frameH);
    SpritesheetHandle loadAseprite(const char* path);
    void destroySpritesheet(SpritesheetHandle sheet);
    Rect getFrame(SpritesheetHandle sheet, int32_t col, int32_t row) const;
    // The frame at an index, counted left to right, top to bottom from 0.
    Rect frameAt(SpritesheetHandle sheet, int32_t index) const;
    int32_t frameCount(SpritesheetHandle sheet) const;
    // The frames of an Aseprite tag as a clip for SpriteAnimator, each frame
    // timed by its duration in the file. A tag the file does not have logs an
    // error naming the tags it has and gives an empty clip.
    AnimationClip clipFromTag(SpritesheetHandle sheet, const char* tagName) const;
    // The tag names of an Aseprite sheet, in file order.
    std::vector<std::string> tagNames(SpritesheetHandle sheet) const;
    void setPivot(SpritesheetHandle sheet, float x, float y);
    Vec2 getPivot(SpritesheetHandle sheet) const;
    TextureHandle getTexture(SpritesheetHandle sheet) const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
