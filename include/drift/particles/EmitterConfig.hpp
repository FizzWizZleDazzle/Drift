#pragma once

#include <drift/Types.hpp>
#include <drift/Handle.hpp>
#include <cstdint>
#include <vector>

namespace drift {

enum class EmissionShape { Point, Circle, Ring, Box, Line };
enum class ParticleSpace { World, Local };
enum class ParticleBlendMode { Alpha, Additive };

struct ValueRange {
    float min = 0.f, max = 0.f;

    ValueRange() = default;
    ValueRange(float v) : min(v), max(v) {}
    ValueRange(float lo, float hi) : min(lo), max(hi) {}

    // Sample using LCG RNG (caller passes state by reference)
    float sample(uint32_t& rng) const {
        rng = rng * 1664525u + 1013904223u;
        float t = static_cast<float>(rng & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
        return min + t * (max - min);
    }
};

struct BurstEntry {
    float time = 0.f;          // seconds into emitter lifecycle
    int32_t count = 10;
    int32_t cycles = 1;        // 0 = infinite
    float interval = 0.f;      // between cycles
};

struct EmitterConfig {
    // Texture
    TextureHandle texture;
    Rect srcRect = {};

    // Emission
    EmissionShape shape = EmissionShape::Point;
    float shapeRadius = 0.f;        // Circle/Ring
    Vec2 shapeExtents = {};          // Box half-size / Line direction
    float spawnRate = 10.f;
    int32_t maxParticles = 256;
    std::vector<BurstEntry> bursts;

    // Particle lifetime
    ValueRange lifetime = {0.5f, 1.5f};

    // Initial motion
    ValueRange speed = {10.f, 50.f};
    ValueRange angle = {0.f, 6.2831853f};
    float velocityInheritance = 0.f;  // 0-1, fraction of emitter velocity added to particle

    // Forces
    Vec2 gravity = {0.f, 0.f};
    float drag = 0.f;               // velocity *= (1 - drag * dt)

    // Size over lifetime (lerp from start to end)
    ValueRange sizeStart = {4.f, 4.f};
    ValueRange sizeEnd = {1.f, 1.f};

    // Rotation
    ValueRange initialRotation = {0.f, 0.f};
    ValueRange angularVelocity = {0.f, 0.f};

    // Color over lifetime
    ColorF colorStart = {1, 1, 1, 1};
    ColorF colorEnd = {1, 1, 1, 0};

    // Rendering
    ParticleBlendMode blendMode = ParticleBlendMode::Alpha;
    float zOrder = 10.f;

    // Duration & looping
    float duration = -1.f;           // -1 = infinite
    bool looping = true;

    // Space
    ParticleSpace space = ParticleSpace::World;

    // Pre-warm: simulate this many seconds at startup
    float preWarmTime = 0.f;

    // Bounds clamping (particles stick to walls)
    // If boundsMin == boundsMax, bounds are disabled
    Vec2 boundsMin = {};
    Vec2 boundsMax = {};

    // RNG seed (0 = random)
    uint32_t seed = 0;
};

} // namespace drift
