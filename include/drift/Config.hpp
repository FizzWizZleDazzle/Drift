#pragma once

#include <drift/Types.hpp>
#include <drift/Reflect.hpp>

#include <cstdint>
#include <string>

namespace drift {

// The engine's own settings, one struct per drift.toml section. Each value
// comes from its default, then drift.toml, then code (app.config()), then
// DRIFT_<SECTION>_<KEY> environment variables and --set flags. See
// docs/src/getting-started/configuration.md.

// [project]: what the game is. Save data, `drift run` and the web build read it.
struct ProjectConfig {
    std::string name = "Drift";
    std::string organization;            // save data lives under organization/name
    std::string version = "0.1.0";
    std::string entry;                   // script a scripting host runs (`drift run`)
    std::string assets = "assets";       // directory the web build packages
    std::string description;             // one line; the web page's description for search and link previews
    std::string icon;                    // image file, relative to the game folder; the web page's icon

    static void describe(reflect::TypeBuilder<ProjectConfig>& t) {
        t.field("name", &ProjectConfig::name)
         .field("organization", &ProjectConfig::organization)
         .field("version", &ProjectConfig::version)
         .field("entry", &ProjectConfig::entry)
         .field("assets", &ProjectConfig::assets)
         .field("description", &ProjectConfig::description)
         .field("icon", &ProjectConfig::icon);
    }
};

// [window]: the window and the logical resolution.
struct WindowConfig {
    std::string title;                   // empty uses project.name
    int32_t width = 1280;                // logical resolution; also the initial window size
    int32_t height = 720;
    bool fullscreen = false;
    bool resizable = true;
    bool highDpi = true;                 // render at the display's native pixel density
    ScaleMode scaleMode = ScaleMode::Letterbox;

    static void describe(reflect::TypeBuilder<WindowConfig>& t) {
        t.field("title", &WindowConfig::title)
         .field("width", &WindowConfig::width)
         .field("height", &WindowConfig::height)
         .field("fullscreen", &WindowConfig::fullscreen)
         .field("resizable", &WindowConfig::resizable)
         .field("high_dpi", &WindowConfig::highDpi)
         .field("scale_mode", &WindowConfig::scaleMode);
    }
};

// [render]: the graphics backend and what every frame starts from.
struct RenderConfig {
    // Backend by name ("gl", "webgpu"). Empty picks the first one compiled
    // in; if the requested backend fails to start, the others are tried in
    // order.
    std::string backend;
    bool vsync = true;
    ColorF clearColor = {0.f, 0.f, 0.f, 1.f};

    static void describe(reflect::TypeBuilder<RenderConfig>& t) {
        t.field("backend", &RenderConfig::backend)
         .field("vsync", &RenderConfig::vsync)
         .field("clear_color", &RenderConfig::clearColor);
    }
};

// [app]: how the frame runs.
struct RunConfig {
    int32_t threads = 0;   // 0 = auto (hardware_concurrency-1, capped at 7), 1 = sequential

    static void describe(reflect::TypeBuilder<RunConfig>& t) {
        t.field("threads", &RunConfig::threads);
    }
};

struct Config {
    ProjectConfig project;
    WindowConfig window;
    RenderConfig render;
    RunConfig app;

    // The window title in effect: window.title, else project.name.
    const std::string& title() const { return window.title.empty() ? project.name : window.title; }
};

} // namespace drift
