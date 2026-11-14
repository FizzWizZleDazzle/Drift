#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/Resource.hpp>
#include <drift/resources/Time.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace drift {

// Reloads loaded assets whose files change: any asset kind with a reloader
// (textures, sounds, fonts, scenes and drift.toml by default, and the kinds
// other plugins add). On the desktop it watches the files on disk. In the
// browser nothing is watched: a page that writes new files into the file
// system names them with filesChanged(), as pages built with
// `drift build --web --dev` do. Its settings are the [hot_reload] section of
// drift.toml.
class HotReloadResource : public Resource {
public:
    DRIFT_RESOURCE(HotReloadResource)

    bool enabled = true;
    float interval = 0.5f;   // seconds between checks of the files on disk

    static void describe(reflect::TypeBuilder<HotReloadResource>& t) {
        t.field("enabled", &HotReloadResource::enabled)
         .field("interval", &HotReloadResource::interval);
    }

    // Reloads the assets whose files changed: the files named with
    // filesChanged, and on the desktop every watched file whose time
    // stamp moved. Returns how many assets reloaded.
    int poll(AssetServer& assets);

    // Files that changed without the watcher seeing them, by their path in
    // the game folder ("main.cdl", "assets/ship.png"). The next poll
    // reloads every asset loaded from them.
    static void filesChanged(const std::vector<std::string>& paths);

private:
    friend class HotReloadPlugin;
    float sinceLastPoll_ = 0.f;
    std::unordered_map<std::string, int64_t> stamps_;   // path -> last seen mtime
};

class HotReloadPlugin : public Plugin {
public:
    void build(App& app) override {
        app.settings<HotReloadResource>("hot_reload");
        // Exclusive: a reload may change entities (scenes) or GL textures.
        app.addSystem("hot_reload", Phase::PreUpdate, [](App& a) {
            auto* hr = a.getResource<HotReloadResource>();
            auto* assets = a.getResource<AssetServer>();
            if (!hr || !assets || !hr->enabled) return;
#ifdef __EMSCRIPTEN__
            hr->poll(*assets);   // only the files the page named
#else
            auto* time = a.getResource<Time>();
            hr->sinceLastPoll_ += time ? time->delta : 0.f;
            if (hr->sinceLastPoll_ < hr->interval) return;
            hr->sinceLastPoll_ = 0.f;
            hr->poll(*assets);
#endif
        });
    }
    DRIFT_PLUGIN(HotReloadPlugin)
};

} // namespace drift
