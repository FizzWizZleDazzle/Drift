#include <drift/math/Noise.hpp>
#include <drift/math/Random.hpp>

#include <cmath>

namespace drift {

namespace {

inline float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
inline float mix(float a, float b, float t) { return a + (b - a) * t; }
inline int fastFloor(float v) { int i = static_cast<int>(v); return v < static_cast<float>(i) ? i - 1 : i; }

// Eight gradient directions (unit length after the 1/sqrt2 diagonal scale).
constexpr float kGradX[8] = {1.f, -1.f, 0.f, 0.f, 0.70710678f, -0.70710678f, 0.70710678f, -0.70710678f};
constexpr float kGradY[8] = {0.f, 0.f, 1.f, -1.f, 0.70710678f, 0.70710678f, -0.70710678f, -0.70710678f};

} // namespace

Noise::Noise(uint32_t seed) { reseed(seed); }

void Noise::reseed(uint32_t seed) {
    seed_ = seed;
    uint8_t p[256];
    for (int i = 0; i < 256; ++i) p[i] = static_cast<uint8_t>(i);
    Rng rng(seed);
    rng.shuffle(std::span<uint8_t>(p, 256));
    for (int i = 0; i < 512; ++i) perm_[i] = p[i & 255];
}

float Noise::value(float x, float y) const {
    int xi = fastFloor(x), yi = fastFloor(y);
    float fx = x - static_cast<float>(xi), fy = y - static_cast<float>(yi);
    int X = xi & 255, Y = yi & 255;
    auto lattice = [&](int ix, int iy) {
        return static_cast<float>(perm_[perm_[ix] + iy]) * (2.f / 255.f) - 1.f;
    };
    float u = fade(fx), v = fade(fy);
    float a = mix(lattice(X, Y), lattice(X + 1, Y), u);
    float b = mix(lattice(X, Y + 1), lattice(X + 1, Y + 1), u);
    return mix(a, b, v);
}

float Noise::perlin(float x, float y) const {
    int xi = fastFloor(x), yi = fastFloor(y);
    float fx = x - static_cast<float>(xi), fy = y - static_cast<float>(yi);
    int X = xi & 255, Y = yi & 255;
    auto grad = [&](int ix, int iy, float dx, float dy) {
        int h = perm_[perm_[ix] + iy] & 7;
        return kGradX[h] * dx + kGradY[h] * dy;
    };
    float u = fade(fx), v = fade(fy);
    float a = mix(grad(X, Y, fx, fy), grad(X + 1, Y, fx - 1.f, fy), u);
    float b = mix(grad(X, Y + 1, fx, fy - 1.f), grad(X + 1, Y + 1, fx - 1.f, fy - 1.f), u);
    // Unit gradients give a range of +-sqrt(0.5); scale to +-1.
    float r = mix(a, b, v) * 1.41421356f;
    return r < -1.f ? -1.f : (r > 1.f ? 1.f : r);
}

float Noise::simplex(float x, float y) const {
    constexpr float F2 = 0.36602540378f;   // (sqrt(3) - 1) / 2
    constexpr float G2 = 0.21132486540f;   // (3 - sqrt(3)) / 6
    float s = (x + y) * F2;
    int i = fastFloor(x + s), j = fastFloor(y + s);
    float t = static_cast<float>(i + j) * G2;
    float x0 = x - (static_cast<float>(i) - t);
    float y0 = y - (static_cast<float>(j) - t);
    int i1 = x0 > y0 ? 1 : 0;
    int j1 = x0 > y0 ? 0 : 1;
    float x1 = x0 - static_cast<float>(i1) + G2, y1 = y0 - static_cast<float>(j1) + G2;
    float x2 = x0 - 1.f + 2.f * G2, y2 = y0 - 1.f + 2.f * G2;
    int ii = i & 255, jj = j & 255;

    auto corner = [&](int gi, float dx, float dy) {
        float tt = 0.5f - dx * dx - dy * dy;
        if (tt < 0.f) return 0.f;
        tt *= tt;
        int h = gi & 7;
        return tt * tt * (kGradX[h] * dx + kGradY[h] * dy);
    };
    float n0 = corner(perm_[ii + perm_[jj]], x0, y0);
    float n1 = corner(perm_[ii + i1 + perm_[jj + j1]], x1, y1);
    float n2 = corner(perm_[ii + 1 + perm_[jj + 1]], x2, y2);
    // Scale the sum to roughly [-1, 1] for unit gradients.
    float r = 99.2f * (n0 + n1 + n2);
    return r < -1.f ? -1.f : (r > 1.f ? 1.f : r);
}

float Noise::sample(NoiseType type, float x, float y) const {
    switch (type) {
        case NoiseType::Value:   return value(x, y);
        case NoiseType::Perlin:  return perlin(x, y);
        case NoiseType::Simplex: return simplex(x, y);
    }
    return 0.f;
}

float Noise::fbm(NoiseType type, float x, float y, int octaves, float lacunarity, float gain) const {
    float sum = 0.f, amp = 1.f, freq = 1.f, norm = 0.f;
    for (int o = 0; o < octaves; ++o) {
        // Offset each octave so lattice points do not line up.
        float off = static_cast<float>(o) * 17.31f;
        sum += amp * sample(type, x * freq + off, y * freq - off);
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

} // namespace drift
