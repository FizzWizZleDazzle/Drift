#pragma once

#include <drift/Resource.hpp>
#include <drift/Types.hpp>
#include <drift/particles/ParticlePool.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace drift {

// Internal emitter state (per entity)
struct EmitterState {
    ParticlePool pool;
    uint32_t rng = 12345u;
    float spawnAccumulator = 0.f;
    float elapsed = 0.f;
    bool finished = false;
    std::vector<int32_t> burstCyclesDone;
    std::vector<float> burstTimers;
    Vec2 prevPosition = {};
    bool hasPrevPosition = false;
};

struct ParticleSystemResource : public Resource {
    DRIFT_RESOURCE(ParticleSystemResource)

    std::unordered_map<EntityId, EmitterState> emitters;
};

} // namespace drift
