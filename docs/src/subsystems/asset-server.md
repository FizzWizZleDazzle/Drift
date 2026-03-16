# Asset Server

The `AssetServer` is a unified loader that delegates to subsystem-specific loaders for textures, sounds, and fonts. It provides both a generic templated API (`load<T>()`) and named convenience methods. Plugins register their loaders during build, so by the time your startup systems run, all asset types are available.

**Header:** `#include <drift/AssetServer.hpp>`

**Plugin:** Loaders are registered by `RendererPlugin`, `AudioPlugin`, and `FontPlugin` respectively. All are included in `DefaultPlugins`.

## Why Use the Asset Server?

Each subsystem resource already has its own load function (`RendererResource::loadTexture`, `AudioResource::loadSound`, etc.). The `AssetServer` provides a single entry point for all asset loading, which is useful when:

- You want a consistent API for loading any asset type
- A system needs to load multiple asset types without depending on every subsystem resource individually
- You are writing generic code or utilities

## Generic API

```cpp
template<typename T, typename... Args>
auto load(const char* path, Args&&... args);
```

The type tag `T` determines which loader is called:

| Type Tag | Returns | Equivalent To |
|----------|---------|---------------|
| `Texture` | `TextureHandle` | `RendererResource::loadTexture(path)` |
| `Sound` | `SoundHandle` | `AudioResource::loadSound(path)` |
| `Font` | `FontHandle` | `FontResource::loadFont(path, sizePx)` |

```cpp
void setup(ResMut<AssetServer> assets) {
    TextureHandle tex   = assets->load<Texture>("assets/player.png");
    SoundHandle   sfx   = assets->load<Sound>("assets/jump.wav");
    FontHandle    font  = assets->load<Font>("assets/font.ttf", 24);
}
```

The `Font` type tag accepts an additional `int sizePx` argument after the path.

## Named Methods

If you prefer explicit method names over template syntax:

```cpp
TextureHandle loadTexture(const char* path);
SoundHandle   loadSound(const char* path);
FontHandle    loadFont(const char* path, int sizePx);
```

```cpp
void setup(ResMut<AssetServer> assets) {
    TextureHandle tex  = assets->loadTexture("assets/player.png");
    SoundHandle   sfx  = assets->loadSound("assets/jump.wav");
    FontHandle    font = assets->loadFont("assets/font.ttf", 24);
}
```

These are functionally identical to the generic API.

## Accessing in Systems

Use `ResMut<AssetServer>` as a system parameter:

```cpp
void startup(ResMut<AssetServer> assets, Commands& cmd) {
    auto tex = assets->load<Texture>("assets/hero.png");

    cmd.spawn().insert(
        Transform2D{.position = {400.f, 300.f}},
        Sprite{.texture = tex}
    );
}
```

## Loader Registration

Plugins register their loaders during the build phase. You do not need to do this yourself when using `DefaultPlugins`. The registration looks like this internally:

```cpp
// Inside RendererPlugin::build():
auto* assets = app.initResource<AssetServer>();
auto* renderer = app.getResource<RendererResource>();
assets->setTextureLoader([renderer](const char* path) {
    return renderer->loadTexture(path);
});

// Inside AudioPlugin::build():
auto* audio = app.getResource<AudioResource>();
assets->setSoundLoader([audio](const char* path) {
    return audio->loadSound(path);
});

// Inside FontPlugin::build():
auto* fonts = app.getResource<FontResource>();
assets->setFontLoader([fonts](const char* path, int size) {
    return fonts->loadFont(path, (float)size);
});
```

If you call `load<T>()` before the corresponding loader has been registered, it returns a default (invalid) handle.

## When to Use AssetServer vs. Direct Loading

| Scenario | Recommendation |
|----------|---------------|
| Loading a texture in a startup system | Either works. `AssetServer` or `RendererResource` are equivalent. |
| Loading mixed asset types in one system | `AssetServer` -- one dependency instead of three. |
| Runtime loading (e.g., level transitions) | `AssetServer` keeps the loading code uniform. |
| Subsystem-specific operations (getTextureSize, destroyTexture) | Use the subsystem resource directly. `AssetServer` is load-only. |

## Complete Example

```cpp
struct GameAssets : public Resource {
    DRIFT_RESOURCE(GameAssets)

    TextureHandle player;
    TextureHandle enemy;
    TextureHandle tileset;
    SoundHandle   jumpSfx;
    SoundHandle   hitSfx;
    FontHandle    hudFont;
};

void loadAllAssets(ResMut<AssetServer> assets, ResMut<GameAssets> ga) {
    ga->player  = assets->load<Texture>("assets/sprites/player.png");
    ga->enemy   = assets->load<Texture>("assets/sprites/enemy.png");
    ga->tileset = assets->load<Texture>("assets/tiles/world.png");
    ga->jumpSfx = assets->load<Sound>("assets/sfx/jump.wav");
    ga->hitSfx  = assets->load<Sound>("assets/sfx/hit.ogg");
    ga->hudFont = assets->load<Font>("assets/fonts/pixel.ttf", 16);
}

void spawnPlayer(Res<GameAssets> ga, Commands& cmd) {
    cmd.spawn().insert(
        Transform2D{.position = {200.f, 400.f}},
        Sprite{.texture = ga->player},
        RigidBody2D{.type = BodyType::Dynamic},
        BoxCollider2D{.halfSize = {12.f, 16.f}},
        PlayerTag{}
    );
}

int main() {
    App app;
    app.setConfig({.title = "Asset Server Demo", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameAssets>();
    app.startup<loadAllAssets>("load_assets");
    app.startup<spawnPlayer>("spawn_player");
    return app.run();
}
```
