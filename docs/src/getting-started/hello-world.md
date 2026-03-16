# Hello World

This chapter walks through a minimal Drift application, then expands it step-by-step to spawn sprites, load textures, and set up a camera.

## The minimal app

```cpp
#include <drift/App.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/Time.hpp>

using namespace drift;

void helloSystem(Res<Time> time) {
    // Called every frame
}

int main() {
    App app;
    app.setConfig({.title = "Hello Drift", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.update<helloSystem>();
    return app.run();
}
```

Let's break this down line by line.

### Includes

```cpp
#include <drift/App.hpp>
```

`App` is the central object in a Drift program. It owns the window, the ECS world, all resources, and the system scheduler. Every Drift program creates exactly one `App` instance.

```cpp
#include <drift/plugins/DefaultPlugins.hpp>
```

`DefaultPlugins` is a plugin group that registers all built-in subsystems: rendering, input, audio, physics, sprites, fonts, particles, tilemaps, UI, hierarchy propagation, sprite animation, camera controllers, and trail rendering. For a simpler setup with just windowing and rendering, you can use `MinimalPlugins` instead.

```cpp
#include <drift/resources/Time.hpp>
```

`Time` is a resource that the engine updates every frame. It provides `delta` (seconds since last frame), `elapsed` (seconds since engine start), and `frame` (frame counter).

### Configuration

```cpp
app.setConfig({.title = "Hello Drift", .width = 1280, .height = 720});
```

`setConfig` accepts a `Config` struct using C++20 designated initializers. The full set of fields:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `title` | `std::string` | `"Drift"` | Window title |
| `width` | `int32_t` | `1280` | Window width in pixels |
| `height` | `int32_t` | `720` | Window height in pixels |
| `fullscreen` | `bool` | `false` | Start in fullscreen mode |
| `vsync` | `bool` | `true` | Enable vertical sync |
| `resizable` | `bool` | `true` | Allow window resizing |
| `threadCount` | `int32_t` | `0` | Worker threads. 0 = auto, 1 = sequential |

### Plugins

```cpp
app.addPlugins<DefaultPlugins>();
```

Plugins are the modular building blocks of the engine. Each plugin registers resources and systems during its `build()` phase, then resolves cross-plugin dependencies in `finish()`. You can add individual plugins with `app.addPlugin<RendererPlugin>()` if you want fine-grained control.

### Systems

```cpp
app.update<helloSystem>();
```

Systems are plain free functions. The engine inspects each parameter type at compile time to automatically inject the correct resource, query, or command buffer. Common system parameter types:

- `Res<T>` -- read-only access to resource `T`
- `ResMut<T>` -- read-write access to resource `T`
- `Query<T, U, ...>` -- read-only query over entities with components `T`, `U`, ...
- `QueryMut<T, U, ...>` -- read-write query over entities
- `Commands&` -- deferred entity mutation buffer
- `EventReader<T>` / `EventWriter<T>` -- event channels

The `update<fn>()` shorthand registers the function in the `Phase::Update` phase. Other phase shortcuts include `startup<fn>()` (runs once), `preUpdate<fn>()`, `postUpdate<fn>()`, and `render<fn>()`.

### Running

```cpp
return app.run();
```

`app.run()` enters the main loop. It processes SDL events, runs all registered systems in phase order each frame, and returns `0` on clean exit.

## Adding a sprite

Now let's display something on screen. We need a texture, a `Sprite` component, a `Transform2D` for positioning, and a `Camera` so the renderer knows what part of the world to show.

```cpp
#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/AssetServer.hpp>
#include <drift/Query.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/Time.hpp>

using namespace drift;

void setup(ResMut<AssetServer> assets, Commands& cmd) {
    // Load a texture through the asset server
    TextureHandle tex = assets->load<Texture>("assets/player.png");

    // Spawn the camera (required for rendering)
    cmd.spawn()
        .insert(Transform2D{.position = {0.f, 0.f}},
                Camera{.zoom = 1.f, .active = true});

    // Spawn a sprite entity
    cmd.spawn()
        .insert(Transform2D{.position = {0.f, 0.f}},
                Sprite{.texture = tex});
}

int main() {
    App app;
    app.setConfig({.title = "Hello Sprite", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.startup<setup>();
    return app.run();
}
```

### Asset loading

```cpp
TextureHandle tex = assets->load<Texture>("assets/player.png");
```

`AssetServer` is a resource registered by `DefaultPlugins`. The `load<T>()` template dispatches to the correct loader based on the type tag:

- `load<Texture>("path.png")` -- loads an image as a GPU texture, returns `TextureHandle`
- `load<Sound>("path.wav")` -- loads an audio file, returns `SoundHandle`
- `load<Font>("path.ttf", 24)` -- loads a font at a given pixel size, returns `FontHandle`

Asset paths are relative to the working directory. Place your assets in an `assets/` folder next to the executable.

### Spawning entities with Commands

```cpp
cmd.spawn()
    .insert(Transform2D{.position = {0.f, 0.f}},
            Sprite{.texture = tex});
```

`Commands` is a deferred mutation buffer. Calls to `spawn()`, `insert()`, and `despawn()` are queued and applied to the ECS world after the current phase finishes. This keeps the world consistent during system execution.

`spawn()` returns an `EntityCommands` builder. The `insert()` method accepts any number of components in a single call (variadic insert). You can also chain multiple `insert()` calls:

```cpp
cmd.spawn()
    .insert<Transform2D>({.position = {100.f, 200.f}})
    .insert<Sprite>({.texture = tex, .tint = Color::red()});
```

### The Camera entity

```cpp
cmd.spawn()
    .insert(Transform2D{.position = {0.f, 0.f}},
            Camera{.zoom = 1.f, .active = true});
```

The renderer looks for an entity with both `Transform2D` and `Camera` components where `active` is `true`. Without a camera entity, nothing will be rendered. The camera's `Transform2D::position` determines the center of the view, and `Camera::zoom` controls the zoom level.

## Moving the sprite

Let's make the sprite respond to input:

```cpp
#include <drift/resources/InputResource.hpp>

// ... (setup function as above)

void movePlayer(Res<InputResource> input, Res<Time> time,
                QueryMut<Transform2D, With<Sprite>> sprites) {
    Vec2 dir{};
    if (input->keyHeld(Key::W) || input->keyHeld(Key::Up))    dir.y -= 1.f;
    if (input->keyHeld(Key::S) || input->keyHeld(Key::Down))  dir.y += 1.f;
    if (input->keyHeld(Key::A) || input->keyHeld(Key::Left))  dir.x -= 1.f;
    if (input->keyHeld(Key::D) || input->keyHeld(Key::Right)) dir.x += 1.f;

    float speed = 200.f;
    Vec2 velocity = dir.normalized() * speed * time->delta;

    sprites.iter([&](Transform2D& t) {
        t.position += velocity;
    });
}

int main() {
    App app;
    app.setConfig({.title = "Hello Movement", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.startup<setup>();
    app.update<movePlayer>();
    return app.run();
}
```

A few things to note:

- **`QueryMut<Transform2D, With<Sprite>>`** iterates all entities that have both `Transform2D` and `Sprite`, giving mutable access to `Transform2D`. The `With<Sprite>` is a filter -- it requires the component to be present but does not yield it in the callback.

- **`sprites.iter([&](Transform2D& t) { ... })`** iterates every matching entity. The callback receives only the data components (`Transform2D`), not the filters.

- **`InputResource`** provides `keyHeld()`, `keyPressed()`, `keyReleased()`, `mousePosition()`, `mouseButtonHeld()`, and `mouseWheelDelta()` among other methods.

- **Time-based movement**: multiply velocity by `time->delta` so movement speed is frame-rate independent.

## Putting it all together

Here is the complete program with setup, movement, and camera tracking:

```cpp
#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/AssetServer.hpp>
#include <drift/Query.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/Name.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/Time.hpp>

using namespace drift;

// Marker component to identify the player entity
struct PlayerTag {};

// Game state stored as a resource
struct GameState : public Resource {
    TextureHandle texture;
    EntityId camera = InvalidEntityId;
};

void setup(ResMut<GameState> game, ResMut<AssetServer> assets, Commands& cmd) {
    game->texture = assets->load<Texture>("assets/player.png");

    // Spawn camera
    game->camera = cmd.spawn()
        .insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});

    // Spawn player
    cmd.spawn()
        .insert(Transform2D{},
                Sprite{.texture = game->texture},
                PlayerTag{},
                Name{"Player"});
}

void movePlayer(Res<InputResource> input, Res<Time> time,
                ResMut<GameState> game,
                QueryMut<Transform2D, With<PlayerTag>> players,
                Commands& cmd) {
    Vec2 dir{};
    if (input->keyHeld(Key::W)) dir.y -= 1.f;
    if (input->keyHeld(Key::S)) dir.y += 1.f;
    if (input->keyHeld(Key::A)) dir.x -= 1.f;
    if (input->keyHeld(Key::D)) dir.x += 1.f;

    float speed = 200.f;
    Vec2 velocity = dir.normalized() * speed * time->delta;

    // Move the player and track position for camera
    players.iter([&](Transform2D& t) {
        t.position += velocity;

        // Update camera to follow player
        cmd.entity(game->camera)
            .insert<Transform2D>({.position = t.position});
    });
}

int main() {
    App app;
    app.setConfig({.title = "Hello Drift", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameState>();
    app.startup<setup>();
    app.update<movePlayer>();
    return app.run();
}
```

Key patterns demonstrated here:

1. **Custom resources** inherit from `Resource` and are registered with `app.initResource<T>()` (idempotent -- safe to call multiple times).
2. **Marker components** like `PlayerTag` are empty structs used with `With<PlayerTag>` to filter queries. They are auto-registered when first used in a query.
3. **Entity references** can be stored as `EntityId` values in resources and later used with `cmd.entity(id)` to mutate specific entities.
4. **`Name{"Player"}`** is a debug label component -- useful for identifying entities in the dev overlay.
5. **`EntityCommands::insert()`** returns the `EntityId` via implicit conversion (`operator EntityId()`), so you can capture it directly: `EntityId id = cmd.spawn().insert(...)`.

## Next steps

- [Project Structure](project-structure.md) -- understand how the engine is organized
- [ECS Concepts](../concepts/ecs.md) -- deeper dive into entities, components, systems, and queries
- [Components Reference](../components/sprite.md) -- full reference for built-in components
