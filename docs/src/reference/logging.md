# Logging

**Header:** `include/drift/Log.hpp`

Drift provides a lightweight logging system with five severity levels and printf-style formatting.

## Macros

| Macro | Level | Description |
|-------|-------|-------------|
| `DRIFT_LOG_TRACE(...)` | Trace | Verbose diagnostic output. Typically disabled in release builds. |
| `DRIFT_LOG_DEBUG(...)` | Debug | Development-time information for tracking program flow. |
| `DRIFT_LOG_INFO(...)` | Info | Normal operational messages (startup, shutdown, resource loading). |
| `DRIFT_LOG_WARN(...)` | Warn | Something unexpected happened but the engine can continue. |
| `DRIFT_LOG_ERROR(...)` | Error | Something failed. The operation could not be completed. |

## Format

All macros use **printf-style** format strings:

```cpp
DRIFT_LOG_INFO("Window created: %dx%d", width, height);
DRIFT_LOG_DEBUG("Entity %llu spawned at (%.1f, %.1f)", entityId, pos.x, pos.y);
DRIFT_LOG_WARN("Texture not found: %s", path.c_str());
DRIFT_LOG_ERROR("Failed to initialize audio: error code %d", result);
DRIFT_LOG_TRACE("Frame %d delta: %.4f", frameCount, dt);
```

## Implementation

The macros expand to calls to `drift::log()`:

```cpp
namespace drift {
    void log(LogLevel level, const char* fmt, ...);
}

#define DRIFT_LOG_TRACE(...) ::drift::log(::drift::LogLevel::Trace, __VA_ARGS__)
#define DRIFT_LOG_DEBUG(...) ::drift::log(::drift::LogLevel::Debug, __VA_ARGS__)
#define DRIFT_LOG_INFO(...)  ::drift::log(::drift::LogLevel::Info,  __VA_ARGS__)
#define DRIFT_LOG_WARN(...)  ::drift::log(::drift::LogLevel::Warn,  __VA_ARGS__)
#define DRIFT_LOG_ERROR(...) ::drift::log(::drift::LogLevel::Error, __VA_ARGS__)
```

The `log()` function is implemented in `src/core/Log.cpp`. It writes to standard output with a level prefix and timestamp.

## Log Levels

The `LogLevel` enum is defined in `include/drift/Types.hpp`:

```cpp
enum class LogLevel {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
};
```

Levels are ordered by severity. `Trace` is the most verbose and `Error` is the most severe.
