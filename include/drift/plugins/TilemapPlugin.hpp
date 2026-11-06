#pragma once

#include <drift/Plugin.hpp>
#include <drift/components/Tilemap.hpp>
#include <drift/resources/TilemapResource.hpp>

namespace drift {

// Adds TilemapResource and the Tilemap component. Tilemap entities are
// drawn every frame; on spawn they get static colliders (when a
// PhysicsResource exists) and run the object spawners.
class TilemapPlugin : public Plugin {
public:
    void build(App& app) override;
    DRIFT_PLUGIN(TilemapPlugin)
};

} // namespace drift
