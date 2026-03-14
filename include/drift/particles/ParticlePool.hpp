#pragma once

#include <drift/Types.hpp>
#include <cstdint>
#include <vector>

namespace drift {

struct ParticlePool {
    // SoA arrays — only [0..count) are active
    std::vector<Vec2> positions;
    std::vector<Vec2> velocities;
    std::vector<float> ages;
    std::vector<float> lifetimes;
    std::vector<float> rotations;
    std::vector<float> angularVelocities;
    std::vector<float> sizes;        // current interpolated size
    std::vector<float> sizeStarts;
    std::vector<float> sizeEnds;
    std::vector<ColorF> colorStarts;
    std::vector<ColorF> colorEnds;

    int32_t count = 0;
    int32_t capacity = 0;

    void reserve(int32_t cap) {
        if (cap <= capacity) return;
        positions.resize(cap);
        velocities.resize(cap);
        ages.resize(cap);
        lifetimes.resize(cap);
        rotations.resize(cap);
        angularVelocities.resize(cap);
        sizes.resize(cap);
        sizeStarts.resize(cap);
        sizeEnds.resize(cap);
        colorStarts.resize(cap);
        colorEnds.resize(cap);
        capacity = cap;
    }

    // O(1) append, returns index of new particle
    int32_t spawn() {
        if (count >= capacity) return -1;
        return count++;
    }

    // O(1) swap-remove with last active particle
    void kill(int32_t index) {
        if (index < 0 || index >= count) return;
        int32_t last = count - 1;
        if (index != last) {
            positions[index] = positions[last];
            velocities[index] = velocities[last];
            ages[index] = ages[last];
            lifetimes[index] = lifetimes[last];
            rotations[index] = rotations[last];
            angularVelocities[index] = angularVelocities[last];
            sizes[index] = sizes[last];
            sizeStarts[index] = sizeStarts[last];
            sizeEnds[index] = sizeEnds[last];
            colorStarts[index] = colorStarts[last];
            colorEnds[index] = colorEnds[last];
        }
        --count;
    }

    void clear() {
        count = 0;
    }
};

} // namespace drift
