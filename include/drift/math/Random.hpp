#pragma once

// Seeded random number generator (PCG32). The same seed produces the same
// sequence on every platform, so it suits procedural generation, replays
// and tests. Not for cryptography.

#include <drift/Types.hpp>
#include <drift/math/Vector.hpp>

#include <cmath>
#include <cstdint>
#include <span>
#include <utility>

namespace drift {

class Rng {
public:
    explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL, uint64_t stream = 0xda3e39cb94b95bdbULL) {
        reseed(seed, stream);
    }

    void reseed(uint64_t seed, uint64_t stream = 0xda3e39cb94b95bdbULL) {
        state_ = 0;
        inc_ = (stream << 1u) | 1u;
        nextU32();
        state_ += seed;
        nextU32();
    }

    uint32_t nextU32() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }

    uint64_t nextU64() { return (uint64_t(nextU32()) << 32) | nextU32(); }

    // Uniform float in [0, 1).
    float next01() { return static_cast<float>(nextU32() >> 8) * (1.f / 16777216.f); }

    // Uniform integer below `bound` (bound > 0), without modulo bias.
    uint32_t below(uint32_t bound) {
        if (bound == 0) return 0;
        uint32_t threshold = (0u - bound) % bound;
        for (;;) {
            uint32_t r = nextU32();
            if (r >= threshold) return r % bound;
        }
    }

    // Uniform integer in [lo, hi], both inclusive.
    int32_t range(int32_t lo, int32_t hi) {
        if (hi < lo) std::swap(lo, hi);
        uint32_t span = static_cast<uint32_t>(int64_t(hi) - int64_t(lo) + 1);
        if (span == 0) return static_cast<int32_t>(nextU32());   // full 32-bit range
        return static_cast<int32_t>(int64_t(lo) + below(span));
    }

    // Uniform float in [lo, hi).
    float range(float lo, float hi) { return lo + (hi - lo) * next01(); }

    // True with probability p.
    bool chance(float p) { return next01() < p; }

    // -1 or +1.
    float sign() { return (nextU32() & 1u) ? 1.f : -1.f; }

    float angle() { return next01() * Tau; }
    Vec2 unitVector() { return fromAngle(angle()); }
    // Uniform point inside a circle of the given radius.
    Vec2 insideCircle(float radius = 1.f) { return fromAngle(angle(), radius * std::sqrt(next01())); }
    Vec2 insideRect(const Rect& r) { return {range(r.x, r.x + r.w), range(r.y, r.y + r.h)}; }
    Color color(uint8_t alpha = 255) {
        uint32_t v = nextU32();
        return {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), alpha};
    }

    // Gaussian sample (Box-Muller).
    float normal(float mean = 0.f, float stddev = 1.f) {
        float u1 = 1.f - next01();   // (0, 1]
        float u2 = next01();
        return mean + stddev * std::sqrt(-2.f * std::log(u1)) * std::cos(Tau * u2);
    }

    // Uniformly chosen element. The span must not be empty.
    template<typename T>
    T& pick(std::span<T> items) { return items[below(static_cast<uint32_t>(items.size()))]; }

    // Index chosen with probability proportional to its weight. Negative
    // weights count as zero; returns -1 if all weights are zero.
    int32_t weightedIndex(std::span<const float> weights) {
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

    // Fisher-Yates shuffle in place.
    template<typename T>
    void shuffle(std::span<T> items) {
        for (size_t i = items.size(); i > 1; --i) {
            size_t j = below(static_cast<uint32_t>(i));
            std::swap(items[i - 1], items[j]);
        }
    }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 0;
};

} // namespace drift
