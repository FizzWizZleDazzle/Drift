# A Game Folder

A Drift game is a folder:

```
my_game/
  drift.toml        settings: name, window, physics, input and your own sections
  main.cdl          the entry script
  src/              more scripts, imported from main.cdl
  assets/           images, sounds, music and maps
```

Only `drift.toml` and the entry script are required. Run `drift` commands from this folder, or pass the folder: `drift build --web path/to/my_game`.

## drift.toml

The settings for the whole game. Every key has a default, so a file holding only `[project]` works:

```toml
[project]
name = "My Game"
entry = "main.cdl"
```

[Settings and drift.toml](../concepts/settings.md) lists every section.

## Scripts and imports

`drift build --web` packs the entry script and every other `.cdl` file in the folder and its subfolders; the page compiles them when it loads. Split a game into files the usual Candela way:

```rust
// main.cdl
import "drift" { * };
import "./src/player.cdl";
import "./src/enemies.cdl";
```

A file import is relative to the file that writes it: `./src/player.cdl` from `main.cdl` is `src/player.cdl` in the folder, and from there `../` reaches the folder above. Each file that uses the engine imports it with `import "drift" { * };`.

`import "drift" { * };` brings in every engine name and attribute. A name you declare yourself shadows the imported one, so a game can have its own `Timer` or `log`. Systems, components and resources are found in every imported file, not only the entry script. A system's name is its function name, so two files may not both declare a system called `setup`.

`import "drift/ui" { * };` adds the [HUD and menu module](../guides/ui-and-score.md). The Candela standard library imports as usual, such as `import "std/string" as string;`.

## Assets

Paths in scripts are relative to the game folder: `texture("assets/ship.png")`. Keep assets under the folder `[project] assets` names, `assets` by default; `drift build --web` packs that folder into the page. See [Assets](../concepts/assets.md).

## Commands

| Command | Does |
|---------|------|
| `drift build --web` | Writes the game as a web page in `build/web` |
| `drift build --web --dev` | The same page, which [hot reloads](../guides/publishing.md#hot-reload-while-you-work) the game when you build again |
| `drift run` | Plays the game in a desktop window |

See [The drift Command](../reference/cli.md) for every option, and [Publishing to the Web](../guides/publishing.md) for putting the page online.
