#pragma once

#include <drift/Plugin.hpp>
#include <drift/Reflect.hpp>
#include <drift/App.hpp>
#include <drift/Events.hpp>
#include <drift/Query.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/SpriteAnimator.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/SpriteResource.hpp>
#include <drift/resources/Time.hpp>

#include <algorithm>

namespace drift {

// Puts the animator's current frame on the sprite: the clip's sheet texture
// and the frame's region in it. The origin stays at the same fraction of the
// region, so a clip with a different frame size keeps the sprite's anchor.
inline void showAnimatorFrame(const SpriteAnimator& animator, Sprite& sprite, const SpriteResource& sprites,
                              const RendererResource& renderer) {
    const AnimationClip* clip = animator.find(animator.current);
    if (!clip || clip->frames.empty()) return;
    int32_t i = std::clamp(animator.frame, 0, static_cast<int32_t>(clip->frames.size()) - 1);
    Rect next = sprites.frameAt(clip->sheet, clip->frames[i]);
    Vec2 before{sprite.srcRect.w, sprite.srcRect.h};
    if (before.x <= 0.f || before.y <= 0.f) {
        int32_t w = 0, h = 0;
        renderer.getTextureSize(sprite.texture, &w, &h);
        before = {static_cast<float>(w), static_cast<float>(h)};
    }
    if (before.x > 0.f && before.y > 0.f && (before.x != next.w || before.y != next.h)) {
        sprite.origin = {sprite.origin.x / before.x * next.w, sprite.origin.y / before.y * next.h};
    }
    sprite.texture = sprites.getTexture(clip->sheet);
    sprite.srcRect = next;
}

inline void spriteAnimationUpdate(Res<Time> time, Res<SpriteResource> sprites, Res<RendererResource> renderer,
                                  QueryMut<SpriteAnimator, Sprite> animated,
                                  EventWriter<AnimationFinished> finished) {
    float dt = time->delta;
    std::vector<std::string> ended;
    animated.iterWithEntity([&](EntityId e, SpriteAnimator& animator, Sprite& sprite) {
        ended.clear();
        animator.advance(dt, &ended);
        for (std::string& name : ended) finished.send(AnimationFinished{e, std::move(name)});
        showAnimatorFrame(animator, sprite, *sprites, *renderer);
    });
}

class SpriteAnimationPlugin : public Plugin {
public:
    void build(App& app) override {
        // The sheet handle is not reflected: sheets are made at run time and
        // have no file to save.
        reflect::type<AnimationClip>("AnimationClip")
            .field("frames", &AnimationClip::frames)
            .field("seconds", &AnimationClip::seconds)
            .field("looping", &AnimationClip::looping);
        reflect::type<NamedClip>("NamedClip")
            .field("name", &NamedClip::name)
            .field("clip", &NamedClip::clip);
        reflect::component<SpriteAnimator>(app.world(), "SpriteAnimator")
            .field("clips", &SpriteAnimator::clips)
            .field("current", &SpriteAnimator::current)
            .field("then", &SpriteAnimator::then)
            .field("once", &SpriteAnimator::once)
            .field("speed", &SpriteAnimator::speed)
            .field("playing", &SpriteAnimator::playing)
            .field("frame", &SpriteAnimator::frame)
            .field("frameTime", &SpriteAnimator::frameTime)
            .field("ended", &SpriteAnimator::ended);
        app.initEvent<AnimationFinished>();
        app.addSystem<spriteAnimationUpdate>("sprite_animation_update", Phase::PostUpdate);
    }
    DRIFT_PLUGIN(SpriteAnimationPlugin)
};

} // namespace drift
