# Install

`drift` is one program: the engine with the Candela compiler built in. It builds games as web pages, and runs them on the desktop while you work.

## Install drift

On Linux and macOS:

```sh
curl -fsSL https://raw.githubusercontent.com/FizzWizZleDazzle/Drift/main/install.sh | sh
```

On Windows, in PowerShell:

```powershell
irm https://raw.githubusercontent.com/FizzWizZleDazzle/Drift/main/install.ps1 | iex
```

The installer downloads the latest release for your platform, checks it against the release's checksums and unpacks it under `~/.drift`. It offers to put `~/.drift/bin` on your `PATH`; on Windows it adds it to your user `PATH`. The `drift` module, the Candela standard library and the web runtime come with it.

Check it:

```sh
drift --version
```

## Build a page and play it

```sh
drift build --web path/to/game
cd path/to/game/build/web
python3 -m http.server 8000
```

Open <http://localhost:8000> in a browser. `drift build --web` writes `index.html`, the web runtime (`drift.js`, `drift.wasm`) and `game.pack`, which holds the game's scripts, `drift.toml` and assets. Serve the folder over HTTP: browsers do not run WebAssembly from `file://` pages. [Publishing to the Web](../guides/publishing.md) covers putting the page online.

`drift run path/to/game` plays the same game in a desktop window.

## Installer options

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

## Editor support

The Candela extension for VS Code, from version 0.4.0, gives `.cdl` files completion, hover, signature help and go-to-definition for the `drift` module, attributes included. Hovering a name shows its documentation, the same text as its entry in this book's [reference](../reference/index.md), with an "Open docs" link to that entry; a struct's hover lists its fields with their defaults, and an enum's its variants. Version 0.4.0 is not on the marketplace yet; it comes from lumen-fx/candela#251. Two settings point it at the right files:

```json
{
    "candela.importRoots": { "drift": "~/.drift/lib/drift/module" },
    "candela.languageServerPath": "/path/to/candela-lsp"
}
```

`candela.importRoots` tells it where `import "drift"` lives; with an installed toolchain that is `~/.drift/lib/drift/module`. `candela.languageServerPath` points at a `candela-lsp` built from lumen-fx/candela#251, which reads `export import` (the `drift` module is written with it) and the module's `///` doc comments. One built before lumen-fx/candela#250 reports an error on the import and knows none of the engine's names; one built before #251 shows no documentation and no docs link. Without the two settings the editor colors the code but does not know the engine's names.

## Building drift from source

You need this only to change Drift itself. Building needs a C++20 compiler (GCC 12+, Clang 15+ or MSVC 2022+), CMake 3.24+, Git and a Rust toolchain; everything else is fetched during the build. The web runtime comes from an [Emscripten](https://emscripten.org) build, which also needs the `wasm32-unknown-emscripten` Rust target:

```sh
git clone https://github.com/FizzWizZleDazzle/Drift.git
cd Drift
rustup target add wasm32-unknown-emscripten
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release -DDRIFT_BUILD_CANDELA=ON
cmake --build build-web
```

That writes the web runtime to `build-web/bindings/candela/web/` and a page for every example to `build-web/bindings/candela/<example>_cdl/`. A game folder of your own builds the same way when you list it: `-DDRIFT_CANDELA_WEB_GAMES="/path/to/my_game"` writes `play_my_game/`.

The desktop program is the `drift_run` target of a normal build (`cmake -B build -DDRIFT_BUILD_CANDELA=ON`), written to `build/bindings/candela/drift`. Point it at your web runtime with `DRIFT_WEB_RUNTIME=build-web/bindings/candela/web`. To build against a Candela checkout instead of the released crate, add `-DDRIFT_CANDELA_SOURCE=<path to candela>`. The [engine book](https://fizzwizzledazzle.github.io/Drift/getting-started/installation.html) covers the build options.

## Platforms

Pages from `drift build --web` run in browsers with WebGL2. Releases carry `drift` for Linux (x86_64), macOS (Apple silicon) and Windows (x86_64). See [Limitations](../limitations.md) for what is tested.
