#pragma once

#include <drift/particles/EmitterConfig.hpp>

namespace drift {

struct ParticleEmitter {
    EmitterConfig config;
    bool playing = true;
    bool oneShot = false;       // auto-destroy entity when finished
};

} // namespace drift
