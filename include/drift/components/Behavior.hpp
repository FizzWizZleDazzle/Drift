#pragma once

#include <drift/Types.hpp>

namespace drift {

// Despawns the entity once `seconds` have passed. The count goes down by
// the frame time each frame.
struct Lifetime {
    float seconds = 1.f;
};

// Moves the entity back and forth around where it is: each frame the
// position gets the change in amplitude * sin(2 pi frequency t + phase), so
// the motion adds to whatever else moves it.
struct Oscillate {
    Vec2 amplitude = {0.f, 8.f};   // pixels each way
    float frequency = 1.f;         // cycles per second
    float phase = 0.f;             // radians
    float elapsed = 0.f;           // seconds since it started
    Vec2 applied = {};             // offset already added to the position
};

// Moves the entity toward another one at `speed` pixels per second,
// stopping `distance` short of the target's position plus `offset`.
// Nothing happens while the target is missing.
struct Follow {
    EntityId target = InvalidEntityId;
    float speed = 100.f;
    float distance = 0.f;
    Vec2 offset = {};
};

} // namespace drift
