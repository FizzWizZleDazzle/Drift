#pragma once

#include <cstdint>
#include <string>

namespace drift {

struct Config {
    std::string title = "Drift";
    int32_t width = 1280;
    int32_t height = 720;
    bool fullscreen = false;
    bool vsync = true;
    bool resizable = true;
    int32_t threadCount = 0;  // 0 = auto (hardware_concurrency-1, capped at 7), 1 = sequential
};

} // namespace drift
