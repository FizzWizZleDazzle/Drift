#pragma once

// Seeded random number generator (PCG32). The same seed produces the same
// sequence on every platform, so it suits procedural generation, replays
// and tests. Not for cryptography.
//
// The integer outputs, next01(), angle() and range(float) are identical on
// every platform. unitVector(), insideCircle() and normal() go through
// the platform's sin, cos and log, whose last bits can differ between math
// libraries; avoid them where two platforms must agree bit for bit.

#include <drift/Types.hpp>
#include <drift/math/Vector.hpp>

#include <cstdint>
#include <span>
#include <string_view>
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

    // The generator's position, to save and resume a sequence.
    uint64_t state() const { return state_; }
    uint64_t increment() const { return inc_; }
    // A generator that continues from a saved state() and increment().
    static Rng fromState(uint64_t state, uint64_t increment) {
        Rng r;
        r.state_ = state;
        r.inc_ = increment | 1u;
        return r;
    }

    // A 64-bit seed from text (FNV-1a), for seeds a player types or
    // generators named by purpose: Rng(Rng::hash("caves")).
    static constexpr uint64_t hash(std::string_view text) {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (char c : text) {
            h ^= static_cast<uint8_t>(c);
            h *= 0x100000001b3ULL;
        }
        return h;
    }

    // A new generator seeded from this one's next outputs: an independent
    // sequence for a sub-task, still reproducible from the first seed.
    Rng split() {
        uint64_t seed = nextU64();
        uint64_t stream = nextU64();
        return Rng(seed, stream);
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
    float range(float lo, float hi);

    // True with probability p.
    bool chance(float p) { return next01() < p; }

    // -1 or +1.
    float sign() { return (nextU32() & 1u) ? 1.f : -1.f; }

    float angle() { return next01() * Tau; }
    Vec2 unitVector();
    // Uniform point inside a circle of the given radius.
    Vec2 insideCircle(float radius = 1.f);
    Vec2 insideRect(const Rect& r) { return {range(r.x, r.x + r.w), range(r.y, r.y + r.h)}; }
    Color color(uint8_t alpha = 255) {
        uint32_t v = nextU32();
        return {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), alpha};
    }

    // Gaussian sample (Box-Muller).
    float normal(float mean = 0.f, float stddev = 1.f);

    // Uniformly chosen element. The span must not be empty.
    template<typename T>
    T& pick(std::span<T> items) { return items[below(static_cast<uint32_t>(items.size()))]; }

    // Index chosen with probability proportional to its weight. Negative
    // weights count as zero; returns -1 if all weights are zero.
    int32_t weightedIndex(std::span<const float> weights);

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
