#pragma once

#include <drift/Types.hpp>
#include <cstdint>

namespace drift {

struct TrailRenderer {
    float width = 4.f;
    float lifetime = 0.5f;          // seconds before trail point fades
    ColorF colorStart = {1, 1, 1, 1};
    ColorF colorEnd = {1, 1, 1, 0};
    float minDistance = 2.f;         // min distance before adding new point
    int32_t maxPoints = 64;
    float zOrder = -1.f;
};

} // namespace drift
