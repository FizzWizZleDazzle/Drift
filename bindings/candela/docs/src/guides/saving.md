# Saving

`Save` keeps data between runs: high scores, unlocked levels, options, a whole saved game. It is a key-value store: values go in under string keys and come back out by the same keys.

`Save` is a key-value store kept as one JSON file per slot in the per-user data directory, named after `organization` and `name` in the `[project]` section of `drift.toml`:

| Platform | Directory |
|----------|-----------|
| Linux | `~/.local/share/<organization>/<name>/` |
| Windows | `%APPDATA%\<organization>\<name>\` |
| macOS | `~/Library/Application Support/<organization>/<name>/` |
| Web | The browser's `localStorage` for the page's site |

```rust
@system
fn game_over(_: Contact<Player, Hazard>, save: Save, game: Game) {
    if game.score > save.get_int("best", 0) {
        save.set_int("best", game.score);
        save.flush();
    }
}
```

- `get_int`, `get_float`, `get_string` and `get_bool` return the fallback for a missing key or one of another type.
- `set_*` changes the slot in memory. Nothing reaches the disk until `flush()`, so call it at checkpoints and when options change, not every frame.
- `flush()` writes a new file and renames it over the old one, so a crash during a save keeps the previous save.
- A slot file that does not parse is reported in the log, and the slot starts empty.

The default slot is `save`. `save.slot("slot2")` opens another one in the same directory, for several save games:

```rust
let slot = save.slot("slot" + string(menu.choice));
slot.set_int("level", progress.level);
slot.flush();
```

## A complete example

This program counts how many times it has started and keeps the best score. In the browser, reload the page: the visit count goes up and the best score stays. [Run it in the browser](../play/gallery/saving/visits/).

```toml
[project]
name = "Visits"
organization = "Drift Book"
```

```rust
{{#include ../../gallery/saving/visits/main.cdl:load}}

{{#include ../../gallery/saving/visits/main.cdl:best}}
```

<details>
<summary>The whole program</summary>

```rust
{{#include ../../gallery/saving/visits/main.cdl:all}}
```

</details>

Things to get right:

- `[project]` names the save. Two games with the same `organization` and `name` read and write the same slot; renaming the game starts it with no save.
- Read with a fallback that suits a first run: on the first start every key is missing, and `get_int("visits", 0)` returns 0.
- Call `flush()` after the `set_*` calls you want kept. A value set but never flushed is gone on the next start.

## Whole values

`set_value(key, value)` stores any value as JSON in one key, and `get_value<T>(key, fallback)` reads it back, or returns `fallback` when the key is missing. A struct holding the player's progress, or an `Inventory`, `Flags` or `Quests` from [drift/rpg](game-systems.md), saves in one call:

```rust
struct Progress { level: int = 1, coins: int = 0, unlocked: string[] = [] }

@system(schedule: Schedule::Startup)
fn load_progress(save: Save, game: Game) {
    game.progress = save.get_value<Progress>("progress", Progress { .. });
}

@system
fn checkpoint(_: ReachedCheckpoint, save: Save, game: Game) {
    save.set_value("progress", game.progress);
    save.flush();
}
```

A field the stored JSON lacks, such as one added in a later version of the game, reads as its type's zero value.

Save data methods act at once.

## What to keep

Keep what you need to rebuild the game, not the entities themselves: the level number, the player's position, the score, the cells of a grid as a string. [Save and Load](../examples/save-load.md) keeps a whole picture as a string of digits under a key per slot, and reads it back with `parse`. For options, keep each one under its own key and read them at startup, falling back to `drift.toml` defaults, as [Menus and States](../examples/menu.md) does.

## In the browser

`Save` works the same in the browser as on the desktop. The slot is kept in the browser's `localStorage` for the page's site, so it survives reloads and browser restarts. `flush()` stores it before returning; call it at checkpoints as you would on the desktop, and the save is kept even if the player closes the tab right after.

The browser can refuse storage: in a sandboxed iframe without `allow-same-origin`, when the player blocks site data, or when the site's quota (usually about 5 MB) is full. `flush()` then fails with `drift_save_failed` and the slot lives only as long as the page. Clearing the site's data in the browser deletes the save.

Games on one site that share `organization` and `name` in `[project]` share a slot, as they would on one computer. Give each game its own `name`.

See [`Save`](../reference/resources.md#save) for every method.
