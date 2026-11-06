# Configuration

**Header:** `include/drift/Config.hpp`

The `Config` struct controls the window, the logical resolution, the graphics backend and engine threading. Pass it to `App::setConfig()` before calling `app.run()`.

## Config Struct

```cpp
struct Config {
    std::string title = "Drift";
    int32_t width     = 1280;
    int32_t height    = 720;
    bool fullscreen   = false;
    bool vsync        = true;
    bool resizable    = true;
    bool highDpi      = true;
    ScaleMode scaleMode = ScaleMode::Letterbox;
    std::string renderer;
    int32_t threadCount = 0;
};
```

## Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `title` | `std::string` | `"Drift"` | Window title bar text. |
| `width` | `int32_t` | `1280` | Logical width the game draws in, and the initial window width. |
| `height` | `int32_t` | `720` | Logical height the game draws in, and the initial window height. |
| `fullscreen` | `bool` | `false` | Start in fullscreen mode. |
| `vsync` | `bool` | `true` | Enable vertical sync. Disable for uncapped frame rate (useful for stress tests). |
| `resizable` | `bool` | `true` | Allow the user to resize the window. |
| `highDpi` | `bool` | `true` | Render at the display's native pixel density on high-DPI screens. |
| `scaleMode` | `ScaleMode` | `Letterbox` | How the logical resolution fits the window: `Letterbox`, `Stretch`, `Integer` (pixel perfect) or `Expand`. See [Rendering](../subsystems/rendering.md#resolution-and-scale-modes). |
| `renderer` | `std::string` | `""` | Graphics backend by name: `"gl"` or `"webgpu"`. Empty picks the first backend in the build (OpenGL when it is built). The `DRIFT_RENDERER` environment variable overrides it. See [Rendering](../subsystems/rendering.md#backends). |
| `threadCount` | `int32_t` | `0` | Worker thread count for the system scheduler. See below. |

## Thread Count

The `threadCount` field controls how many worker threads the engine's system scheduler uses:

| Value | Behavior |
|-------|----------|
| `0` | **Auto** (default). Uses `hardware_concurrency - 1`, capped at 7. This is the recommended setting for most games. The web build always runs sequentially. |
| `1` | **Sequential mode**. All systems run on the main thread. Useful for debugging data races or for very simple games where threading overhead is not worthwhile. |
| `2+` | Explicit thread count. Use this if you need fine control, for example when profiling or running on known hardware. |

## Usage

Use C++20 designated initializers for concise configuration:

```cpp
App app;
app.setConfig({
    .title = "My Game",
    .width = 1920,
    .height = 1080,
    .fullscreen = false,
    .vsync = true,
});
```

Only specify the fields you want to change; the rest keep their defaults:

```cpp
// Just change the title and disable vsync
app.setConfig({.title = "Stress Test", .vsync = false});
```

## Examples from the Codebase

**Hello Sprites**: standard window:

```cpp
app.setConfig({.title = "Drift - Hello Sprites", .width = 1280, .height = 720});
```

**Flappy Bird**: small fixed-size window:

```cpp
app.setConfig({
    .title = "Flappy Bird - Drift Engine",
    .width = 576,    // 288 * 2
    .height = 1024,  // 512 * 2
    .resizable = false,
});
```

**Asteroid Field**: vsync disabled for stress testing:

```cpp
app.setConfig({
    .title = "Drift - Asteroid Field (Stress Test)",
    .width = 1280,
    .height = 720,
    .vsync = false,
});
```
