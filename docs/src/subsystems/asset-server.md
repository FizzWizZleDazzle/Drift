# Asset Server

The `AssetServer` is a unified loader that delegates to subsystem-specific loaders for textures, sounds, and fonts. It provides both a generic templated API (`load<T>()`) and named convenience methods. Plugins register their loaders during build, so by the time your startup systems run, all asset types are available.

It also remembers which file every asset came from: loading a path twice returns the same handle, scenes save assets as paths, and hot reload knows which files to watch.

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

The `Font` type tag accepts an additional `int sizePx` argument after the path. It loads TrueType/OpenType files and BMFont `.fnt` files; for a bitmap font, `0` means the size the font was made at. See [Fonts](fonts.md).

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

## Caching and Paths

Each path loads once. Loading it again returns the same handle, so ten sprites that name `assets/coin.png` share one texture. Fonts are cached per path and pixel size.

```cpp
const char* pathOf(TextureHandle h) const;   // nullptr for assets made in code
const char* pathOf(SoundHandle h) const;
const char* pathOf(FontHandle h) const;
int         fontSize(FontHandle h) const;
void        add(const char* path, TextureHandle h);
```

`pathOf` returns the file an asset was loaded from. [Scenes](scenes.md) use it to save asset fields as paths. `add` gives a texture made in code a name, so scenes can save and load it like a file:

```cpp
TextureHandle dot = renderer->createTextureFromPixels(px, 8, 8);
assets->add("gen://dot", dot);
// A scene with "texture": "gen://dot" now resolves to this texture.
```

Handles from the asset server are shared. Destroying one through `RendererResource::destroyTexture` breaks every user of that path.

## Hot Reload

In development builds (`DRIFT_DEV`) on desktop, `HotReloadPlugin` checks the files of loaded assets a couple of times per second and reloads the ones that changed on disk. Textures, sounds and fonts reload in place (the handle stays the same; a texture's size may change, and voices playing a reloaded sound stop), and [scenes](scenes.md) update their live entities in place. Edit an image or a scene file in another program and the running game shows it. The game's [drift.toml](../reference/configuration.md#live-reload) is watched the same way.

`HotReloadResource` controls it, starting from the `[hot_reload]` section of drift.toml (`enabled`, `interval`):

```cpp
auto* hr = app.getResource<HotReloadResource>();
hr->enabled = false;     // stop watching
hr->interval = 1.f;      // seconds between checks
hr->poll(*assets);       // check now
```

In the browser nothing is watched. A page that writes new files into the game's file system names them instead, and they reload at the next frame:

```cpp
HotReloadResource::filesChanged({"assets/hero.png"});   // paths in the game folder
```

From JavaScript, set `Module.driftChangedFiles` to the list of paths and call `Module._drift_files_changed()`. Candela pages built with `drift build --web --dev` do this when the game changes. Outside `DRIFT_DEV`, add `HotReloadPlugin` yourself to get the same behaviour: `app.addPlugin<HotReloadPlugin>()` (header `drift/plugins/HotReloadPlugin.hpp`).

## Other Asset Kinds

Plugins with asset types of their own record what they load so tools and hot reload see every asset in one place:

```cpp
void track(const char* kind, uint32_t id, const char* path, int param = 0);
void untrack(const char* kind, uint32_t id);
uint32_t find(const char* kind, const char* path, int param = 0) const;   // 0 if not loaded
const char* pathOf(const char* kind, uint32_t id) const;
const std::vector<AssetRecord>& records() const;

using Reloader = std::function<bool(uint32_t id, const std::string& path)>;
void setReloader(const char* kind, Reloader fn);
bool reload(const char* path);   // reload every asset loaded from path
```

Textures are kind `"texture"`, sounds `"sound"`, fonts `"font"` and scenes `"scene"`. A kind with a reloader takes part in hot reload; `RendererPlugin`, `AudioPlugin`, `FontPlugin` and `ScenePlugin` register the ones for their kinds (`reloadTexture`, `reloadSound`, `reloadFont`, `SceneResource::reload`). Reloaders run inside an exclusive system, so they may change the world.

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
auto* assets = app.getResource<AssetServer>();
auto* renderer = app.getResource<RendererResource>();
assets->setTextureLoader([renderer](const char* path) {
    return renderer->loadTexture(path);
});
assets->setReloader("texture", [renderer](uint32_t id, const std::string& path) {
    return renderer->reloadTexture(TextureHandle{id}, path.c_str());
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
| Loading mixed asset types in one system | `AssetServer`; one dependency instead of three. |
| Runtime loading (e.g., level transitions) | `AssetServer` keeps the loading code uniform. |
| Subsystem-specific operations (getTextureSize, destroyTexture) | Use the subsystem resource directly. `AssetServer` loads and tracks assets. |
| Assets that scenes save or hot reload watches | `AssetServer`. Assets loaded directly through a subsystem resource have no recorded path. |

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
    app.config().window.title = "Asset Server Demo";
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameAssets>();
    app.startup<loadAllAssets>("load_assets");
    app.startup<spawnPlayer>("spawn_player");
    return app.run();
}
```
