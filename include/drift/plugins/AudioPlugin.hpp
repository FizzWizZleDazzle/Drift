#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/resources/AudioResource.hpp>

namespace drift {

#ifndef SWIG
inline void audio_update(ResMut<AudioResource> audio) { audio->update(); }
#endif

class AudioPlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        auto* audio = app.addResource<AudioResource>();
        auto* assets = app.getResource<AssetServer>();
        assets->setSoundLoader([audio](const char* path) {
            return audio->loadSound(path);
        });
        app.addSystem<audio_update>("audio_update", Phase::PostUpdate);
#endif
    }
    DRIFT_PLUGIN(AudioPlugin)
};

} // namespace drift
