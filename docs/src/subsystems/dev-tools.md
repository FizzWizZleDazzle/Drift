# Dev Tools

Development builds come with an in-game overlay for looking inside a running game: an entity inspector you can click entities into, a performance panel, the recent warnings and errors, physics options, and scene save and load. Press **F1** to show or hide it; it works in the browser too. The overlay is built with Dear ImGui and exists only when the engine is compiled with `DRIFT_DEV` (on by default; turn it off for release builds, see [Installation](../getting-started/installation.md)).

Debug views, which draw colliders, entity bounds, cameras, lights and tile grids over the game, work in every build.

**Headers:** `#include <drift/resources/UIResource.hpp>`, `#include <drift/DebugViews.hpp>`

**Plugins:** `UIPlugin` hosts the overlay; `RendererPlugin` adds the `DebugViews` resource; `PhysicsPlugin`, `CameraPlugin`, `LightingPlugin`, `TilemapPlugin` and `ScenePlugin` add their views and panels. All but `LightingPlugin` are in `DefaultPlugins`.

## The Overlay

The overlay opens with a small menu showing the frame rate, a check box per panel, and a check box per debug view (hover one for what it draws). Each panel is its own window; drag and resize them as you like.

| Panel | What it does |
|-------|--------------|
| Inspector | Lists entities, picks them by clicking in the game, edits their components, spawns and despawns. Open by default. |
| Performance | Frame time graph, renderer counters, entities, memory, time per phase and per system. |
| Log | The latest warnings and errors. |
| Physics | The colliders view and its options, and the body count. |
| Scene | Saves the world to a file, spawns a scene file, reloads a file. |

Change the toggle key with `ui->setDevToggleKey(Key::F2)`, or show the overlay from code with `ui->devToggle()`.

## Inspector

- **Picking.** Click an entity in the game to select it. While the mouse is over the game, the inspector outlines what is under it; clicks on the overlay's windows stay with them. The entity whose sprite, shape or text is drawn on top wins (higher layer, then higher z). Picking tests the outlines the [debug views](#debug-views) know.
- **Entity list.** Every entity that has a [reflected](../concepts/reflection.md) component, shown as a hierarchy tree by `Parent`. Entities show their `Name`, or their id. Type in the filter box to search by name.
- **Components.** Selecting an entity lists its reflected components with an editor per field: numbers drag, colors open a picker, enums are drop-downs, vectors grow and shrink, textures show a preview and accept a new path (press Enter to load it). Edits apply to the world at once.
- **Components kept elsewhere.** Components that live outside the ECS, such as a scripting host's, show below the reflected ones, read-only, once the host hands them over with `ui->setForeignComponents(fn)`: `fn` takes an entity and answers its components as a JSON object by name. The Candela runner does this for game components.
- **Add and remove.** "Add component" inserts any reflected component with default values. Right-click a component header to remove it.
- **Spawn and despawn.** "Spawn" creates an entity with a `Name` and a `Transform2D`. "Despawn" removes the selected entity and its children.
- **Selection.** The selected entity is outlined in the world, over lighting and post effects.

Your own components appear once you describe them with `reflect::component`.

## Performance

A graph of recent frame times with the average and worst frame, the backend name, draw calls and sprite count from the renderer, the entity count, the memory in use (in the browser), the milliseconds each phase took last frame, and a table of how long each system took, sorted by time or in schedule order. The same timings are available in code:

```cpp
for (const SystemTiming& t : app.systemTimings()) {
    DRIFT_LOG_INFO("%s: %llu us", t.name.c_str(), (unsigned long long)t.microseconds);
}
```

## Log

The latest warnings and errors from the [log](../reference/logging.md#recent-problems), errors in red, newest at the bottom. "Clear" hides the lines shown so far; "Errors only" hides warnings.

## Debug Views

A debug view is drawing that shows what the engine sees, switched on and off by name while the game runs. Views draw on `OverlayLayer`: in world coordinates through every camera that shows the screen, after lighting and post effects, so ambient darkness, vignettes and tints leave them at full brightness (see [Layers](rendering.md#layers-and-the-hud)). They work in every build.

| View | Added by | Draws |
|------|----------|-------|
| `bounds` | `RendererPlugin` | The outline of what each sprite, shape and text entity draws, and a cross at each entity's position |
| `colliders` | `PhysicsPlugin` | Colliders by body kind, sleeping bodies dimmed, sensors dashed, contacts and velocities; see [Physics debug drawing](physics-resource.md#debug-drawing) |
| `camera` | `CameraPlugin` | Each camera's view and center, its `CameraBounds` area, its `CameraFollow` dead zone and the point it follows |
| `lights` | `LightingPlugin` | Each light's reach and cone in its color, and the light occluders |
| `tiles` | `TilemapPlugin` | Tile grid lines near the view, brighter on the edges of the chunks maps are drawn in |

```cpp
auto* views = app.getResource<DebugViews>();
views->set("colliders", true);
bool on = views->enabled("camera");
for (const auto& [name, description] : views->list()) { /* ... */ }
```

`set` answers false for a name no plugin added. Switched off, a view costs nothing.

Plugins and games add their own views. The function runs in the Render phase while the view is on, with the renderer's layer set to `OverlayLayer`; several plugins may add to one name.

```cpp
app.initResource<DebugViews>()->add("paths", "Each enemy's path", [](App& a, RendererResource& r) {
    for (const auto& path : a.getResource<Paths>()->all)
        r.drawPolyline(path.data(), int32_t(path.size()), Color{255, 120, 0, 255}, 2.f);
});
```

Use `initResource` rather than `getResource`, so the view is added whichever plugin is built first.

The `bounds` view and the inspector's picking read entity outlines from sources plugins add with `addOutlines`: a function that hands each entity's `DebugOutline` (four corners, the entity's position, its layer and z) to a sink. Sprites, shapes and text add theirs; `debugBox` builds the corners of a box placed like a sprite. `pick(app, screenPos)` answers the entity whose outline holds a point on the screen.

## Physics Debug Drawing

The Physics panel switches the `colliders` view and picks what it draws: shapes, bounding boxes, contacts, velocities and joints. In code, switch the view through `DebugViews` and set the options through `PhysicsResource`:

```cpp
app.getResource<DebugViews>()->set("colliders", true);
physics->setDebugDraw({.aabbs = true});
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

## In the Browser

The overlay, the debug views and picking work in web builds. While the game has focus, the page cancels the browser's own action for every function key, as far as the browser lets a page do that, so F1 goes to the overlay.

## Hot Reload

Development builds also watch the files of loaded textures, sounds, fonts and scenes and reload them when they change on disk; see [Asset Server](asset-server.md#hot-reload).
