#include <drift/components/SpriteAnimator.hpp>
#include <drift/Log.hpp>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

// A frame shorter than this counts as this long, so a zero duration cannot
// spin the advance loop.
constexpr float kShortestFrame = 0.001f;

} // namespace

float AnimationClip::frameSeconds(size_t i) const {
    if (seconds.empty()) return 0.1f;
    float s = seconds.size() == 1 ? seconds[0] : seconds[std::min(i, seconds.size() - 1)];
    return std::max(s, kShortestFrame);
}

float AnimationClip::length() const {
    float total = 0.f;
    for (size_t i = 0; i < frames.size(); ++i) total += frameSeconds(i);
    return total;
}

AnimationClip AnimationClip::once() const {
    AnimationClip c = *this;
    c.looping = false;
    return c;
}

AnimationClip clip(SpritesheetHandle sheet, std::vector<int32_t> frames, float seconds) {
    AnimationClip c;
    c.sheet = sheet;
    c.frames = std::move(frames);
    c.seconds = {seconds};
    return c;
}

AnimationClip clip(SpritesheetHandle sheet, std::vector<int32_t> frames, std::vector<float> seconds) {
    if (seconds.size() != 1 && seconds.size() != frames.size()) {
        DRIFT_LOG_ERROR("clip: %d frames but %d durations; give one duration or one per frame",
                        static_cast<int>(frames.size()), static_cast<int>(seconds.size()));
    }
    AnimationClip c;
    c.sheet = sheet;
    c.frames = std::move(frames);
    c.seconds = std::move(seconds);
    return c;
}

const AnimationClip* SpriteAnimator::find(std::string_view name) const {
    for (const NamedClip& c : clips) {
        if (c.name == name) return &c.clip;
    }
    return nullptr;
}

std::string SpriteAnimator::clipNames() const {
    std::string out;
    for (const NamedClip& c : clips) {
        if (!out.empty()) out += ", ";
        out += c.name;
    }
    return out.empty() ? std::string("none") : out;
}

bool SpriteAnimator::play(const std::string& name) {
    if (!find(name)) {
        DRIFT_LOG_ERROR("SpriteAnimator: no clip \"%s\" (clips: %s)", name.c_str(), clipNames().c_str());
        return false;
    }
    if (name == current && ended != name) return true;
    current = name;
    then.clear();
    once = false;
    frame = 0;
    frameTime = 0.f;
    ended.clear();
    playing = true;
    return true;
}

bool SpriteAnimator::playOnce(const std::string& name, const std::string& next) {
    if (!find(name)) {
        DRIFT_LOG_ERROR("SpriteAnimator: no clip \"%s\" (clips: %s)", name.c_str(), clipNames().c_str());
        return false;
    }
    if (!next.empty() && !find(next)) {
        DRIFT_LOG_ERROR("SpriteAnimator: no clip \"%s\" (clips: %s)", next.c_str(), clipNames().c_str());
        return false;
    }
    current = name;
    then = next;
    once = true;
    frame = 0;
    frameTime = 0.f;
    ended.clear();
    playing = true;
    return true;
}

float SpriteAnimator::time() const {
    const AnimationClip* c = find(current);
    if (!c) return 0.f;
    float t = frameTime;
    for (int32_t i = 0; i < frame && i < static_cast<int32_t>(c->frames.size()); ++i) t += c->frameSeconds(i);
    return t;
}

void SpriteAnimator::seek(float t) {
    const AnimationClip* c = find(current);
    if (!c || c->frames.empty()) return;
    float length = c->length();
    t = std::max(t, 0.f);
    if (c->looping && !once) {
        t = std::fmod(t, length);
    } else if (t >= length) {
        frame = static_cast<int32_t>(c->frames.size()) - 1;
        frameTime = c->frameSeconds(frame);
        return;
    }
    frame = 0;
    while (frame + 1 < static_cast<int32_t>(c->frames.size()) && t >= c->frameSeconds(frame)) {
        t -= c->frameSeconds(frame);
        ++frame;
    }
    frameTime = t;
}

void SpriteAnimator::advance(float dt, std::vector<std::string>* endedOut) {
    if (!playing || speed <= 0.f) return;
    const AnimationClip* c = find(current);
    if (!c || c->frames.empty()) return;
    // A one-shot that ended holds its last frame until something else plays.
    if (ended == current) return;

    frameTime += dt * speed;
    for (;;) {
        int32_t count = static_cast<int32_t>(c->frames.size());
        frame = std::clamp(frame, 0, count - 1);
        float d = c->frameSeconds(frame);
        if (frameTime < d) return;
        if (frame + 1 < count) {
            frameTime -= d;
            ++frame;
            continue;
        }
        if (c->looping && !once) {
            frameTime -= d;
            frame = 0;
            continue;
        }
        // The clip played to its end.
        ended = current;
        if (endedOut) endedOut->push_back(current);
        const AnimationClip* next = then.empty() ? nullptr : find(then);
        if (!next || next->frames.empty()) {
            frameTime = d;
            return;
        }
        frameTime -= d;
        current = then;
        then.clear();
        once = false;
        frame = 0;
        c = next;
    }
}

} // namespace drift
