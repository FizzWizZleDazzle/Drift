#pragma once

// Seeded 2D coherent noise: value, Perlin (gradient) and simplex, plus
// fractal (fBm) layering. Output is in [-1, 1]; the same seed gives the same
// field on every platform.

#include <cstdint>

namespace drift {

enum class NoiseType : uint8_t {
    Value,
    Perlin,
    Simplex,
};

class Noise {
public:
    explicit Noise(uint32_t seed = 0);

    void reseed(uint32_t seed);
    uint32_t seed() const { return seed_; }

    float value(float x, float y) const;
    float perlin(float x, float y) const;
    float simplex(float x, float y) const;
    float sample(NoiseType type, float x, float y) const;

    // Fractal Brownian motion: `octaves` layers, each `lacunarity` times the
    // frequency and `gain` times the amplitude of the previous one.
    // Normalized back to [-1, 1].
    float fbm(NoiseType type, float x, float y, int octaves = 4,
              float lacunarity = 2.f, float gain = 0.5f) const;

private:
    uint32_t seed_ = 0;
    uint8_t perm_[512] = {};
};

} // namespace drift
