# Installation

Drift is a C++20 game engine with an SDL3 GPU backend. All third-party dependencies are fetched automatically during the CMake configure step, so there is nothing to install manually beyond a compiler and CMake itself.

## Prerequisites

| Tool | Minimum Version | Notes |
|------|----------------|-------|
| **C++ compiler** | GCC 12+, Clang 15+, or MSVC 2022+ (17.4+) | Must support C++20 (designated initializers, concepts, `<concepts>` header) |
| **CMake** | 3.24+ | Required for `FetchContent` features used by the build |
| **Git** | Any recent version | Used by CMake FetchContent to clone dependency repositories |

### Linux-specific requirements

On Linux the build links `m`, `dl`, and `pthread` automatically. Depending on your distribution you may also need the SDL3 development headers and a GPU driver that supports Vulkan or OpenGL. On Ubuntu/Debian-based systems:

```bash
sudo apt update
sudo apt install build-essential cmake git libx11-dev libxext-dev \
    libwayland-dev libxkbcommon-dev libegl-dev
```

The exact packages vary by distribution. If you already have a working Vulkan or OpenGL development environment, you likely have everything you need.

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
| [SDL3](https://github.com/libsdl-org/SDL) | `main` branch | Windowing, input, GPU rendering backend |
| [flecs](https://github.com/SanderMertens/flecs) | v4.0.4 | Entity Component System |
| [Box2D](https://github.com/erincatto/box2d) | v3.0.0 | 2D physics simulation |
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.91.8 | Developer overlay and debug UI |
| [stb](https://github.com/nothings/stb) | latest | Image loading (stb_image), font rasterization (stb_truetype) |
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

To change an option, pass it with `-D` during the configure step:

```bash
cmake -B build -DDRIFT_DEV=OFF -DDRIFT_BUILD_EXAMPLES=OFF
cmake --build build
```

## Verifying the build

After building with examples enabled, run one of the example programs to verify everything works:

```bash
# Linux / macOS
./build/hello_sprites

# Windows (from the repo root)
.\build\Debug\hello_sprites.exe
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
