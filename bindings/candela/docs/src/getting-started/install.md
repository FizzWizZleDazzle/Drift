# Install

`drift` is one program: the engine with the candela compiler built in. It runs games on the desktop and builds them as web pages.

On Linux and macOS:

```sh
curl -fsSL https://raw.githubusercontent.com/FizzWizZleDazzle/Drift/main/install.sh | sh
```

On Windows, in PowerShell:

```powershell
irm https://raw.githubusercontent.com/FizzWizZleDazzle/Drift/main/install.ps1 | iex
```

The installer downloads the latest release for your platform, checks it against the release's checksums and unpacks it under `~/.drift`. It offers to put `~/.drift/bin` on your `PATH`; on Windows it adds it to your user `PATH`. Nothing else is needed: the `drift` module, the candela standard library and the web runtime come with it.

| Option | Does |
|--------|------|
| `--version 0.4.0` | Installs that release instead of the latest |
| `--force` | Reinstalls the version already installed |
| `--uninstall` | Removes every file the installer put under `~/.drift` |
| `--prefix <dir>` | Installs somewhere other than `~/.drift` |
| `--no-modify-path` | Leaves your shell configuration alone |
| `--from <dir or URL>` | Installs from a folder or URL holding the archive and `sha256sums.txt`, such as a build of your own |

Pass options to the script after `sh -s --`:

```sh
curl -fsSL https://raw.githubusercontent.com/FizzWizZleDazzle/Drift/main/install.sh | sh -s -- --version 0.4.0
```

The PowerShell installer takes the same options as `-Version`, `-Force`, `-Uninstall`, `-Prefix`, `-NoModifyPath` and `-From`. To pass them, run it as a script block: `& ([scriptblock]::Create((irm https://raw.githubusercontent.com/FizzWizZleDazzle/Drift/main/install.ps1))) -Version 0.4.0`.

Run the installer again to update. It replaces the old version and removes files the new one no longer has.

Check it:

```sh
drift --version
drift run path/to/game
```

## In the browser

```sh
drift build --web path/to/game
cd path/to/game/build/web && python3 -m http.server 8000   # open http://localhost:8000
```

`drift build --web` writes a page that runs the game: `index.html`, the web runtime (`drift.js`, `drift.wasm`) and `game.pack`, which holds `drift.toml`, every `.cdl` script in the game folder at its path in the folder, and the assets folder. Hidden folders and build output folders (`build`, `build-*`) are left out. The page compiles the scripts when it loads, so a game split across subfolders runs as it does on the desktop. Serve the folder over HTTP; browsers do not run WebAssembly from `file://` pages. `-o <folder>` writes the page somewhere other than `build/web`. [Web Games](../guides/web.md) covers hosting, itch.io, phones, offline play and saves.

## Building from source

Building `drift` needs a C++20 compiler (GCC 12+, Clang 15+ or MSVC 2022+), CMake 3.24+, Git and a Rust toolchain; everything else is fetched during the build.

```sh
git clone https://github.com/FizzWizZleDazzle/Drift.git
cd Drift
cmake -B build -DCMAKE_BUILD_TYPE=Release -DDRIFT_BUILD_CANDELA=ON
cmake --build build --target drift_run
```

The program is `build/bindings/candela/drift`, with the candela standard library it was built with in `build/bindings/candela/libs`. To build against a candela checkout instead of the released crate, add `-DDRIFT_CANDELA_SOURCE=<path to candela>`.

The web runtime comes from an [Emscripten](https://emscripten.org) build, which also needs the `wasm32-unknown-emscripten` Rust target (`rustup target add wasm32-unknown-emscripten`):

```sh
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release -DDRIFT_BUILD_CANDELA=ON
cmake --build build-web --target drift_web
DRIFT_WEB_RUNTIME=build-web/bindings/candela/web build/bindings/candela/drift build --web path/to/game
```

An Emscripten build also writes a page for each example the same way, `build-web/bindings/candela/flappy_bird_cdl/` and `coin_dash_cdl/`, and for each game folder listed in `-DDRIFT_CANDELA_WEB_GAMES="/path/to/my_game;/path/to/other"`, as `play_<folder name>/`.

## Editor support

The candela language server reads the `drift` module like any other, so completion, hover and errors work in `.cdl` files, attributes included. Point your editor's candela extension at the language server as the candela book describes.

## Platforms

Releases carry `drift` for Linux (x86_64), macOS (Apple silicon) and Windows (x86_64). It runs on desktops with OpenGL 3.3, or with Vulkan, Metal or Direct3D 12 when built with the WebGPU backend. Pages from `drift build --web` run in browsers with WebGL2. See [Limitations](../limitations.md) for the platforms Drift is tested on.
