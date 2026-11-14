#pragma once

#include <drift/Types.hpp>

namespace drift {

struct CameraShake {
    float intensity = 0.f;      // current shake magnitude (pixels)
    float decay = 5.f;          // how fast intensity decays per second
    float frequency = 20.f;     // shake oscillation speed
    float elapsed = 0.f;
    Vec2 offset = {};           // displacement applied this frame, taken back next frame

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

// Keeps what the camera shows inside `area` (world units): the camera's
// position is held so its view rect stays within the area, and centered on
// an axis where the area is smaller than the view. Applies after
// CameraFollow and before CameraShake. Rotation is not taken into account.
struct CameraBounds {
    Rect area = {};
};

} // namespace drift
