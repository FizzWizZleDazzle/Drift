# Installation

Drift is a C++20 2D game engine for desktop (OpenGL 3.3) and the browser (WebGL2). All third-party dependencies are fetched automatically during the CMake configure step, so there is nothing to install manually beyond a compiler and CMake itself.

## Prerequisites

| Tool | Minimum Version | Notes |
|------|----------------|-------|
| **C++ compiler** | GCC 12+, Clang 15+, or MSVC 2022+ (17.4+) | Must support C++20 (designated initializers, concepts, `<concepts>` header) |
| **CMake** | 3.24+ | Required for `FetchContent` features used by the build |
| **Git** | Any recent version | Used by CMake FetchContent to clone dependency repositories |

### Linux-specific requirements

On Linux the build links `m`, `dl`, and `pthread` automatically. Depending on your distribution you may also need the windowing development headers and an OpenGL 3.3 capable driver. On Ubuntu/Debian-based systems:

```bash
sudo apt update
sudo apt install build-essential cmake git libx11-dev libxext-dev \
    libwayland-dev libxkbcommon-dev libegl-dev
```

The exact packages vary by distribution. If you already have a working OpenGL development environment, you likely have everything you need.

### macOS

Xcode Command Line Tools (which provide Clang) and CMake are sufficient. Install CMake through Homebrew if you do not have it:

```bash
brew install cmake
```

### Windows

Visual Studio 2022 with the "Desktop development with C++" workload provides both the compiler and CMake. You can also use CMake from the command line with the Visual Studio generator or Ninja.

## Dependencies (automatic)

Drift uses CMake `FetchContent` to download and build every dependency at configure time. You do **not** need to install any of these manually:

| Library | Version | Purpose |
|---------|---------|---------|
| [SDL3](https://github.com/libsdl-org/SDL) | `main` branch | Windowing, input, audio, OpenGL context |
| [flecs](https://github.com/SanderMertens/flecs) | v4.0.4 | Entity Component System |
| [Box2D](https://github.com/erincatto/box2d) | v3.0.0 | 2D physics simulation |
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.91.8 | Developer overlay and debug UI |
| [stb](https://github.com/nothings/stb) | latest | Image loading (stb_image), PNG screenshots (stb_image_write), font rasterization (stb_truetype), OGG Vorbis decoding (stb_vorbis) |
| [dr_libs](https://github.com/mackron/dr_libs) | pinned commit | WAV (dr_wav) and MP3 (dr_mp3) decoding |
| [cute_aseprite](https://github.com/RandyGaul/cute_headers) | latest | Aseprite sprite sheet loading |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | JSON parsing (used for Tiled map support) |

The first build takes longer because CMake will clone and compile these dependencies. Subsequent builds reuse the cached results in your build directory.

## Clone and build

```bash
git clone https://github.com/FizzWizZleDazzle/Drift.git
cd drift
cmake -B build
cmake --build build
```

That is it. After the build completes, example binaries will be available in the `build/` directory (e.g. `build/hello_sprites`, `build/fireball`).

### Build in Release mode

For optimized builds, specify the build type at configure time:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Build options

Drift exposes several CMake options you can toggle at configure time:

| Option | Default | Description |
|--------|---------|-------------|
| `DRIFT_DEV` | `ON` | Enables the developer overlay and debug tools (ImGui-based). Defines the `DRIFT_DEV=1` preprocessor macro. Turn this off for release/shipping builds. |
| `DRIFT_BUILD_EXAMPLES` | `ON` | Builds the example programs in the `examples/` directory. Turn this off if you only want the engine library. |
| `DRIFT_BACKEND_GL` | `ON` | Builds the OpenGL 3.3 / WebGL2 renderer backend. |
| `DRIFT_BUILD_TESTS` | `ON` when Drift is the top-level project | Builds the engine tests. Run them with `ctest --test-dir build`. |

To change an option, pass it with `-D` during the configure step:

```bash
cmake -B build -DDRIFT_DEV=OFF -DDRIFT_BUILD_EXAMPLES=OFF
cmake --build build
```

## Verifying the build

After building with examples enabled, run one of the example programs to verify everything works:

Examples load their assets relative to the working directory, so run them from their example folder:

```bash
# Linux / macOS
cd examples/hello_sprites && ../../build/hello_sprites

# Windows
cd examples\hello_sprites
..\..\build\Debug\hello_sprites.exe
```

You should see a window with a player sprite you can move with WASD or arrow keys, surrounded by orbiting sprites. If you see this, your build is working correctly.

## Using Drift in your own project

The recommended way to use Drift is via CMake `FetchContent` from your own project. Create a `CMakeLists.txt` like this:

```cmake
cmake_minimum_required(VERSION 3.24)
project(my_game LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    drift
    GIT_REPOSITORY https://github.com/FizzWizZleDazzle/Drift.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
set(DRIFT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(drift)

add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE drift)
```

Then configure and build as usual:

```bash
cmake -B build
cmake --build build
```

CMake will automatically fetch Drift and all of its transitive dependencies. See the [Hello World](hello-world.md) chapter for what to put in `main.cpp`.

## Building for the web

Drift builds for the browser with [Emscripten](https://emscripten.org). The same game code runs unchanged; the web build renders through WebGL2 and runs systems on one thread.

Install the Emscripten SDK once:

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh
```

Then configure with `emcmake` in a separate build directory:

```bash
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
```

Each example becomes an `.html` page with its `.js`, `.wasm` and `.data` files. Browsers will not load WebAssembly from `file://`, so serve the directory:

```bash
cd build-web && python3 -m http.server 8000
# open http://localhost:8000/flappy_bird.html
```

### Web builds of your own game

Call `drift_web_app` on your game target. It packages asset folders, sets the page template, and does nothing in desktop builds, so you can call it unconditionally:

```cmake
add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE drift)
drift_web_app(my_game ASSETS assets)                 # files appear under "assets/"
# drift_web_app(my_game ASSETS assets SHELL web/index.html)   # your own page
```

`PRELOAD <src>@<dst>` maps any other directory into the virtual file system. A custom `SHELL` page must contain a `<canvas id="canvas">` and the `{{{ SCRIPT }}}` placeholder; start from `cmake/drift_shell.html`.

## Shader translation (optional)

Custom shaders written in WGSL are translated to GLSL at build time by [naga](https://github.com/gfx-rs/wgpu/tree/trunk/naga). You only need it if your game uses `drift_add_shaders`:

```bash
cargo install naga-cli
```

See [Shaders and Effects](../subsystems/shaders-and-effects.md).
