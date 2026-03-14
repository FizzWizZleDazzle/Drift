#pragma once

#include <drift/Resource.hpp>
#include <drift/World.hpp>

namespace drift {

// Non-owning Resource wrapper around World so systems can inject it via Res<WorldResource>.
class WorldResource : public Resource {
public:
    explicit WorldResource(World& world) : world_(world) {}

    DRIFT_RESOURCE(WorldResource)

    World& world() { return world_; }
    const World& world() const { return world_; }

private:
    World& world_;
};

} // namespace drift
