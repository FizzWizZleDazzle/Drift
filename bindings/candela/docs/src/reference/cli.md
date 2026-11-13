# The drift Command

`drift` runs, checks and builds candela games. It reads `drift.toml` in the game folder unless `--config` names another.

## drift run

```sh
drift run                    # the game in this folder: the entry script drift.toml names
drift run path/to/game       # the game in that folder
drift run main.cdl           # a script
drift run game.cdlb          # a built game
```

Compiles the script and the modules it imports, opens the window and runs the game. Given a folder, `drift run` plays the game there as if started from inside it: `drift.toml`, the entry script and asset paths are read from that folder. A compile error prints with its file, line and a pointer at the code, the game does not start, and `drift` exits with status 1.

## drift check

```sh
drift check
```

Compiles without running: types, attributes and system parameters. It prints every system with its parameters, conditions, states and the order it runs in. Editors and CI run it.

## drift build --web

```sh
drift build --web                        # the game in this folder, to build/web
drift build --web path/to/game           # to path/to/game/build/web
drift build --web path/to/game -o site   # to site/
```

Writes the game as a web page: `index.html`, the web runtime `drift.js` and `drift.wasm`, and `game.pack`, which holds `drift.toml`, every `.cdl` script in the folder and its subfolders, and the assets folder. Hidden files and folders and build output folders (`build`, `build-*`) are left out. The page title is the `[project] name`. No compiler or SDK is needed; the runtime comes with `drift`.

Serve the folder over HTTP to play it, for example `python3 -m http.server` inside it; browsers do not run WebAssembly from `file://` pages.

`drift build --web` reports an error and writes nothing when the folder has no entry script.

## drift build

```sh
drift build                  # writes build/<name>.cdlb
drift build -o game.cdlb
```

Compiles the game into one `.cdlb` file that `drift run` starts without compiling anything. Ship it with `drift.toml` and the assets folder. Not built yet: `drift build` without `--web` stops with a message saying so.

## drift new

```sh
drift new my_game
```

Makes a folder with a `drift.toml`, a `main.cdl` that opens a window and moves a square, and an empty `assets/`.

## Options

These go after the subcommand and apply to `run`:

| Option | Does |
|--------|------|
| `--config <path>` | Read this file instead of `./drift.toml` |
| `--set section.key=value` | Override one setting for this run; repeat for more |
| `--no-reload` | Do not watch the scripts and `drift.toml` for changes |

Settings also come from `DRIFT_<SECTION>_<KEY>` environment variables; `--set` wins over them. See [Resources and Settings](../concepts/resources.md#settings).

## Other commands

| Command | Does |
|---------|------|
| `drift --version` | Prints the version |
| `drift --help` | Prints the commands and options |

## Environment

| Variable | Does |
|----------|------|
| `DRIFT_WEB_RUNTIME` | The folder `drift build --web` copies the runtime from (`drift.html`, `drift.js`, `drift.wasm`), instead of the one installed with `drift` |
| `DRIFT_CANDELA_MODULE` | The folder of the `drift` module, instead of the one installed with `drift` |
| `CANDELA_LIB_PATH` | The candela standard library, instead of the copy installed with `drift` |

