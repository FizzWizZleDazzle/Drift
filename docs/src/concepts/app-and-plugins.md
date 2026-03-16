# App and Plugins

The `App` is the central type in Drift. It owns the ECS world, the resource store, the system scheduler, and the main loop. You configure it with a builder pattern, add plugins to bring in subsystems, register your own systems and resources, then call `run()` to start the engine.

## Minimal Application

```cpp
#include <drift/App.hpp>
#include <drift/plugins/DefaultPlugins.hpp>

using namespace drift;

int main() {
    App app;
    app.setConfig({.title = "My Game", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    return app.run();
}
```

This creates a window, initializes the GPU, sets up all default subsystems, and enters the main loop. The `run()` method returns an exit code when the application closes.

## Config

`Config` controls window and engine settings. Pass it to `setConfig()` before calling `run()`.

```cpp
struct Config {
    std::string title = "Drift";
    int32_t width  = 1280;
    int32_t height = 720;
    bool fullscreen = false;
    bool vsync      = true;
    bool resizable  = true;
    int32_t threadCount = 0;  // 0 = auto, 1 = sequential (no threading)
};
```

The `threadCount` field controls the parallel scheduler. `0` uses `hardware_concurrency - 1` threads (capped at 7). `1` disables threading and runs all systems sequentially on the main thread.

```cpp
app.setConfig({
    .title = "Asteroid Field",
    .width = 1920,
    .height = 1080,
    .fullscreen = true,
    .vsync = false,
    .threadCount = 4,  // 3 workers + main thread
});
```

## App Lifecycle

The lifecycle is:

1. **Construction** -- `App app;` creates the app with default state.
2. **Configuration** -- `setConfig()`, `addPlugins()`, `addPlugin()`, `addResource()`, `addSystem()`, etc.
3. **Run** -- `app.run()` initializes SDL, creates the window and GPU device, then:
   - Builds all plugin groups (calls `PluginGroup::build(App&)`)
   - Builds all individual plugins (calls `Plugin::build(App&)`)
   - Finishes all individual plugins (calls `Plugin::finish(App&)`)
   - Sorts systems by system set ordering
   - Runs all Startup-phase systems (once)
   - Runs initial `OnEnter` state callbacks
   - Enters the main loop

Each frame of the main loop executes phases in order:

```
PreUpdate -> [SDL event poll] -> [State transitions] -> Update -> PostUpdate -> Extract -> Render -> RenderFlush
```

Commands are flushed after PreUpdate, Update, and PostUpdate phases. Startup systems also flush commands after running.

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

**`build(App&)`** is called first for every plugin, in registration order. This is where you register resources, systems, and components.

**`finish(App&)`** is called after all plugins have been built. Use it when your plugin depends on resources created by other plugins. `finish()` is optional -- the default implementation is a no-op.

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
| `AudioPlugin` | Sound effects and music |
| `PhysicsPlugin` | Box2D-based 2D physics |
| `SpritePlugin` | Sprite component registration and rendering |
| `FontPlugin` | Font loading and text rendering |
| `ParticlePlugin` | Particle system management |
| `TilemapPlugin` | Tile-based map rendering |
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

// Creates only if not already present (idempotent) -- returns existing if found
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
    app.setConfig({.title = "My Game", .width = 1280, .height = 720});
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
