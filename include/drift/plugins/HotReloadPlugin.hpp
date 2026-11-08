#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/Resource.hpp>
#include <drift/resources/Time.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace drift {

// Watches the files of loaded assets and reloads the ones that change on
// disk (any asset kind with a reloader: textures, scenes and drift.toml by
// default). Desktop only; on the web nothing is watched. Its settings are
// the [hot_reload] section of drift.toml.
class HotReloadResource : public Resource {
public:
    DRIFT_RESOURCE(HotReloadResource)

    bool enabled = true;
    float interval = 0.5f;   // seconds between checks

    static void describe(reflect::TypeBuilder<HotReloadResource>& t) {
        t.field("enabled", &HotReloadResource::enabled)
         .field("interval", &HotReloadResource::interval);
    }

    // Check every watched file now; returns how many assets reloaded.
    int poll(AssetServer& assets);

private:
    friend class HotReloadPlugin;
    float sinceLastPoll_ = 0.f;
    std::unordered_map<std::string, int64_t> stamps_;   // path -> last seen mtime
};

class HotReloadPlugin : public Plugin {
public:
    void build(App& app) override {
        app.settings<HotReloadResource>("hot_reload");
#ifndef __EMSCRIPTEN__
        // Exclusive: a reload may change entities (scenes) or GL textures.
        app.addSystem("hot_reload", Phase::PreUpdate, [](App& a) {
            auto* hr = a.getResource<HotReloadResource>();
            auto* assets = a.getResource<AssetServer>();
            auto* time = a.getResource<Time>();
            if (!hr || !assets || !hr->enabled) return;
            hr->sinceLastPoll_ += time ? time->delta : 0.f;
            if (hr->sinceLastPoll_ < hr->interval) return;
            hr->sinceLastPoll_ = 0.f;
            hr->poll(*assets);
        });
#endif
    }
    DRIFT_PLUGIN(HotReloadPlugin)
};

} // namespace drift
