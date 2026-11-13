# Scenes and Prefabs

A scene is a JSON file of entities and their components. Save part or all of the world to one, load it back later, or spawn the same file many times as a prefab with per-copy changes. Scenes read and write components through [reflection](../concepts/reflection.md), so any described component, engine or game, is saved.

**Header:** `#include <drift/Scene.hpp>`

**Plugin:** `ScenePlugin` adds `SceneResource`. It is part of `DefaultPlugins`.

## File Format

```json
{
  "drift_scene": 1,
  "entities": [
    {
      "id": 0,
      "components": {
        "Name": { "value": "Player" },
        "Transform2D": { "position": [100, 200] },
        "Sprite": { "texture": "assets/player.png", "origin": [16, 16] }
      }
    },
    {
      "id": 1,
      "components": {
        "Name": { "value": "Sword" },
        "Parent": { "entity": 0 },
        "Transform2D": { "position": [20, 0] },
        "GlobalTransform2D": {}
      }
    }
  ]
}
```

- `id` identifies an entity inside the file. Entity references (`Parent`, `CameraFollow::target`, any `.entity` field) use these ids and are remapped to the new entities on spawn.
- Components are keyed by their reflected name. Fields you leave out keep their defaults, so hand-written files stay short.
- Asset fields hold the file path. Spawning loads it through the `AssetServer`, which loads each path once.
- `Children` is not stored. Spawning rebuilds it from `Parent`. For transforms to follow the parent, give parent and children a `GlobalTransform2D`, as described in [Hierarchy](../components/hierarchy.md).

## Saving

```cpp
app.addSystem("save_level", Phase::Update, [](App& a) {
    if (a.getResource<InputResource>()->keyPressed(Key::F5)) {
        a.getResource<SceneResource>()->save("level.json");
    }
});
```

```cpp
nlohmann::json serialize(const SceneSaveOptions& options = {}) const;
bool save(const char* path, const SceneSaveOptions& options = {});
```

By default every entity with a described component is saved. `SceneSaveOptions` narrows that:

| Field | Meaning |
|-------|---------|
| `entities` | Save only these entities (and their descendants while `includeChildren` is true). |
| `includeChildren` | Follow `Children` from the listed entities. Default `true`. |
| `filter` | Return `false` to leave an entity out. |

A `Parent` that points outside the saved set is dropped, so the entity becomes a root of the file. Saving `{.entities = {door}}` writes a prefab of the door and everything under it.

Saving reads the world directly. Call it from an exclusive system (a lambda taking `App&`) or a startup system, not from a typed system that may run in parallel with others.

The saved file becomes a loaded scene, and the saved entities become its instance: when the file is reloaded later, those entities update in place.

## Loading and Spawning

```cpp
SceneHandle load(const char* path);
SceneHandle create(nlohmann::json document);
SceneInstance spawn(Commands& commands, SceneHandle scene, const SceneSpawnOptions& options = {});
SceneInstance spawnNow(SceneHandle scene, const SceneSpawnOptions& options = {});
```

`load` reads and caches a file; loading the same path again returns the same handle. `create` makes a scene from JSON built in code.

`spawn` allocates the entities at once and adds their components when the commands flush, so it works in any system with `Commands&`. `spawnNow` adds everything immediately; use it in exclusive systems and startup.

```cpp
void setup(ResMut<SceneResource> scenes, Commands& cmd) {
    SceneHandle level = scenes->load("assets/level.json");
    SceneInstance inst = scenes->spawn(cmd, level);
    EntityId player = inst.find("Player");   // by Name
}
```

`SceneInstance` lists the spawned `entities` in file order, the `roots` (entities with no parent inside the file), and the `names`; `find(name)` returns the first entity with that `Name`.

## Prefabs

A prefab is a scene file you spawn many times. `SceneSpawnOptions` changes each copy:

| Field | Meaning |
|-------|---------|
| `offset` | Added to the `Transform2D` position of each root entity. |
| `parent` | Root entities become children of this entity. |
| `overrides` | Component patches per entity, keyed by `Name` or by file `id`. |

```cpp
SceneHandle coin = scenes->load("assets/coin.json");

SceneSpawnOptions opts;
opts.offset = {320.f, 120.f};
opts.overrides = {
    {"Coin", {{"Sprite", {{"tint", {255, 160, 160, 255}}}}}},
};
scenes->spawn(cmd, coin, opts);
```

Overrides merge into the saved component values (JSON merge patch): fields you list replace the saved ones, others stay. Setting a component to `null` leaves it out of that copy.

## Reloading

```cpp
bool reload(SceneHandle scene);
```

`reload` reads the file again and updates every live instance in place: entities keep their ids, components follow the file (described components missing from the file are removed), entities added to the file are spawned, and entities removed from it are despawned. Spawn options (offset, parent, overrides) apply again.

Scene files are tracked by the `AssetServer`, so hot reload picks up edits on disk without any code; see [Asset Server](asset-server.md#hot-reload).

Physics bodies follow the reloaded components: a body whose collider or `RigidBody2D` settings changed is made again.
