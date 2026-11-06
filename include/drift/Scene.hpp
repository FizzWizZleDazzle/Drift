#pragma once

// Scenes and prefabs: entities saved to JSON through component reflection,
// and JSON files spawned back into the world any number of times.

#include <drift/Resource.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace drift {

class App;
class Commands;

struct SceneTag {};
using SceneHandle = Handle<SceneTag>;

struct SceneSaveOptions {
    // Entities to save. Empty = every entity that has a reflected component.
    std::vector<EntityId> entities;
    // With `entities` set, also save their descendants (through Children).
    bool includeChildren = true;
    // Optional: return false to leave an entity out.
    std::function<bool(EntityId)> filter;
};

struct SceneSpawnOptions {
    // Root entities of the scene become children of this entity.
    EntityId parent = InvalidEntityId;
    // Added to the Transform2D position of the scene's root entities.
    Vec2 offset = {};
    // Per-entity component patches, keyed by the entity's Name or its id in
    // the file: {"door": {"Sprite": {"tint": [255, 0, 0, 255]}}}.
    // Patches merge into the saved values; null removes a component.
    nlohmann::json overrides;
};

// The entities one spawn created, in file order.
struct SceneInstance {
    std::vector<EntityId> entities;
    std::vector<EntityId> roots;     // entities without a parent inside the scene
    std::vector<std::string> names;  // Name of each entity ("" when it has none)

    // First entity with this Name, or InvalidEntityId.
    EntityId find(const char* name) const;
};

class SceneResource : public Resource {
public:
    explicit SceneResource(App& app);
    ~SceneResource() override;

    DRIFT_RESOURCE(SceneResource)
    bool mainThreadOnly() const override { return true; }

    // ---- Saving ----
    // Reads the world directly: call from an exclusive system
    // (app.addSystem(name, phase, [](App&) {...})) or from startup.
    nlohmann::json serialize(const SceneSaveOptions& options = {}) const;
    // The saved file becomes a loaded scene with the saved entities as its
    // instance, so reloading the file (or hot reload) updates them in place.
    bool save(const char* path, const SceneSaveOptions& options = {});

    // ---- Loading ----
    // Loads and caches a scene file; loading the same path returns the same
    // handle. File scenes are tracked by the AssetServer, so hot reload
    // picks up edits.
    SceneHandle load(const char* path);
    // A scene built in code or received from elsewhere.
    SceneHandle create(nlohmann::json document);
    const nlohmann::json* document(SceneHandle scene) const;
    // Re-read the file and update every live instance in place: entities
    // keep their ids, components follow the file.
    bool reload(SceneHandle scene);

    // ---- Spawning ----
    // Entity ids are allocated now; components arrive when the commands
    // flush (end of the current phase).
    SceneInstance spawn(Commands& commands, SceneHandle scene,
                        const SceneSpawnOptions& options = {});
    // Spawns immediately. Only from exclusive systems or startup.
    SceneInstance spawnNow(SceneHandle scene, const SceneSpawnOptions& options = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace drift
