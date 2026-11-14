#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/Query.hpp>
#include <drift/components/AudioSource.hpp>
#include <drift/components/GlobalTransform.hpp>
#include <drift/resources/AudioResource.hpp>
#include <drift/resources/Time.hpp>

#include <unordered_map>

namespace drift {

// Playback started by AudioSource components, keyed by entity, so the
// plugin can stop it when the entity or component goes away.
struct AudioSourceTracking : public Resource {
    DRIFT_RESOURCE(AudioSourceTracking)
    std::unordered_map<EntityId, PlayingSoundHandle> playing;
    std::unordered_map<EntityId, bool> seen;
};

inline Vec2 audioWorldPosition(const Transform2D* t, const GlobalTransform2D* g) {
    if (g) return g->position;
    if (t) return t->position;
    return {};
}

inline void audioSourceUpdate(ResMut<AudioResource> audio,
                              ResMut<AudioSourceTracking> tracking,
                              Query<AudioListener, Optional<Transform2D>,
                                    Optional<GlobalTransform2D>> listeners,
                              QueryMut<AudioSource, Optional<Transform2D>,
                                       Optional<GlobalTransform2D>> sources) {
    bool haveListener = false;
    Vec2 listenerPos;
    listeners.iter([&](const AudioListener& l, const Transform2D* t,
                       const GlobalTransform2D* g) {
        if (haveListener || !l.active) return;
        haveListener = true;
        listenerPos = audioWorldPosition(t, g);
    });

    auto& seen = tracking->seen;
    for (auto& [id, flag] : seen) flag = false;

    sources.iterWithEntity([&](EntityId id, AudioSource& src, Transform2D* t,
                               GlobalTransform2D* g) {
        bool first = seen.find(id) == seen.end();
        seen[id] = true;

        float gain = src.volume;
        float pan = 0.f;
        if (src.spatial && haveListener) {
            SpatialMix m = spatialize(listenerPos, audioWorldPosition(t, g), src);
            gain *= m.gain;
            pan = m.pan;
        }

        if (src.playRequested || (first && src.autoplay)) {
            src.playRequested = false;
            audio->stopSound(src.instance);
            PlayParams p;
            p.bus = src.bus;
            p.volume = gain;
            p.pan = pan;
            p.pitch = src.pitch;
            p.loop = src.loop;
            p.priority = src.priority;
            src.instance = audio->playSound(src.sound, p);
            tracking->playing[id] = src.instance;
            return;
        }
        if (!audio->isSoundPlaying(src.instance)) return;
        audio->setSoundVolume(src.instance, gain);
        audio->setSoundPan(src.instance, pan);
        audio->setSoundPitch(src.instance, src.pitch);
        audio->setSoundLooping(src.instance, src.loop);
    });

    // Stop playback whose entity or component disappeared.
    for (auto it = seen.begin(); it != seen.end();) {
        if (it->second) { ++it; continue; }
        auto p = tracking->playing.find(it->first);
        if (p != tracking->playing.end()) {
            audio->stopSound(p->second, 0.05f);
            tracking->playing.erase(p);
        }
        it = seen.erase(it);
    }
}

inline void audioUpdate(ResMut<AudioResource> audio, Res<Time> time) {
    audio->update(time->delta);
}

// The [audio] section of drift.toml: starting volumes of the built-in
// buses, 0..1. Change them while running through AudioResource.
struct AudioSettings : public Resource {
    DRIFT_RESOURCE(AudioSettings)

    float masterVolume = 1.f;
    float musicVolume = 1.f;
    float sfxVolume = 1.f;

    static void describe(reflect::TypeBuilder<AudioSettings>& t) {
        t.field("master_volume", &AudioSettings::masterVolume)
         .field("music_volume", &AudioSettings::musicVolume)
         .field("sfx_volume", &AudioSettings::sfxVolume);
    }
};

class AudioPlugin : public Plugin {
public:
    void settings(App& app) override { app.settings<AudioSettings>("audio"); }
    void build(App& app) override {
        auto* audio = app.addResource<AudioResource>();
        auto& settings = *app.getResource<AudioSettings>();
        auto apply = [audio, &settings](const std::vector<std::string>&) {
            audio->setBusVolume(audio->masterBus(), settings.masterVolume);
            audio->setBusVolume(audio->musicBus(), settings.musicVolume);
            audio->setBusVolume(audio->sfxBus(), settings.sfxVolume);
        };
        apply({});
        app.onSettingsChanged("audio", apply);
        auto* assets = app.getResource<AssetServer>();
        assets->setSoundLoader([audio](const char* path) {
            return audio->loadSound(path);
        });
        assets->setReloader("sound", [audio](uint32_t id, const std::string& path) {
            return audio->reloadSound(SoundHandle{id}, path.c_str());
        });
        app.world().registerComponent<AudioSource>("AudioSource");
        app.world().registerComponent<AudioListener>("AudioListener");
        app.initResource<AudioSourceTracking>();
        app.postUpdate<audioSourceUpdate>("audio_sources");
        app.postUpdate<audioUpdate>("audio_update");
    }
    DRIFT_PLUGIN(AudioPlugin)
};

} // namespace drift
