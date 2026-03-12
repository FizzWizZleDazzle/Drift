#pragma once

#include <drift/Resource.h>
#include <drift/Handle.h>
#include <drift/Types.h>

namespace drift {

class RendererResource;

struct AnimationDef {
    int32_t* frames = nullptr;
    float* durations = nullptr;
    int32_t frameCount = 0;
    bool looping = true;
};

class SpriteResource : public Resource {
public:
    explicit SpriteResource(RendererResource& renderer);
    ~SpriteResource() override;

    const char* name() const override { return "SpriteResource"; }

    // Spritesheets
    SpritesheetHandle createSpritesheet(TextureHandle texture, int32_t frameW, int32_t frameH);
    SpritesheetHandle loadAseprite(const char* path);
    void destroySpritesheet(SpritesheetHandle sheet);
    Rect getFrame(SpritesheetHandle sheet, int32_t col, int32_t row) const;
    int32_t frameCount(SpritesheetHandle sheet) const;
    void setPivot(SpritesheetHandle sheet, float x, float y);
    Vec2 getPivot(SpritesheetHandle sheet) const;
    TextureHandle getTexture(SpritesheetHandle sheet) const;

    // Animations
    AnimationHandle createAnimation(SpritesheetHandle sheet, const AnimationDef& def);
    AnimationHandle createAnimationFromTag(SpritesheetHandle sheet, const char* tagName);
    void destroyAnimation(AnimationHandle anim);
    void resetAnimation(AnimationHandle anim);
    Rect updateAnimation(AnimationHandle anim, float dt);
    Rect getAnimationFrame(AnimationHandle anim) const;
    bool isAnimationFinished(AnimationHandle anim) const;
    SpritesheetHandle getAnimationSheet(AnimationHandle anim) const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
