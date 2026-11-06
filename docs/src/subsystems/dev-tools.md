# Dev Tools

Development builds come with an in-game overlay for looking inside a running game: an entity inspector, a performance panel, physics debug drawing, and scene save and load. Press **F1** to show or hide it. The tools are built with Dear ImGui and exist only when the engine is compiled with `DRIFT_DEV` (on by default; turn it off for release builds, see [Installation](../getting-started/installation.md)).

**Header:** `#include <drift/resources/UIResource.hpp>`

**Plugin:** `UIPlugin` hosts the overlay; `PhysicsPlugin` and `ScenePlugin` add their panels to it. All are in `DefaultPlugins`.

## The Overlay

The overlay opens with a small menu showing the frame rate and a checkbox per panel. Each panel is its own window; drag and resize them as you like.

| Panel | What it does |
|-------|--------------|
| Inspector | Lists entities, edits their components, spawns and despawns. Open by default. |
| Performance | Frame time graph, renderer counters, per-system timings. |
| Physics | Toggles physics debug drawing. |
| Scene | Saves the world to a file, spawns a scene file, reloads a file. |

Change the toggle key with `ui->setDevToggleKey(Key::F2)`, or show the overlay from code with `ui->devToggle()`.

## Inspector

- **Entity list.** Every entity that has a [reflected](../concepts/reflection.md) component, shown as a hierarchy tree by `Parent`. Entities show their `Name`, or their id. Type in the filter box to search by name.
- **Components.** Selecting an entity lists its reflected components with an editor per field: numbers drag, colors open a picker, enums are drop-downs, vectors grow and shrink, textures show a preview and accept a new path (press Enter to load it). Edits apply to the world at once.
- **Add and remove.** "Add component" inserts any reflected component with default values. Right-click a component header to remove it.
- **Spawn and despawn.** "Spawn" creates an entity with a `Name` and a `Transform2D`. "Despawn" removes the selected entity and its children.
- **Selection.** The selected entity is outlined in the world (its sprite bounds and a cross at its position).

Your own components appear once you describe them with `reflect::component`.

## Performance

A graph of recent frame times with the average and worst frame, the backend name, draw calls and sprite count from the renderer, and a table of how long each system took in the last frame, sorted by time or in schedule order. The same timings are available in code:

```cpp
for (const SystemTiming& t : app.systemTimings()) {
    DRIFT_LOG_INFO("%s: %llu us", t.name.c_str(), (unsigned long long)t.microseconds);
}
```

## Physics Debug Drawing

The Physics panel turns on drawing of Box2D's view of the world: collider outlines (filled for solid bodies), bounding boxes, contact points and normals, and joints. Shapes are drawn in world space through the cameras, above the scene.

Debug drawing works in any build, through `PhysicsResource`:

```cpp
physics->setDebugDraw({.enabled = true, .aabbs = true});
```

See [Physics](physics-resource.md#debug-drawing).

## Adding Your Own Panel

Any plugin or game can add a panel. The function fills an ImGui window and runs as an exclusive system, so it may read and change the world freely.

```cpp
#ifdef DRIFT_DEV
#include "imgui.h"

void finish(App& app) override {
    app.getResource<UIResource>()->addDevPanel("Enemies", [](App& a) {
        auto* spawner = a.getResource<EnemySpawner>();
        ImGui::SliderFloat("Spawn rate", &spawner->rate, 0.f, 10.f);
        if (ImGui::Button("Spawn wave")) spawner->spawnWave(a);
    });
}
#endif
```

Register panels in a plugin's `finish()` (the UI exists by then). Pass `true` as the third argument to open the panel when the overlay first shows.

## Hot Reload

Development builds also watch the files of loaded textures and scenes and reload them when they change on disk; see [Asset Server](asset-server.md#hot-reload).
