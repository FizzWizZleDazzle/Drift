# Install

`drift` is one program: the engine with the candela compiler built in. Building it needs a C++20 compiler (GCC 12+, Clang 15+ or MSVC 2022+), CMake 3.24+, Git and a Rust toolchain; everything else is fetched during the build.

```sh
git clone https://github.com/FizzWizZleDazzle/Drift.git
cd Drift
cmake -B build -DCMAKE_BUILD_TYPE=Release -DDRIFT_BUILD_CANDELA=ON
cmake --build build --target drift
```

The program is `build/drift`. Put it on your `PATH`, or run it by its full path.

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

`drift` runs on desktops with OpenGL 3.3, or with Vulkan, Metal or Direct3D 12 when built with the WebGPU backend. See [Limitations](../limitations.md) for the platforms Drift is tested on and for the browser.
