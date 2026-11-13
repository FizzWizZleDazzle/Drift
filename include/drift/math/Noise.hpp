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

// A complete noise recipe, for sample(options, x, y): the type, the
// feature scale, fractal layering and domain warp in one value.
struct NoiseOptions {
    NoiseType type = NoiseType::Simplex;
    // Multiplies the input coordinates: 0.05 gives features about 20 units
    // across.
    float frequency = 1.f;
    int octaves = 1;
    float lacunarity = 2.f;
    float gain = 0.5f;
    // Domain warp: moves each sample point by up to this many units
    // (before `frequency`) along a second noise field, which bends straight
    // features into swirls. 0 turns it off.
    float warp = 0.f;
    // Frequency of the warp field relative to `frequency`.
    float warpFrequency = 1.f;
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

    // The point (x, y) displaced by up to `strength` units along two
    // independent fBm fields of `octaves` layers, sampled at `frequency`.
    // Feed the result to any sampling function for domain-warped noise.
    void warp(float& x, float& y, float strength, float frequency = 1.f,
              NoiseType type = NoiseType::Simplex, int octaves = 2) const;

    // Samples with every option applied: warp, then fBm at `frequency`.
    // In [-1, 1].
    float sample(const NoiseOptions& options, float x, float y) const;

private:
    uint32_t seed_ = 0;
    uint8_t perm_[512] = {};
};

} // namespace drift
