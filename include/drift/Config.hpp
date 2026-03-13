#pragma once

#include <cstdint>

namespace drift {

struct Config {
    const char* title = "Drift";
    int32_t width = 1280;
    int32_t height = 720;
    bool fullscreen = false;
    bool vsync = true;
    bool resizable = true;
};

} // namespace drift
