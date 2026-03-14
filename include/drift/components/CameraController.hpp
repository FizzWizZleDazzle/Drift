#pragma once

#include <drift/Types.hpp>

namespace drift {

struct CameraShake {
    float intensity = 0.f;      // current shake magnitude (pixels)
    float decay = 5.f;          // how fast intensity decays per second
    float frequency = 20.f;     // shake oscillation speed
    float elapsed = 0.f;

    void trigger(float mag) {
        intensity = mag;
        elapsed = 0.f;
    }
};

struct CameraFollow {
    EntityId target = InvalidEntityId;
    float smoothing = 5.f;      // lerp speed (higher = snappier)
    Vec2 offset = {};           // offset from target
    Vec2 deadZone = {};         // don't move if target is within this range
};

} // namespace drift
