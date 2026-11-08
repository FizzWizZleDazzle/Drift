// =============================================================================
// Audio
// =============================================================================
// Spatial sound sources, mixer buses and music. Every sound is synthesized in
// code with AudioResource::createSound, so the example needs no audio files.
//
// Controls: WASD moves the listener (white), drag a source with the left mouse
//           button, Space plays a blip on the sfx bus. The panel sets bus
//           volumes, mutes and pauses buses, and switches music.
// =============================================================================

#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/Query.hpp>
#include <drift/components/AudioSource.hpp>
#include <drift/components/Camera.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/AudioResource.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>
#include <drift/resources/UIResource.hpp>

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

using namespace drift;

namespace {

constexpr int kRate = 44100;
constexpr float kTau = 6.2831853f;

// ---- Synthesis --------------------------------------------------------------

SoundHandle synth(AudioResource& audio, float seconds, const std::function<float(float)>& f) {
    std::vector<float> s(static_cast<size_t>(seconds * kRate));
    for (size_t i = 0; i < s.size(); ++i) s[i] = f(static_cast<float>(i) / kRate);
    return audio.createSound(s.data(), s.size(), 1, kRate);
}

float tri(float phase) { return 4.f * std::fabs(phase - std::floor(phase + 0.5f)) - 1.f; }
float saw(float phase) { return 2.f * (phase - std::floor(phase + 0.5f)); }

// An arpeggio over `notes` (Hz), eighth notes at 120 bpm.
SoundHandle arpeggio(AudioResource& audio, const std::vector<float>& notes, float seconds) {
    return synth(audio, seconds, [&](float t) {
        float step = 0.25f;
        int i = static_cast<int>(t / step);
        float local = t - i * step;
        float hz = notes[i % notes.size()];
        float env = std::exp(-local * 6.f);
        float bass = 0.15f * tri(t * notes[0] * 0.5f);
        return 0.25f * env * tri(local * hz) + bass;
    });
}

// ---- Game state -------------------------------------------------------------

struct Emitter {
    const char* name;
    Color color;
};

struct Demo : Resource {
    DRIFT_RESOURCE(Demo)
    SoundHandle blip;
    SoundHandle trackA, trackB, intro;
    BusHandle ambience;
    EntityId listener = InvalidEntityId;
    EntityId dragging = InvalidEntityId;
    std::vector<std::pair<EntityId, Emitter>> sources;
    float busVolume[4] = {1.f, 0.6f, 1.f, 1.f};
    const char* status = "music: track A";
    bool uiWantsMouse = false;

    const Emitter* find(EntityId id) const {
        for (auto& [eid, e] : sources)
            if (eid == id) return &e;
        return nullptr;
    }
};

void setup(ResMut<AudioResource> audio, ResMut<RendererResource> r, ResMut<Demo> demo,
           Commands& cmd) {
    r->setClearColor({0.07f, 0.08f, 0.11f, 1.f});
    cmd.spawn().insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});

    // Looping ambience. Each loop length is a whole number of periods so
    // the loop point is seamless.
    SoundHandle engine = synth(*audio, 1.f, [](float t) {
        float wobble = 0.6f + 0.4f * std::sin(kTau * 6.f * t);
        return 0.35f * wobble * saw(t * 82.f);
    });
    SoundHandle chime = synth(*audio, 1.f, [](float t) {
        float local = std::fmod(t, 0.5f);
        return 0.4f * std::exp(-local * 9.f) * std::sin(kTau * 880.f * t) *
               (0.7f + 0.3f * std::sin(kTau * 2.f * t));
    });
    SoundHandle drone = synth(*audio, 2.f, [](float t) {
        float swell = 0.5f + 0.5f * std::sin(kTau * 0.5f * t);
        return 0.3f * swell * (tri(t * 220.f) + 0.5f * tri(t * 330.f)) / 1.5f;
    });

    demo->blip = synth(*audio, 0.15f, [](float t) {
        float hz = 600.f + 2400.f * t;
        return 0.5f * std::exp(-t * 20.f) * std::sin(kTau * hz * t);
    });
    audio->setSoundMaxInstances(demo->blip, 4);

    demo->trackA = arpeggio(*audio, {261.6f, 329.6f, 392.f, 523.3f}, 4.f);
    demo->trackB = arpeggio(*audio, {220.f, 261.6f, 329.6f, 440.f, 329.6f, 261.6f}, 3.f);
    demo->intro = synth(*audio, 1.f, [](float t) {
        return 0.2f * t * (tri(t * 261.6f) + tri(t * 329.6f) + tri(t * 392.f)) / 3.f;
    });

    demo->ambience = audio->createBus("ambience");
    audio->setBusVolume(audio->musicBus(), demo->busVolume[1]);
    audio->playMusic(demo->trackA, {.crossfade = 1.f});

    auto spawnSource = [&](Vec2 pos, SoundHandle s, const char* name, Color c) {
        AudioSource src;
        src.sound = s;
        src.bus = demo->ambience;
        src.loop = true;
        src.spatial = true;
        src.minDistance = 60.f;
        src.maxDistance = 520.f;
        EntityId id = cmd.spawn().insert(Transform2D{pos}, src).id();
        demo->sources.push_back({id, Emitter{name, c}});
    };
    spawnSource({-220.f, -170.f}, engine, "engine", Color(255, 140, 80));
    spawnSource({360.f, -160.f}, chime, "chime", Color(120, 200, 255));
    spawnSource({40.f, 220.f}, drone, "drone", Color(170, 255, 140));

    demo->listener = cmd.spawn().insert(Transform2D{{0.f, 0.f}}, AudioListener{}).id();
}

void controls(Res<InputResource> input, Res<Time> time, ResMut<RendererResource> r,
              ResMut<AudioResource> audio, ResMut<Demo> demo,
              QueryMut<Transform2D> transforms) {
    Vec2 mouse = r->screenToWorld(input->mousePosition());
    float speed = 320.f * time->delta;

    if (input->mouseButtonPressed(MouseButton::Left) && !demo->uiWantsMouse) {
        float best = 40.f;
        transforms.iterWithEntity([&](EntityId id, Transform2D& t) {
            float d = (t.position - mouse).length();
            if (demo->find(id) && d < best) {
                best = d;
                demo->dragging = id;
            }
        });
    }
    if (!input->mouseButtonHeld(MouseButton::Left)) demo->dragging = InvalidEntityId;

    transforms.iterWithEntity([&](EntityId id, Transform2D& t) {
        if (id == demo->dragging) t.position = mouse;
        if (id == demo->listener) {
            if (input->keyHeld(Key::A)) t.position.x -= speed;
            if (input->keyHeld(Key::D)) t.position.x += speed;
            if (input->keyHeld(Key::W)) t.position.y -= speed;
            if (input->keyHeld(Key::S)) t.position.y += speed;
        }
    });

    if (input->keyPressed(Key::Space)) {
        float pitch = 0.75f + 0.5f * static_cast<float>(std::rand()) / RAND_MAX;
        audio->playSound(demo->blip, {.volume = 0.8f, .pitch = pitch});
    }
}

void panel(ResMut<UIResource>, ResMut<AudioResource> audio, ResMut<Demo> demo,
           Query<AudioSource, Transform2D> sources, Query<AudioListener, Transform2D> listeners) {
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(330, 0), ImGuiCond_Once);
    ImGui::Begin("Audio");
    demo->uiWantsMouse = ImGui::GetIO().WantCaptureMouse;

    const BusHandle buses[4] = {audio->masterBus(), audio->musicBus(), audio->sfxBus(),
                                demo->ambience};
    const char* names[4] = {"master", "music", "sfx", "ambience"};
    ImGui::SeparatorText("Buses");
    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(i);
        if (ImGui::SliderFloat(names[i], &demo->busVolume[i], 0.f, 1.f))
            audio->setBusVolume(buses[i], demo->busVolume[i], 0.1f);
        bool muted = audio->isBusMuted(buses[i]);
        if (ImGui::Checkbox("mute", &muted)) audio->setBusMuted(buses[i], muted);
        ImGui::SameLine();
        bool paused = audio->isBusPaused(buses[i]);
        if (ImGui::Checkbox("pause", &paused)) audio->setBusPaused(buses[i], paused);
        ImGui::SameLine();
        if (ImGui::SmallButton("fade out")) {
            demo->busVolume[i] = 0.f;
            audio->setBusVolume(buses[i], 0.f, 2.f);
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText("Music");
    if (ImGui::Button("Track A")) {
        audio->playMusic(demo->trackA, {.crossfade = 2.f});
        demo->status = "music: track A (2 s crossfade)";
    }
    ImGui::SameLine();
    if (ImGui::Button("Track B")) {
        audio->playMusic(demo->trackB, {.crossfade = 2.f});
        demo->status = "music: track B (2 s crossfade)";
    }
    ImGui::SameLine();
    if (ImGui::Button("Intro + B")) {
        audio->playMusic(demo->trackB, {.crossfade = 0.5f, .intro = demo->intro});
        demo->status = "music: intro, then track B looping";
    }
    if (ImGui::Button("Playlist A, B")) {
        audio->playPlaylist({demo->trackA, demo->trackB}, {.crossfade = 1.f});
        demo->status = "music: playlist, 1 s crossfades";
    }
    ImGui::SameLine();
    if (ImGui::Button("Next")) audio->nextTrack();
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        audio->stopMusic(1.5f);
        demo->status = "music: stopped";
    }
    ImGui::TextUnformatted(demo->status);
    const char* now = audio->currentMusic() == demo->trackA   ? "A"
                      : audio->currentMusic() == demo->trackB ? "B"
                                                              : "-";
    ImGui::Text("playing: %s%s", now, audio->isMusicPlaying() ? "" : " (paused or stopped)");

    ImGui::SeparatorText("Sources");
    Vec2 lp;
    listeners.iter([&](const AudioListener&, const Transform2D& t) { lp = t.position; });
    sources.iterWithEntity([&](EntityId id, const AudioSource& s, const Transform2D& t) {
        const Emitter* e = demo->find(id);
        SpatialMix m = spatialize(lp, t.position, s);
        ImGui::Text("%-7s gain %.2f  pan %+.2f  %s", e ? e->name : "?", m.gain, m.pan,
                    audio->isSoundPlaying(s.instance) ? "playing" : "stopped");
    });
    ImGui::TextWrapped("WASD moves the listener, drag sources with the mouse, Space plays a blip.");
    ImGui::Text("output: %s", audio->hasDevice() ? "device open" : "no device");
    ImGui::End();
}

void draw(ResMut<RendererResource> r, Res<Demo> demo, Query<AudioSource, Transform2D> sources,
          Query<AudioListener, Transform2D> listeners) {
    Vec2 lp;
    listeners.iter([&](const AudioListener&, const Transform2D& t) { lp = t.position; });

    // Grid for a sense of distance.
    for (int i = -8; i <= 8; ++i) {
        r->drawLine({i * 100.f, -500.f}, {i * 100.f, 500.f}, Color(255, 255, 255, 18), 1.f, -2.f);
        r->drawLine({-800.f, i * 100.f}, {800.f, i * 100.f}, Color(255, 255, 255, 18), 1.f, -2.f);
    }

    sources.iterWithEntity([&](EntityId id, const AudioSource& s, const Transform2D& t) {
        const Emitter* e = demo->find(id);
        Color c = e ? e->color : Color::white();
        SpatialMix m = spatialize(lp, t.position, s);
        Color faint = c;
        faint.a = 40;
        r->drawCircle(t.position, s.maxDistance, faint, 1.f, 64, -1.f);
        Color ring = c;
        ring.a = 110;
        r->drawCircle(t.position, s.minDistance, ring, 2.f, 48, -1.f);
        r->drawCircleFilled(t.position, 14.f, c);
        Color link = c;
        link.a = static_cast<uint8_t>(40 + 215 * m.gain);
        r->drawLine(lp, t.position, link, 1.f + 5.f * m.gain, -0.5f);
    });

    r->drawCircleFilled(lp, 12.f, Color::white());
    r->drawCircle(lp, 20.f, Color(255, 255, 255, 120), 2.f);
}

} // namespace

int main(int argc, char** argv) {
    App app;
    app.args(argc, argv);   // window and project settings: drift.toml
    app.addPlugins<DefaultPlugins>();
    app.addResource<Demo>();
    app.startup<setup>();
    app.update<controls>("controls");
    app.update<panel>("panel");
    app.render<draw>("draw");
    return app.run();
}
