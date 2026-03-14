#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/SpriteAnimator.hpp>
#include <drift/resources/Time.hpp>

namespace drift {

#ifndef SWIG
inline void spriteAnimationUpdate(Res<Time> time,
                                  QueryMut<SpriteAnimator, Sprite> animated) {
    float dt = time->delta;

    animated.iter([&](SpriteAnimator& animator, Sprite& sprite) {
        if (!animator.playing) return;
        if (animator.clips.empty()) return;
        if (animator.currentClip < 0 ||
            animator.currentClip >= static_cast<int32_t>(animator.clips.size())) return;

        auto& clip = animator.clips[animator.currentClip];
        if (clip.frames.empty()) return;

        animator.elapsed += dt;

        while (animator.elapsed >= clip.frameDuration) {
            animator.elapsed -= clip.frameDuration;
            animator.currentFrame++;

            if (animator.currentFrame >= static_cast<int32_t>(clip.frames.size())) {
                if (clip.looping) {
                    animator.currentFrame = 0;
                } else {
                    animator.currentFrame = static_cast<int32_t>(clip.frames.size()) - 1;
                    animator.playing = false;
                    break;
                }
            }
        }

        sprite.srcRect = clip.frames[animator.currentFrame];
    });
}
#endif

class SpriteAnimationPlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        app.world().registerComponent<SpriteAnimator>("SpriteAnimator");
        app.addSystem<spriteAnimationUpdate>("sprite_animation_update", Phase::PostUpdate);
#endif
    }
    DRIFT_PLUGIN(SpriteAnimationPlugin)
};

} // namespace drift
