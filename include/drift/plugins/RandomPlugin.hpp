#pragma once

#include <drift/App.hpp>
#include <drift/Log.hpp>
#include <drift/Plugin.hpp>
#include <drift/math/Random.hpp>

#include <chrono>
#include <cstdint>
#include <string_view>

namespace drift {

// The [random] section of drift.toml.
struct RandomSettings : public Resource {
    DRIFT_RESOURCE(RandomSettings)

    // The game's seed. 0 picks a new one each run (and logs it, so a run
    // can be replayed by setting it here or with --set random.seed=N).
    int64_t seed = 0;

    static void describe(reflect::TypeBuilder<RandomSettings>& t) { t.field("seed", &RandomSettings::seed); }
};

// One seed for the whole game. `rng` is the shared generator; stream(name)
// gives each purpose its own generator, so rolling more loot does not
// change the next level.
//
//   Rng levelRng = random->stream("level-1");
//   Grid cave = generateCave(levelRng, 80, 50);
class Random : public Resource {
public:
    DRIFT_RESOURCE(Random)

    explicit Random(uint64_t seed = 1) { reseed(seed); }

    uint64_t seed() const { return seed_; }
    // Starts `rng` and every later stream over from a new seed.
    void reseed(uint64_t seed) {
        seed_ = seed;
        rng = Rng(seed, Rng::hash("drift"));
    }

    // A generator for one purpose: the same seed and name always give the
    // same sequence, independent of `rng` and of other names.
    Rng stream(std::string_view name) const { return Rng(seed_ ^ Rng::hash(name), Rng::hash(name)); }

    Rng rng;

private:
    uint64_t seed_ = 1;
};

class RandomPlugin : public Plugin {
public:
    void build(App& app) override {
        auto& settings = app.settings<RandomSettings>("random", {.restart = {"seed"}});
        uint64_t seed = static_cast<uint64_t>(settings.seed);
        if (seed == 0) {
            seed = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
            seed = (Rng(seed).nextU32() & 0x7fffffffu) | 1u;   // a short positive number to type back in
            DRIFT_LOG_INFO("Random seed %llu (set [random] seed to replay it)", static_cast<unsigned long long>(seed));
        }
        app.addResource<Random>(seed);
    }
    DRIFT_PLUGIN(RandomPlugin)
};

} // namespace drift
