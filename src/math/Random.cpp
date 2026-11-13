// The float helpers of Rng. This file is compiled without fused multiply-add
// (see CMakeLists.txt), so their results match on every platform.

#include <drift/math/Random.hpp>

#include <cmath>

namespace drift {

float Rng::range(float lo, float hi) { return lo + (hi - lo) * next01(); }

Vec2 Rng::unitVector() { return fromAngle(angle()); }

Vec2 Rng::insideCircle(float radius) { return fromAngle(angle(), radius * std::sqrt(next01())); }

float Rng::normal(float mean, float stddev) {
    float u1 = 1.f - next01();   // (0, 1]
    float u2 = next01();
    return mean + stddev * std::sqrt(-2.f * std::log(u1)) * std::cos(Tau * u2);
}

int32_t Rng::weightedIndex(std::span<const float> weights) {
    float total = 0.f;
    for (float w : weights) total += w > 0.f ? w : 0.f;
    if (total <= 0.f) return -1;
    float r = next01() * total;
    int32_t last = -1;
    for (size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] <= 0.f) continue;
        last = static_cast<int32_t>(i);
        if (r < weights[i]) return last;
        r -= weights[i];
    }
    return last;   // float rounding: fall back to the last positive weight
}

} // namespace drift
