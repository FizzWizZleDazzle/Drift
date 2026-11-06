#pragma once

// Timers and cooldowns. Both are plain values: keep them in your own
// resource and tick them yourself, or put a Timer on an entity and let
// TimerPlugin tick it and send TimerFinished events.

#include <drift/Types.hpp>

#include <cstdint>

namespace drift {

enum class TimerMode : uint8_t {
    Once,      // finishes once, then stays finished until reset()
    Repeat,    // finishes every `duration` seconds
};

struct Timer {
    float duration = 1.f;
    TimerMode mode = TimerMode::Once;
    float elapsed = 0.f;
    bool paused = false;
    uint32_t tag = 0;            // free for game use; copied into TimerFinished
    uint32_t timesFinished = 0;  // how many times it finished during the last tick

    static Timer once(float seconds, uint32_t tag = 0) { return {seconds, TimerMode::Once, 0.f, false, tag}; }
    static Timer repeating(float seconds, uint32_t tag = 0) { return {seconds, TimerMode::Repeat, 0.f, false, tag}; }

    // Advance by dt; returns true if the timer finished during this tick.
    // A repeating timer can finish several times in one long tick; see
    // timesFinished.
    bool tick(float dt) {
        timesFinished = 0;
        if (paused || duration <= 0.f) return false;
        if (mode == TimerMode::Once) {
            if (elapsed >= duration) return false;   // already done
            elapsed += dt;
            if (elapsed >= duration) { elapsed = duration; timesFinished = 1; }
            return timesFinished > 0;
        }
        elapsed += dt;
        while (elapsed >= duration) {
            elapsed -= duration;
            ++timesFinished;
        }
        return timesFinished > 0;
    }

    // True for the tick in which the timer finished.
    bool justFinished() const { return timesFinished > 0; }
    // One-shot timers: true once the duration has passed.
    bool finished() const { return mode == TimerMode::Once && elapsed >= duration; }
    float remaining() const { return duration > elapsed ? duration - elapsed : 0.f; }
    // 0 at the start, 1 at the end of the current period.
    float fraction() const { return duration > 0.f ? elapsed / duration : 1.f; }
    void reset() { elapsed = 0.f; timesFinished = 0; }
};

// Sent by TimerPlugin when an entity's Timer finishes (once per finish).
struct TimerFinished {
    EntityId entity = InvalidEntityId;
    uint32_t tag = 0;
};

// A cooldown: ready() until used, then blocked for `duration` seconds.
struct Cooldown {
    float duration = 1.f;
    float remaining = 0.f;

    explicit Cooldown(float seconds = 1.f) : duration(seconds) {}

    void tick(float dt) { remaining = remaining > dt ? remaining - dt : 0.f; }
    bool ready() const { return remaining <= 0.f; }
    // Use it if ready: returns true and starts the cooldown.
    bool tryUse() {
        if (!ready()) return false;
        remaining = duration;
        return true;
    }
    void reset() { remaining = 0.f; }
    // 0 when ready, 1 right after use.
    float fraction() const { return duration > 0.f ? remaining / duration : 0.f; }
};

} // namespace drift
