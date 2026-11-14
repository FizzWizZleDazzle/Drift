# Configuration

**Headers:** `include/drift/Config.hpp`, `include/drift/Settings.hpp`

A game's settings live in one file, `drift.toml`, next to the game. The engine, every plugin and the game itself read their sections from it, so the window size, the graphics backend, physics gravity, bus volumes and your own tuning values sit in one place, and changing them needs no rebuild.

```toml
[project]
name = "Coin Dash"
organization = "My Studio"

[window]
width = 960
height = 540
scale_mode = "integer"

[render]
clear_color = [0.07, 0.08, 0.12]

[physics]
gravity = [0, 0]

[audio]
music_volume = 0.6
```

A game needs no `drift.toml`; every setting has a default.

## Where Values Come From

Each setting takes the last value set, in this order:

1. The default in the struct.
2. `drift.toml`.
3. Code: `app.config()` and the resources `app.settings<T>()` returns, changed before `run()`.
4. Environment variables, `DRIFT_<SECTION>_<KEY>`: `DRIFT_RENDER_BACKEND=webgpu`.
5. Command-line flags, `--set section.key=value`: `--set window.width=640`.

So the file describes the game, code pins what the game depends on, and the environment and command line change one run without editing either. Values in the environment and on the command line are written as in TOML (`640`, `true`, `[0, 9.8]`, `"text"`); text that is not valid TOML is taken as a string.

Pass the command line to the app first, so `--config` can name the file:

```cpp
int main(int argc, char** argv) {
    App app;
    app.args(argc, argv);                 // --config <path>, --set section.key=value
    app.config().window.width = 576;      // this game is laid out for 576x1024
    app.config().window.height = 1024;
    app.addPlugins<DefaultPlugins>();
    return app.run();
}
```

Change individual fields in code. Assigning a whole struct, such as `app.config().window = {...}`, also resets every field of it that drift.toml set.

## Finding the File

Drift reads the first of:

1. The file `--config <path>` names.
2. The file the `DRIFT_CONFIG` environment variable names.
3. `drift.toml` in the working directory.
4. `drift.toml` beside the executable.

In the browser, `drift_web_app` packages the project's `drift.toml` (see [Installation](../getting-started/installation.md)), and the game finds it the same way.

A syntax error is logged with its line and column, and the file is ignored. A value of the wrong type is logged with the line it is on and what the key needs, and the setting keeps its previous value. A key that no section reads is logged as a likely typo, with the keys the section has.

## Engine Sections

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| **[project]** | | | |
| `name` | string | `"Drift"` | The game's name. The window title when `window.title` is empty, and the save directory name. |
| `organization` | string | `""` | The save directory's parent; see [Save Data](../gameplay/save-data.md). |
| `version` | string | `"0.1.0"` | The game's version. |
| `entry` | string | `""` | The script a scripting host runs. |
| `assets` | string | `"assets"` | The directory the web build packages. |
| `description` | string | `""` | One line about the game: the description of a Candela game's web page, for search engines and link previews. |
| `icon` | string | `""` | An image in the game folder: a Candela game's page icon. |
| **[window]** | | | |
| `title` | string | `""` | Window title; empty uses `project.name`. |
| `width`, `height` | integer | `1280`, `720` | The logical resolution the game draws in, and the initial window size. |
| `fullscreen` | bool | `false` | Start fullscreen. |
| `resizable` | bool | `true` | Let the player resize the window. |
| `high_dpi` | bool | `true` | Render at the display's native pixel density. |
| `scale_mode` | name | `"Letterbox"` | How the logical resolution fits the window: `Letterbox`, `Stretch`, `Integer` or `Expand`, in any case. See [Rendering](../subsystems/rendering.md#resolution-and-scale-modes). |
| **[render]** | | | |
| `backend` | string | `""` | Graphics backend: `"gl"` or `"webgpu"`. Empty picks the first in the build. See [Rendering](../subsystems/rendering.md#backends). |
| `vsync` | bool | `true` | Wait for the display between frames. Turn it off to measure uncapped frame rates. |
| `clear_color` | color | `[0, 0, 0, 1]` | The color every frame starts from. |
| **[app]** | | | |
| `threads` | integer | `0` | Threads for the system scheduler: `0` picks one fewer than the CPU has, at most 7; `1` runs every system on the main thread; more sets the count. The web build always uses one. |

In code these are `app.config().project`, `.window`, `.render` and `.app`, with the same names in camelCase: `app.config().window.scaleMode`, `app.config().app.threads`.

## Plugin Sections

Plugins read their own sections:

| Section | Keys | Page |
|---------|------|------|
| `[physics]` | `gravity` = `[x, y]`, default `[0, 9.8]`; `pixels_per_meter`, default 32, restart-only | [Physics](../subsystems/physics-resource.md#units-and-scale) |
| `[audio]` | `master_volume`, `music_volume`, `sfx_volume`, 0 to 1, default 1 | [Audio](../subsystems/audio.md) |
| `[lighting]` | `enabled`, `ambient`, `layer_mask`, `resolution_scale` | [Lighting](../components/lighting.md) |
| `[hot_reload]` | `enabled`, `interval` (seconds between checks) | [Asset Server](../subsystems/asset-server.md#hot-reload) |
| `[input]` | One key per action, a list of bindings: `jump = ["key:Space", "button:South"]` | [Input Actions](../gameplay/input-actions.md) |
| `[random]` | `seed`, the game's seed; 0 (the default) picks one each run and logs it. Restart-only | [Random Numbers and Noise](../math/random-and-noise.md#the-games-seed) |

Physics and audio settings are where the world and the mixer start; change them while the game runs through `PhysicsResource` and `AudioResource`. Lighting and hot reload read theirs every frame.

Value types: a color is `[r, g, b]` or `[r, g, b, a]` with channels from 0 to 1; a vector is `[x, y]`; an enum is the value's name as a string.

## Your Own Sections

Bind a section to a resource of your own. The struct lists its fields once, with the names drift.toml uses:

```cpp
struct Rules : Resource {
    DRIFT_RESOURCE(Rules)
    int32_t lives = 3;
    float enemySpeed = 120.f;
    Vec2 spawn = {64.f, 64.f};

    static void describe(reflect::TypeBuilder<Rules>& t) {
        t.field("lives", &Rules::lives)
         .field("enemy_speed", &Rules::enemySpeed)
         .field("spawn", &Rules::spawn);
    }
};

int main(int argc, char** argv) {
    App app;
    app.args(argc, argv);
    app.settings<Rules>("rules");         // [rules] in drift.toml
    ...
}

void spawnEnemy(Res<Rules> rules, Commands& cmd) { ... rules->enemySpeed ... }
```

`settings<T>()` adds the resource on first use and returns it, so systems read it as `Res<Rules>` like any other resource. Field types are those [reflection](../concepts/reflection.md) describes. A field cannot be called `name`, because every resource has a `name()` method.

To read a value without a struct, use the `Settings` resource. Paths are dotted, and a missing value or one of another type returns the fallback:

```cpp
void setup(Res<Settings> settings) {
    double rate = settings->getFloat("level.spawn.rate", 1.0);
    std::string start = settings->getString("level.start", "intro");
}
```

`has`, `getInt`, `getFloat`, `getBool` and `getString` see environment and `--set` values too. `valueJson(path)` returns a value or a whole table as JSON text, which is how a scripting host reads a section. The free function `tomlToJson(toml, json, error)` converts any TOML text the same way, for data files kept in TOML.

## Live Reload

Builds with `DRIFT_DEV` (the default) watch `drift.toml` with the other assets. When you save it, the keys whose value changed in the file are applied while the game runs, and the log names them. A key removed from the file goes back to its default. A key the environment or `--set` pins keeps its value. A parse error leaves every setting as it was.

Some keys only take effect on restart, and saving a change to one logs that instead: everything in `[project]`, `window.width`, `window.height`, `window.high_dpi`, `render.backend`, `render.vsync` and `app.threads`.

To react to a change of your own section, register a callback; it receives the names of the keys that changed:

```cpp
app.onSettingsChanged("rules", [](const std::vector<std::string>& keys) {
    DRIFT_LOG_INFO("rules changed: %zu keys", keys.size());
});
```

A section's options name its own restart-only keys: `app.settings<Rules>("rules", {.restart = {"lives"}})`.
