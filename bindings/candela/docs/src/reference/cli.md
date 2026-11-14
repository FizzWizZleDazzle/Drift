# The drift Command

`drift` builds Candela games as web pages and runs them on the desktop. It reads `drift.toml` in the game folder.

## drift build --web

```sh
drift build --web                        # the game in this folder, to build/web
drift build --web path/to/game           # to path/to/game/build/web
drift build --web path/to/game -o site   # to site/
drift build --web --dev                  # a page that hot reloads
```

Writes the game as a web page: `index.html`, the web runtime `drift.js` and `drift.wasm`, and `game.pack`, which holds `drift.toml`, every `.cdl` script in the folder and its subfolders, and the assets folder. Hidden files and folders and build output folders (`build`, `build-*`) are left out. No compiler or SDK is needed; the runtime comes with `drift`.

The page takes its title, description and icon from `[project]`, and its touch controls, orientation, colors and offline mode from `[web]` in `drift.toml`. With `[project] icon` it also writes the icon beside `index.html`; with `[web] offline = true`, `manifest.webmanifest` and `sw.js`. A `[web]` value it does not understand stops the build with a message naming the key.

It reports an error and writes nothing when the folder has no entry script, and stops before writing `index.html` when `[project] icon` names a missing file. Serve the folder over HTTP to play it; see [Publishing to the Web](../guides/publishing.md).

With `--dev` the page also gets `drift-dev.js`, which watches `game.pack` on the server and swaps each new build into the running game; see [Hot reload while you work](../guides/publishing.md#hot-reload-while-you-work). A dev page ignores `[web] offline`. Build without `--dev` to publish.

`drift build` without `--web` stops with a message: the web page is the only build.

## drift run

```sh
drift run                    # the game in this folder: the entry script drift.toml names
drift run path/to/game       # the game in that folder
drift run main.cdl           # a script
```

Compiles the scripts, opens a window and runs the game. Given a folder, `drift run` plays the game there as if started from inside it: `drift.toml`, the entry script and asset paths are read from that folder. A compile error prints with its file, line and a pointer at the code, the game does not start, and `drift` exits with status 1.

While the game runs, `drift run` watches its files. Saving a script, a data file the game read or `drift.toml` swaps the new code into the running game the way a dev page does ([Hot reload while you work](../guides/publishing.md#hot-reload-while-you-work)); saving an image, sound, font or scene loads it again. A change that does not compile prints the error and the game keeps running the code it had. A change that cannot move into the running game prints why; start the game again to run it.

| Option | Does |
|--------|------|
| `--config <path>` | Read this file instead of `./drift.toml` |
| `--set section.key=value` | Override one setting for this run; repeat for more |

Settings also come from `DRIFT_<SECTION>_<KEY>` environment variables; `--set` wins over them. See [Settings and drift.toml](../concepts/settings.md#overriding-settings).

## Other commands

| Command | Does |
|---------|------|
| `drift --version` | Prints the version |
| `drift --help` | Prints the commands and options |

## Environment

| Variable | Does |
|----------|------|
| `DRIFT_WEB_RUNTIME` | The folder `drift build --web` copies the runtime from (`drift.html`, `drift.js`, `drift.wasm`, and `drift-dev.js` for `--dev`), instead of the one installed with `drift` |
| `DRIFT_CANDELA_MODULE` | The folder of the `drift` module, instead of the one installed with `drift` |
| `CANDELA_LIB_PATH` | The Candela standard library, instead of the copy installed with `drift` |
