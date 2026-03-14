#pragma once

#include <drift/Resource.hpp>

#include <cstdint>

namespace drift {

struct Time : public Resource {
    DRIFT_RESOURCE(Time)

    float delta = 0.f;      // seconds since last frame
    double elapsed = 0.0;   // seconds since engine start
    uint64_t frame = 0;     // frame counter
};

} // namespace drift
