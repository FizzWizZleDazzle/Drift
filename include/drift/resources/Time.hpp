#pragma once

#include <drift/Resource.hpp>

#include <cstdint>

namespace drift {

struct Time : public Resource {
    DRIFT_RESOURCE(Time)

    float delta = 0.f;      // seconds since last frame, times `scale`
    float scale = 1.f;      // 0 freezes the game, 0.5 runs it at half speed
    float realDelta = 0.f;  // seconds since last frame, whatever the scale
    double elapsed = 0.0;   // seconds since engine start
    uint64_t frame = 0;     // frame counter
};

} // namespace drift
