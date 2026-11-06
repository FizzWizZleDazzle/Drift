# Audio

`AudioResource` plays sound effects and music. It decodes WAV, OGG Vorbis and MP3, mixes every playing sound through a tree of buses (master, music, sfx and any you add), and handles music crossfades, intro-plus-loop tracks and playlists. The `AudioSource` and `AudioListener` components add 2D positional sound to entities.

**Header:** `#include <drift/resources/AudioResource.hpp>`

**Components:** `#include <drift/components/AudioSource.hpp>`

**Plugin:** `AudioPlugin` (included in `DefaultPlugins`)

## Loading Sounds

```cpp
SoundHandle loadSound(const char* path, LoadMode mode = LoadMode::Decode);
SoundHandle loadSoundFromMemory(const void* data, size_t size, LoadMode mode = LoadMode::Decode);
SoundHandle createSound(const float* samples, size_t frames, int channels, int sampleRate);
void        destroySound(SoundHandle sound);
double      soundDuration(SoundHandle sound) const;   // seconds
```

The format is detected from the file contents, so the extension does not matter. Mono and stereo files play as they are; files with more channels keep their first two.

`LoadMode` picks how the sound is kept in memory:

| Mode | What it does | Use it for |
|------|--------------|------------|
| `LoadMode::Decode` | Decodes the whole file when it loads. Playback costs almost nothing. | Short sound effects |
| `LoadMode::Stream` | Keeps the compressed file and decodes while playing. | Music and long ambience |

`createSound` builds a sound from raw float samples (interleaved when stereo), which is how you play procedurally generated audio.

Load sounds once, in a startup system, and keep the handles. `assets->load<Sound>(path)` on the [Asset Server](asset-server.md) calls `loadSound(path)` in `Decode` mode.

```cpp
struct GameAudio : public Resource {
    DRIFT_RESOURCE(GameAudio)
    SoundHandle jump;
    SoundHandle coin;
    SoundHandle theme;
};

void loadAudio(ResMut<AudioResource> audio, ResMut<GameAudio> ga) {
    ga->jump  = audio->loadSound("assets/sfx/jump.wav");
    ga->coin  = audio->loadSound("assets/sfx/coin.ogg");
    ga->theme = audio->loadSound("assets/music/theme.ogg", LoadMode::Stream);
}
```

## Playing Sounds

```cpp
PlayingSoundHandle playSound(SoundHandle sound, const PlayParams& params = {});

struct PlayParams {
    BusHandle bus;          // invalid = the sfx bus
    float volume = 1.f;
    float pan = 0.f;        // -1 left, 0 centre, +1 right
    float pitch = 1.f;      // playback speed; 2 = one octave up
    bool loop = false;
    float fadeIn = 0.f;     // seconds
    int priority = 0;       // higher priority voices are stolen last
};
```

Each call starts a new instance and returns a handle to it. Any number of instances of one sound can play at once. Fields you leave out keep their defaults:

```cpp
audio->playSound(ga->jump);
audio->playSound(ga->coin, {.volume = 0.7f, .pitch = 1.2f});
auto hum = audio->playSound(ga->engine, {.loop = true, .fadeIn = 0.5f});
```

### Controlling an Instance

```cpp
void stopSound(PlayingSoundHandle playing, float fadeOut = 0.f);
void pauseSound(PlayingSoundHandle playing, float fadeOut = 0.f);
void resumeSound(PlayingSoundHandle playing, float fadeIn = 0.f);
void setSoundVolume(PlayingSoundHandle playing, float volume, float fadeSeconds = 0.f);
void setSoundPan(PlayingSoundHandle playing, float pan);
void setSoundPitch(PlayingSoundHandle playing, float pitch);
void setSoundLooping(PlayingSoundHandle playing, bool loop);
bool isSoundPlaying(PlayingSoundHandle playing) const;
bool isSoundPaused(PlayingSoundHandle playing) const;
```

Fade times are in seconds. A stop without a fade still ramps down over a few milliseconds so the cut does not click. `isSoundPlaying` stays true while an instance is paused and turns false as soon as you stop it. Handles to finished instances are safe to pass to any of these functions; the calls do nothing.

```cpp
void engineSound(ResMut<AudioResource> audio, Res<GameState> state,
                 Query<Velocity2D, With<PlayerTag>> query) {
    query.iter([&](const Velocity2D& vel) {
        float speed = std::clamp(vel.linear.length() / 500.f, 0.f, 1.f);
        audio->setSoundVolume(state->engineSound, speed);
        audio->setSoundPitch(state->engineSound, 0.8f + 0.6f * speed);
    });
}
```

### Instance Limits and Priority

Up to 64 sounds play at the same time. Two controls decide what gives way when a game asks for more:

```cpp
void setSoundMaxInstances(SoundHandle sound, int maxInstances); // 0 = no cap
```

With a cap, starting one more instance of that sound fades out its oldest instance. Use it for sounds that fire in bursts, such as footsteps or gunfire.

When all voices are busy, a new sound replaces the lowest priority voice, oldest first. If every playing voice has a higher `priority` than the new sound, `playSound` returns an invalid handle and the new sound does not play. Music always outranks sound effects.

## Buses

Every sound plays on a bus. Buses form a tree: `music` and `sfx` sit under `master`, and buses you create go under `master` unless you give a parent. A bus's volume, mute and pause apply to everything below it.

```cpp
BusHandle masterBus() const;
BusHandle musicBus() const;
BusHandle sfxBus() const;
BusHandle createBus(const char* name, BusHandle parent = {});
BusHandle findBus(const char* name) const;
void  setBusVolume(BusHandle bus, float volume, float fadeSeconds = 0.f);
float busVolume(BusHandle bus) const;      // the target volume
void  setBusMuted(BusHandle bus, bool muted);
bool  isBusMuted(BusHandle bus) const;
void  setBusPaused(BusHandle bus, bool paused);
bool  isBusPaused(BusHandle bus) const;
void  stopBus(BusHandle bus, float fadeOut = 0.f);
```

`createBus` returns the existing bus when the name is already taken. Pausing a bus freezes its sounds in place; they continue from the same point when it resumes. A volume set with a fade time ramps there smoothly, which makes ducking and scene transitions one call:

```cpp
void setupBuses(ResMut<AudioResource> audio, ResMut<GameAudio> ga) {
    ga->ui    = audio->createBus("ui");
    ga->voice = audio->createBus("voice", audio->sfxBus());
}

void openPauseMenu(ResMut<AudioResource> audio, Res<GameAudio> ga) {
    audio->setBusPaused(audio->sfxBus(), true);          // game sounds freeze
    audio->setBusVolume(audio->musicBus(), 0.3f, 0.5f);  // music ducks
    audio->playSound(ga->click, {.bus = ga->ui});        // menu sounds still play
}
```

A settings menu maps its sliders straight to bus volumes:

```cpp
audio->setBusVolume(audio->musicBus(), settings.musicVolume);
audio->setBusVolume(audio->sfxBus(), settings.effectsVolume);
```

## Music

One music track plays at a time, on the music bus.

```cpp
bool playMusic(SoundHandle track, const MusicParams& params = {});
bool playMusic(const char* path, const MusicParams& params = {});
void stopMusic(float fadeOut = 0.f);
void pauseMusic(float fadeOut = 0.f);
void resumeMusic(float fadeIn = 0.f);
bool isMusicPlaying() const;
SoundHandle currentMusic() const;

struct MusicParams {
    float crossfade = 0.f;  // seconds; the old track fades out while this one fades in
    float volume = 1.f;
    bool loop = true;
    double loopStart = 0.0; // seconds; loops jump back here instead of to the start
    SoundHandle intro;      // optional track played once before the main track
};
```

The `path` overload loads the file in `Stream` mode the first time and reuses it afterwards.

```cpp
audio->playMusic("assets/music/town.ogg");
// Later: the town theme fades out while the battle theme fades in.
audio->playMusic(ga->battle, {.crossfade = 2.f});
```

### Intro and Loop

Many game tracks have an intro that plays once and a body that loops. Drift supports both ways of authoring that, and both switch without a gap:

```cpp
// Two files: the intro plays once, then the body loops.
audio->playMusic(ga->bossLoop, {.intro = ga->bossIntro});

// One file: after the first pass, loops restart 12.5 seconds in.
audio->playMusic(ga->boss, {.loopStart = 12.5});
```

### Playlists

```cpp
bool playPlaylist(const std::vector<SoundHandle>& tracks, const PlaylistParams& params = {});
void nextTrack();

struct PlaylistParams {
    float crossfade = 0.f;  // seconds between tracks; 0 = gapless
    bool loop = true;       // start over after the last track
    bool shuffle = false;
    float volume = 1.f;
};
```

A playlist plays its tracks in order (or shuffled, never the same track twice in a row), crossfading from each into the next. `nextTrack` skips ahead with the same crossfade. `playMusic` or `stopMusic` ends the playlist.

```cpp
audio->playPlaylist({ga->ambient1, ga->ambient2, ga->ambient3},
                    {.crossfade = 3.f, .shuffle = true});
```

## Spatial Audio

Add an `AudioSource` to an entity to play a sound from it, and an `AudioListener` to the entity you hear from (usually the camera or the player). With `spatial` on, the source gets quieter with distance from the listener and pans left or right with its horizontal offset.

```cpp
struct AudioSource {
    SoundHandle sound;
    BusHandle bus;               // invalid = the sfx bus
    float volume = 1.f;
    float pitch = 1.f;
    bool loop = false;
    bool autoplay = true;        // start when the component first appears
    bool spatial = false;
    float minDistance = 64.f;    // full volume inside this radius
    float maxDistance = 1024.f;  // silent beyond this radius
    Rolloff rolloff = Rolloff::Linear;
    int priority = 0;
    bool playRequested = false;  // set to start (or restart) playback
    PlayingSoundHandle instance; // the current playback, set by AudioPlugin
};

struct AudioListener {
    bool active = true;
};
```

```cpp
void spawnWaterfall(Commands& cmd, Res<GameAudio> ga) {
    cmd.spawn().insert(Transform2D{{800.f, 120.f}},
                       AudioSource{.sound = ga->waterfall, .loop = true, .spatial = true,
                                   .minDistance = 100.f, .maxDistance = 900.f});
}

void spawnCamera(Commands& cmd) {
    cmd.spawn().insert(Transform2D{}, Camera{.zoom = 1.f, .active = true}, AudioListener{});
}
```

Each frame `AudioPlugin` updates the volume, pan, pitch and looping of every source from its component, so you change a playing source by editing the component. Positions come from `GlobalTransform2D` when the entity has one, otherwise from `Transform2D`.

- **Starting and restarting.** A source with `autoplay` starts the first frame it exists. Set `playRequested` to start it again (for a one-shot sound such as a footstep on the entity). Use `instance` with the `AudioResource` functions to pause or stop it.
- **Removal.** When the entity is despawned or the component is removed, its sound fades out quickly.
- **Rolloff.** `Rolloff::Linear` falls in a straight line from full volume at `minDistance` to silence at `maxDistance`. `Rolloff::Inverse` falls quickly near the source, the way real sound does, and still reaches silence at `maxDistance`.
- **Pan.** The pan is the horizontal offset from the listener divided by `maxDistance`, so a source halfway out on the right plays at pan 0.5. Sources directly above or below the listener are centred.
- **No listener.** Without an active listener, spatial sources play at their own volume, centred.

`spatialize(listenerPos, sourcePos, source)` returns the gain and pan the plugin uses, if you want to show them or apply them to a sound you play yourself.

## Threads and the Web Build

The mixer runs in SDL's audio stream callback. On desktop SDL calls it from its audio thread; in the browser the Web Audio callback drives it on the main thread, so audio needs no threads of its own. Every `AudioResource` function is safe to call from any system. Browsers start audio output only after the first click or key press on the page.

If no audio device can be opened, `hasDevice()` returns false and everything else keeps working without output: `AudioPlugin` advances playback each frame, so sounds still end on time and a game behaves the same on a machine without sound.

## Limitations

- Panning uses a balance law: a centred sound plays at full volume on both sides, and panning turns the far side down.
- There are no effects (reverb, filters) on buses yet.
