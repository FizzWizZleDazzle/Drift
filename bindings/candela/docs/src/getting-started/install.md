# Install

`drift` is one program: the engine with the candela compiler built in. Building it needs a C++20 compiler (GCC 12+, Clang 15+ or MSVC 2022+), CMake 3.24+, Git and a Rust toolchain; everything else is fetched during the build.

```sh
git clone https://github.com/FizzWizZleDazzle/Drift.git
cd Drift
cmake -B build -DCMAKE_BUILD_TYPE=Release -DDRIFT_BUILD_CANDELA=ON
cmake --build build --target drift_run
```

The program is `build/bindings/candela/drift`. Put it on your `PATH`, or run it by its full path. Scripts that import the standard library (`std/random`, `std/math`, ...) read it from the installed candela toolchain, `~/.candela/libs`, or from `CANDELA_LIB_PATH`.

To build against a candela checkout instead of the released crate, add `-DDRIFT_CANDELA_SOURCE=<path to candela>`.

## In the browser

Build with [Emscripten](https://emscripten.org) and the `wasm32-unknown-emscripten` Rust target (`rustup target add wasm32-unknown-emscripten`). Each game is its own page, made with `drift_candela_web_game(<target> PROJECT <folder>)` in CMake; the example builds as `flappy_bird_cdl`:

```sh
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release -DDRIFT_BUILD_CANDELA=ON
cmake --build build-web --target flappy_bird_cdl
cd build-web/bindings/candela && python3 -m http.server 8000   # open flappy_bird_cdl.html
```

The page carries the game's scripts, `drift.toml` and assets, and compiles the scripts when it loads. To build your own game folders the same way, list them in `-DDRIFT_CANDELA_WEB_GAMES="/path/to/my_game;/path/to/other"`; each becomes the target `<folder name>_cdl`.

Check it:

```sh
drift new hello
cd hello
drift run
```

A window opens with a square you move with the arrow keys.

## Editor support

The candela language server reads the `drift` module like any other, so completion, hover and errors work in `.cdl` files, attributes included. Point your editor's candela extension at the language server as the candela book describes.

## Platforms

`drift` runs on desktops with OpenGL 3.3, or with Vulkan, Metal or Direct3D 12 when built with the WebGPU backend. Candela games also run in the browser; see above. See [Limitations](../limitations.md) for the platforms Drift is tested on.
