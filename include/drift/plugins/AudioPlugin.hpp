#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/resources/AudioResource.hpp>

namespace drift {

#ifndef SWIG
inline void audio_update(ResMut<AudioResource> audio, float) { audio->update(); }
#endif

class AudioPlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        app.addResource<AudioResource>();
        app.addSystem<audio_update>("audio_update", Phase::PostUpdate);
#endif
    }
    DRIFT_PLUGIN(AudioPlugin)
};

} // namespace drift
