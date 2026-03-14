#pragma once

#include <drift/Types.hpp>
#include <cstdint>
#include <vector>

namespace drift {

struct AnimationClip {
    std::vector<Rect> frames;       // srcRect per frame
    float frameDuration = 0.1f;     // seconds per frame
    bool looping = true;
};

struct SpriteAnimator {
    std::vector<AnimationClip> clips;
    int32_t currentClip = 0;
    int32_t currentFrame = 0;
    float elapsed = 0.f;
    bool playing = true;

    void play(int32_t clipIndex) {
        if (clipIndex >= 0 && clipIndex < static_cast<int32_t>(clips.size())) {
            currentClip = clipIndex;
            currentFrame = 0;
            elapsed = 0.f;
            playing = true;
        }
    }

    void stop() {
        playing = false;
    }

    bool isFinished() const {
        if (clips.empty() || currentClip < 0 ||
            currentClip >= static_cast<int32_t>(clips.size())) return true;
        const auto& clip = clips[currentClip];
        if (clip.looping) return false;
        return currentFrame >= static_cast<int32_t>(clip.frames.size()) - 1 &&
               elapsed >= clip.frameDuration;
    }
};

} // namespace drift
