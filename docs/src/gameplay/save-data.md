# Save Data

`SaveData` is a persistent key-value store for settings, progress and high scores. Each save slot is one JSON file in the per-user data directory, and values are anything [nlohmann/json](https://github.com/nlohmann/json) can convert: numbers, strings, bools, vectors, maps, or your own types with `to_json`/`from_json`.

**Header:** `#include <drift/gameplay/SaveData.hpp>`

**Plugin:** `SavePlugin` (not in `DefaultPlugins`; it needs your game's name)

## Setup

```cpp
app.addPlugins<DefaultPlugins>();
app.addPlugin(new SavePlugin("MyStudio", "MyGame"));          // slot "save"
```

The file is `<user data dir>/<org>/<app>/<slot>.json`. The directory comes from `SDL_GetPrefPath`:

| Platform | Directory |
|----------|-----------|
| Linux | `~/.local/share/<org>/<app>/` (or `$XDG_DATA_HOME`) |
| Windows | `%APPDATA%\<org>\<app>\` |
| macOS | `~/Library/Application Support/<org>/<app>/` |

For several slots, construct more `SaveData` objects yourself: `SaveData slot2("MyStudio", "MyGame", "slot2");`. `openFile(path)` uses an explicit path instead.

## Reading and writing

```cpp
void onGameOver(ResMut<SaveData> save, Res<Score> score) {
    if (score->value > save->get("highscore", 0)) {
        save->set("highscore", score->value);
        save->flush();
    }
}
```

| Call | Effect |
|------|--------|
| `get(key, fallback)` | The value, or `fallback` if missing or of another type |
| `set(key, value)` | Store a value (in memory) |
| `has(key)`, `erase(key)`, `clear()` | Inspect and remove |
| `json()` | The whole document, for nested data: `save->json()["options"]["volume"] = 0.8;` |
| `flush()` | Write to disk |
| `load()` | Re-read from disk, discarding unsaved changes |
| `dirty()` | True when there are unsaved changes |

Nothing is written until you call `flush()`. Call it at checkpoints and when settings change, not every frame.

`flush()` writes a temporary file and renames it over the old one, so a crash during a save keeps the previous save intact. A save file that does not parse is reported in the log and the store starts empty.

## Web

On the web the save directory lives in Emscripten's in-memory filesystem. `flush()` already calls `FS.syncfs` after writing; mounting the directory as IndexedDB storage (IDBFS) and loading it at startup is not wired up yet, so web saves do not survive a page reload.
