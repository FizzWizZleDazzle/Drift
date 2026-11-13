#pragma once

#include <drift/Types.hpp>
#include <drift/Handle.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace drift {

// Frames of a spritesheet played over time. `seconds` holds one duration per
// frame, or a single duration that times every frame.
struct AnimationClip {
    SpritesheetHandle sheet;
    std::vector<int32_t> frames;    // frame indices into the sheet
    std::vector<float> seconds;     // per frame, or one value for all of them
    bool looping = true;

    // How long frame `i` of the clip shows.
    float frameSeconds(size_t i) const;
    // The whole clip, in seconds.
    float length() const;
    // The same clip, played one time instead of looping.
    AnimationClip once() const;
};

// A clip with every frame shown for `seconds`.
AnimationClip clip(SpritesheetHandle sheet, std::vector<int32_t> frames, float seconds);
// A clip with one duration per frame. A list whose length does not match the
// frames logs an error.
AnimationClip clip(SpritesheetHandle sheet, std::vector<int32_t> frames, std::vector<float> seconds);

struct NamedClip {
    std::string name;
    AnimationClip clip;
};

// Sent when a clip plays to its end: a clip that does not loop, or a clip
// started with playOnce.
struct AnimationFinished {
    EntityId entity = 0;
    std::string clip;
};

// Plays named clips on the entity's Sprite, setting its texture and frame
// region.
struct SpriteAnimator {
    std::vector<NamedClip> clips;
    std::string current;       // the clip playing
    std::string then;          // played when a one-shot ends; empty holds the last frame
    bool once = false;         // the current clip plays one time, even a looping one
    float speed = 1.f;         // time multiplier; 0 freezes the clip
    bool playing = true;       // false pauses on the current frame
    int32_t frame = 0;         // index into the current clip's frames
    float frameTime = 0.f;     // seconds into the current frame
    std::string ended;         // the last clip that played to its end

    const AnimationClip* find(std::string_view name) const;
    // The clip names, comma separated, for error messages.
    std::string clipNames() const;

    // Switches to a clip. Playing the clip that is already playing changes
    // nothing. An unknown name logs an error naming the known clips and
    // returns false.
    bool play(const std::string& name);
    // Plays a clip one time, then switches to `then`, or holds the last frame
    // when `then` is empty.
    bool playOnce(const std::string& name, const std::string& then = {});
    void pause() { playing = false; }
    void resume() { playing = true; }
    // Whether `name` played to its end and no other clip has been started
    // since.
    bool finished(std::string_view name) const { return !name.empty() && ended == name; }

    // Seconds into the current clip.
    float time() const;
    // Moves to `t` seconds into the current clip: wrapped for a looping clip,
    // clamped for one that plays once.
    void seek(float t);

    // Moves the clip on by `dt` seconds times `speed`, and appends the name
    // of every clip that reached its end to `endedOut`.
    void advance(float dt, std::vector<std::string>* endedOut = nullptr);
};

} // namespace drift
