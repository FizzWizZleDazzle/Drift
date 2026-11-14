# App and Plugins

The `App` is the central type in Drift. It owns the ECS world, the resource store, the system scheduler, and the main loop. You configure it with a builder pattern, add plugins to bring in subsystems, register your own systems and resources, then call `run()` to start the engine.

## Minimal Application

```cpp
#include <drift/App.hpp>
#include <drift/plugins/DefaultPlugins.hpp>

using namespace drift;

int main() {
    App app;
    app.config().window.title = "My Game";
    app.addPlugins<DefaultPlugins>();
    return app.run();
}
```

This creates a window, initializes the GPU, sets up all default subsystems, and enters the main loop. The `run()` method returns an exit code when the application closes.

## Config

Window, rendering and engine settings come from `drift.toml` beside the game, which plugins and the game read their own sections from as well. `app.config()` holds the engine's part; change fields in code before `run()` to override the file:

```toml
[window]
title = "Asteroid Field"
width = 1920
height = 1080

[app]
threads = 4   # 3 workers + main thread
```

```cpp
App app;
app.args(argc, argv);                  // lets --config and --set reach the settings
app.config().render.vsync = false;     // code wins over drift.toml
```

`window.width` and `window.height` are the logical resolution the game draws in; `window.scale_mode` decides how it fits the window. `app.threads` controls the parallel scheduler: `0` uses `hardware_concurrency - 1` threads (capped at 7), `1` runs every system on the main thread. See [Configuration](../reference/configuration.md) for every section, the order values are applied in, and live reload.

## App Lifecycle

The lifecycle is:

1. **Construction**: `App app;` creates the app with default state.
2. **Configuration**: `args()`, `config()`, `addPlugins()`, `addPlugin()`, `addResource()`, `addSystem()`, etc.
3. **Run**: `app.run()` initializes SDL, reads `drift.toml` (if nothing read it earlier), creates the window and GPU device, then:
   - Builds all plugin groups (calls `PluginGroup::build(App&)`)
   - Builds all individual plugins (calls `Plugin::build(App&)`)
   - Finishes all individual plugins (calls `Plugin::finish(App&)`)
   - Applies environment and `--set` settings again, over what code changed
   - Sorts systems by system set ordering
   - Runs all Startup-phase systems (once)
   - Runs initial `OnEnter` state callbacks
   - Enters the main loop

Each frame of the main loop executes phases in order:

```
PreUpdate -> [SDL event poll] -> [State transitions] -> Update -> PostUpdate -> Extract -> Render -> RenderFlush
```

Commands are flushed after PreUpdate, Update, and PostUpdate phases. Startup systems also flush commands after running.

## Driving the Loop Yourself

`run()` owns the loop. A host that needs its own loop, such as a scripting language binding or a program embedding Drift in a larger application, drives the same frame in two halves instead:

```cpp
App app;
app.config().window.title = "Embedded";
app.addPlugins<DefaultPlugins>();
if (!app.start()) return 1;          // window, device, plugins, Startup systems

while (app.beginFrame()) {           // time, events, PreUpdate, Update, PostUpdate
    // Your work: read input, change the world, queue draws.
    auto* input = app.getResource<InputResource>();
    if (input->keyPressed(Key::Escape)) app.quit();
    app.getResource<RendererResource>()->drawCircleFilled({100, 100}, 20, Color::red());
    app.endFrame();                  // Extract, Render, RenderFlush: draw and present
}
```

| Method | Does |
|--------|------|
| `start()` | Opens the window and graphics device, builds plugins and runs Startup systems. Returns `false` on failure. Only the first call does anything. |
| `beginFrame()` | Advances time, polls events and runs PreUpdate, Update and PostUpdate. Returns `false` once the app has quit; do not call `endFrame()` then. |
| `endFrame()` | Runs Extract, Render and RenderFlush. |
| `running()` | `true` between a successful `start()` and the frame that sees `quit()` or a closed window. |

Between `beginFrame()` and `endFrame()` no system is running, so the world and every resource are safe to read and change from the calling thread: input reflects this frame's events, physics has stepped and collision events are fresh, and draws queued on `RendererResource` show in this frame. `run()` is `start()` followed by this loop. In the browser `run()` hands the same two halves to the browser's frame callback.

### Simulating while the host works

`beginFrame()` is `pollFrame()` followed by `simulate()`. A host with work of its own each frame, such as a scripting language running game logic, can call them separately and let the simulation run on a background thread meanwhile:

```cpp
while (app.pollFrame()) {          // time, events, PreUpdate, state transitions
    app.simulateAsync();           // Update and PostUpdate start on a background thread
    hostWorkThatLeavesTheWorldAlone();
    app.finishSimulation();        // wait, then run what needed the main thread
    // The world is the host's again: read results, write changes.
    app.endFrame();
}
```

| Method | Does |
|--------|------|
| `pollFrame()` | Advances time, polls events and runs PreUpdate and state transitions. Returns `false` once the app has quit |
| `simulate()` | Runs Update and PostUpdate on the calling thread, with the worker pool |
| `simulateAsync()` | Starts Update and PostUpdate on a background thread and returns at once |
| `finishSimulation()` | Waits for the background part, then runs the systems it left for the main thread. Does nothing when no simulation is in flight |
| `lastSimulation()` | A `SimulationTiming`: when the simulation started and ended on the performance-counter clock, whether it ran in the background, and how long the main-thread remainder took |

Between `simulateAsync()` and `finishSimulation()` the host must not touch the world, `Commands`, or a resource any system may use. Resources that are `mainThreadOnly()` (the renderer, fonts, the asset server, ...) are the exception: systems that use them do not run on the background thread. Instead they, and systems ordered after them by a conflicting access, wait for `finishSimulation()` and run on the calling thread in their usual order; exclusive systems registered after them run first, on the background thread, so an exclusive system must not use thread-bound state. Without threads (the web build, or `[app] threads = 1`) `simulateAsync()` is `simulate()`. `endFrame()` finishes a simulation still in flight. The candela package drives its frames this way.

## Plugin

A `Plugin` registers resources, systems, and event handlers with the `App`. Implement the `Plugin` interface and use the `DRIFT_PLUGIN` macro for the name.

```cpp
#include <drift/Plugin.hpp>
#include <drift/App.hpp>

class ScorePlugin : public Plugin {
public:
    void build(App& app) override {
        app.addResource<ScoreResource>();
        app.addSystem<scoreUpdate>("score_update", Phase::Update);
    }

    void finish(App& app) override {
        // Called after ALL plugins have been built.
        // Use this for cross-plugin dependencies.
        auto* renderer = app.getResource<RendererResource>();
        // ... set up something that depends on the renderer ...
    }

    DRIFT_PLUGIN(ScorePlugin)
};
```

**`settings(App&)`** is called when the plugin is added, before anything is built. A plugin that reads a `drift.toml` section binds it here with `app.settings<T>("section")`, and reads the resource in `build()` with `app.getResource<T>()`. Binding here is what lets [`App::loadSettings()`](../reference/configuration.md#checking-the-file) check the section without starting the app. It is optional: the default does nothing.

**`build(App&)`** is called first for every plugin, in registration order. This is where you register resources, systems, and components.

**`finish(App&)`** is called after all plugins have been built. Use it when your plugin depends on resources created by other plugins. `finish()` is optional: the default implementation is a no-op.

**`name()`** returns a string identifier. The `DRIFT_PLUGIN(Name)` macro implements it for you:

```cpp
#define DRIFT_PLUGIN(Name) const char* name() const override { return #Name; }
```

### Adding Plugins to the App

Use the template form to add a single plugin:

```cpp
app.addPlugin<ScorePlugin>();
```

Or add a raw pointer if you need to configure the plugin before registration:

```cpp
auto* physics = new PhysicsPlugin();
app.addPlugin(physics);
```

## PluginGroup

A `PluginGroup` bundles multiple plugins into a single unit. It has a single `build()` method that adds individual plugins.

```cpp
class GamePlugins : public PluginGroup {
public:
    void build(App& app) override {
        app.addPlugin<ScorePlugin>();
        app.addPlugin<InventoryPlugin>();
        app.addPlugin<EnemyAIPlugin>();
    }
};

// Usage
app.addPlugins<GamePlugins>();
```

## DefaultPlugins

`DefaultPlugins` is the standard plugin group that sets up a full-featured 2D game engine. It includes:

| Plugin | Purpose |
|--------|---------|
| `InputPlugin` | Keyboard, mouse, gamepad input |
| `RendererPlugin` | GPU rendering, sprite batching |
| `AudioPlugin` | Sound effects, music, mixer buses, `AudioSource`/`AudioListener` spatial audio |
| `PostEffectsPlugin` | Post effect library: bloom, CRT, color grading and more |
| `PhysicsPlugin` | Box2D-based 2D physics |
| `SpritePlugin` | Sprite component registration and rendering |
| `FontPlugin` | Font loading, text rendering, and the `Text` component |
| `ParticlePlugin` | Particle system management |
| `TilemapPlugin` | Tiled and LDtk maps: drawing, colliders, object spawning |
| `UIPlugin` | Immediate-mode UI (ImGui) |
| `HierarchyPlugin` | Parent/child transforms, cascading despawn |
| `SpriteAnimationPlugin` | Frame-based sprite animation |
| `CameraPlugin` | Camera follow and screen shake |
| `TrailPlugin` | Trail renderer for moving entities |

```cpp
app.addPlugins<DefaultPlugins>();
```

## MinimalPlugins

`MinimalPlugins` includes only the bare minimum: `InputPlugin` and `RendererPlugin`. Use it for lightweight tools, tests, or custom setups where you want to pick which subsystems to include.

```cpp
app.addPlugins<MinimalPlugins>();
app.addPlugin<PhysicsPlugin>();   // add only what you need
app.addPlugin<SpritePlugin>();
```

## Resources

Resources are singleton objects stored in the `App`. Plugins and user code register resources, and systems access them via `Res<T>` (read-only) and `ResMut<T>` (read-write) parameters.

```cpp
struct GameState : public Resource {
    DRIFT_RESOURCE(GameState)

    int score = 0;
    float elapsed = 0.f;
    EntityId player = InvalidEntityId;
};
```

Three methods manage resources:

```cpp
// Always creates a new resource (overwrites if already present)
auto* state = app.addResource<GameState>();

// Creates only if not already present (idempotent); returns existing if found
auto* state = app.initResource<GameState>();

// Retrieve a resource (returns nullptr if not found)
auto* state = app.getResource<GameState>();
```

`initResource<T>()` is safe to call multiple times and is the recommended way to register resources in plugins, since plugin ordering may cause the same resource to be initialized from multiple places.

Resources must derive from `Resource`. The `DRIFT_RESOURCE(Name)` macro provides the `name()` override used for debugging.

## Putting It Together

A typical game entry point:

```cpp
#include <drift/App.hpp>
#include <drift/plugins/DefaultPlugins.hpp>

using namespace drift;

struct GameState : public Resource {
    DRIFT_RESOURCE(GameState)
    EntityId camera = InvalidEntityId;
    int score = 0;
};

void setup(ResMut<GameState> game, Commands& cmd) {
    game->camera = cmd.spawn()
        .insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});
}

void gameLoop(Res<Time> time, ResMut<GameState> game, Commands& cmd) {
    // game logic here
}

int main() {
    App app;
    app.config().window.title = "My Game";
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameState>();
    app.startup<setup>("setup");
    app.update<gameLoop>("game_loop");
    return app.run();
}
```

## Custom Plugin Example

Here is a complete custom plugin that registers a resource, a startup system, and an update system:

```cpp
struct EnemyResource : public Resource {
    DRIFT_RESOURCE(EnemyResource)
    float spawnTimer = 0.f;
    float spawnInterval = 2.f;
};

struct EnemyTag {};

void spawnInitialEnemies(ResMut<EnemyResource> enemies, Commands& cmd) {
    for (int i = 0; i < 5; ++i) {
        cmd.spawn().insert(Transform2D{.position = {i * 100.f, 0.f}},
                           Sprite{}, EnemyTag{});
    }
}

void enemySpawnSystem(Res<Time> time, ResMut<EnemyResource> enemies, Commands& cmd) {
    enemies->spawnTimer += time->delta;
    if (enemies->spawnTimer >= enemies->spawnInterval) {
        enemies->spawnTimer -= enemies->spawnInterval;
        cmd.spawn().insert(Transform2D{}, Sprite{}, EnemyTag{});
    }
}

class EnemyPlugin : public Plugin {
public:
    void build(App& app) override {
        app.initResource<EnemyResource>();
        app.startup<spawnInitialEnemies>("spawn_initial_enemies");
        app.update<enemySpawnSystem>("enemy_spawn");
    }
    DRIFT_PLUGIN(EnemyPlugin)
};
```
