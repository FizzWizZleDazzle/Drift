# Projects and the drift Command

A Drift game is a folder:

```
my_game/
  drift.toml        settings: name, window, engine and your own sections
  main.cdl          the entry script
  src/              more scripts, imported from main.cdl
  assets/           images, sounds, fonts, maps and scenes
```

Only `drift.toml` and the entry script are required. Run `drift` commands from this folder.

## drift.toml

The settings for the whole game. Every key has a default, so a file holding just `[project]` works.

```toml
[project]
name = "My Game"
organization = "My Studio"   # with name, where save data lives
entry = "main.cdl"           # the script drift run starts

[window]
width = 1280                 # logical resolution
height = 720
scale_mode = "letterbox"     # letterbox, stretch, integer, expand

[render]
clear_color = [0, 0, 0]

[physics]
gravity = [0, 900]

[audio]
music_volume = 0.7

[lighting]                   # its presence turns lighting on
ambient = [0.1, 0.1, 0.15]

[input]                      # actions and their bindings
jump = ["key:Space", "button:South"]

[random]
seed = 0                     # 0 picks a new seed each run and logs it

[my_game]                    # your own section, read by a @settings struct
lives = 3
```

| Section | Keys |
|---------|------|
| `[project]` | `name`, `organization`, `version`, `entry`, `assets`; `description` and `icon` (an image path) for the web page |
| `[window]` | `title` (empty uses `project.name`), `width`, `height`, `fullscreen`, `resizable`, `high_dpi`, `scale_mode` |
| `[render]` | `backend` (`"gl"` or `"webgpu"`), `vsync`, `clear_color` |
| `[app]` | `threads` for the engine's scheduler: 0 picks for the machine, 1 runs everything on one thread |
| `[physics]` | `gravity` |
| `[audio]` | `master_volume`, `music_volume`, `sfx_volume` |
| `[lighting]` | `enabled`, `ambient`, `layer_mask`, `resolution_scale` |
| `[hot_reload]` | `enabled`, `interval` |
| `[input]` | One key per action, a list of bindings: `key:<name>`, `mouse:Left`, `button:South` |
| `[random]` | `seed` for the `Random` resource; see [Procedural Generation](../guides/procedural-generation.md#the-games-seed) |
| `[web]` | The web page: `orientation`, `background`, `fullscreen_button`, `touch_controls`, `touch_stick`, `touch_buttons`, `offline`; see [Web Games](../guides/web.md#the-page) |

Values apply in order: the default, then `drift.toml`, then your systems, then `DRIFT_<SECTION>_<KEY>` environment variables, then `--set section.key=value` on the command line. So `DRIFT_RENDER_BACKEND=webgpu drift run` tries the WebGPU backend for one run, and `drift run --set window.width=640` checks a smaller window.

A key a section does not have, or a value of the wrong type, is logged with its line when the game starts.

## Scripts and imports

`drift run` compiles the entry script and everything it imports. Split a game into files the usual candela way:

```rust
// main.cdl
import "drift" { * };
import "./src/player.cdl";
import "./src/enemies.cdl";
```

A file import is relative to the file that writes it: `./src/player.cdl` from `main.cdl` is `src/player.cdl` in the project, and `src/player.cdl` reaches `src/weapons.cdl` with `import "./weapons.cdl";` and the project folder above it with `../`. Games split this way run the same on the desktop and in the browser.

`import "drift" { * };` brings in every engine name and attribute. A name you declare yourself shadows the imported one, so a game can have its own `Timer` or `log`. Systems, components and resources are found in every imported file, not only the entry script. A system's name is its function name, so two files may not both declare a system called `setup`.

The `drift` module comes with `drift` itself; there is nothing to install. Other candela packages import as usual.

## Assets

Paths in scripts are relative to the project folder: `texture("assets/ship.png")`. Keep assets under the folder `[project] assets` names, `assets` by default; that is the folder shipped with the game.

## Commands

| Command | Does |
|---------|------|
| `drift run` | Compiles and runs the game, reloading scripts and assets as they change |
| `drift build --web` | Writes the game as a web page to serve |
| `drift check` | Compiles without running and lists the systems with their parameters and order |
| `drift build` | Compiles the game into one `.cdlb` file to ship |
| `drift new <name>` | Makes a project folder |

See [The drift Command](../reference/cli.md) for every option.

## Shipping

For the browser, `drift build --web` writes `build/web/`, a page with the game packed beside the web runtime. Upload the folder to any static web host or to itch.io; [Web Games](../guides/web.md) covers hosting, phones and saves.

`drift build` writes `build/<name>.cdlb`. Ship it with the `drift` program, `drift.toml` and the assets folder; `drift run build/<name>.cdlb` starts it without compiling anything.
