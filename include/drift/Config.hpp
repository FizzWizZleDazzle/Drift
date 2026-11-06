#pragma once

#include <drift/Types.hpp>

#include <cstdint>
#include <string>

namespace drift {

struct Config {
    std::string title = "Drift";
    int32_t width = 1280;               // logical resolution; also the initial window size
    int32_t height = 720;
    bool fullscreen = false;
    bool vsync = true;
    bool resizable = true;
    bool highDpi = true;                // render at the display's native pixel density
    ScaleMode scaleMode = ScaleMode::Letterbox;
    // Graphics backend by name ("gl", "webgpu"). Empty picks the first one
    // compiled in; if the requested backend fails to start, the others are
    // tried in order. The DRIFT_RENDERER environment variable overrides this.
    std::string renderer;
    int32_t threadCount = 0;  // 0 = auto (hardware_concurrency-1, capped at 7), 1 = sequential
};

} // namespace drift
