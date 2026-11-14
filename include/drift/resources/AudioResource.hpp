#pragma once

#include <drift/Resource.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>

#include <cstddef>
#include <vector>

namespace drift {

struct BusTag {};
using BusHandle = Handle<BusTag>;

// How loadSound keeps a sound in memory.
enum class LoadMode {
    Decode, // decode the whole file up front; best for short effects
    Stream, // keep the compressed file, decode while playing; best for music
};

// Options for one playback of a sound.
struct PlayParams {
    BusHandle bus;          // invalid = the sfx bus
    float volume = 1.f;
    float pan = 0.f;        // -1 left, 0 centre, +1 right
    float pitch = 1.f;      // playback speed; 2 = one octave up
    bool loop = false;
    float fadeIn = 0.f;     // seconds
    int priority = 0;       // higher priority voices are stolen last
};

// Options for playMusic.
struct MusicParams {
    float crossfade = 0.f;  // seconds; the old track fades out while this one fades in
    float volume = 1.f;
    bool loop = true;
    double loopStart = 0.0; // seconds; loops jump back here instead of to the start
    SoundHandle intro;      // optional track played once before the main track
};

// Options for playPlaylist.
struct PlaylistParams {
    float crossfade = 0.f;  // seconds between tracks; 0 = gapless
    bool loop = true;       // start over after the last track
    bool shuffle = false;
    float volume = 1.f;
};

class AudioResource : public Resource {
public:
    AudioResource();
    ~AudioResource() override;

    DRIFT_RESOURCE(AudioResource)

    // Output sample rate of the mixer.
    int sampleRate() const;
    // True when an output device is open. Without one, every call still
    // works and playback advances silently through update().
    bool hasDevice() const;
    // AudioPlugin calls this once per frame. It only does work when no
    // output device is open: it advances playback by `dt` seconds so sounds
    // still finish on time.
    void update(float dt);

    // ---- Sounds --------------------------------------------------------
    // WAV, OGG Vorbis and MP3, detected from the file contents.
    SoundHandle loadSound(const char* path, LoadMode mode = LoadMode::Decode);
    SoundHandle loadSoundFromMemory(const void* data, size_t size,
                                    LoadMode mode = LoadMode::Decode);
    // A sound from raw float samples (interleaved when channels == 2).
    SoundHandle createSound(const float* samples, size_t frames, int channels,
                            int sampleRate);
    void destroySound(SoundHandle sound);
    // Reads `sound` again from `path`, keeping its handle and how it was
    // loaded; voices playing it stop. Hot reload uses it.
    bool reloadSound(SoundHandle sound, const char* path);
    double soundDuration(SoundHandle sound) const; // seconds
    // Caps simultaneous instances of one sound; starting another fades out
    // the oldest. 0 = no cap.
    void setSoundMaxInstances(SoundHandle sound, int maxInstances);

    // ---- Buses ---------------------------------------------------------
    // Every sound plays on a bus. Buses form a tree under master; a bus's
    // volume, mute and pause apply to everything below it.
    BusHandle masterBus() const;
    BusHandle musicBus() const;
    BusHandle sfxBus() const;
    // Parent defaults to master. Returns the existing bus if the name is taken.
    BusHandle createBus(const char* name, BusHandle parent = {});
    BusHandle findBus(const char* name) const;
    void setBusVolume(BusHandle bus, float volume, float fadeSeconds = 0.f);
    float busVolume(BusHandle bus) const;
    void setBusMuted(BusHandle bus, bool muted);
    bool isBusMuted(BusHandle bus) const;
    void setBusPaused(BusHandle bus, bool paused);
    bool isBusPaused(BusHandle bus) const;
    // Stops every sound on the bus and the buses below it.
    void stopBus(BusHandle bus, float fadeOut = 0.f);

    // ---- Playing sounds ------------------------------------------------
    // Returns an invalid handle when every voice is busy with higher
    // priority sounds.
    PlayingSoundHandle playSound(SoundHandle sound, const PlayParams& params = {});
    void stopSound(PlayingSoundHandle playing, float fadeOut = 0.f);
    void pauseSound(PlayingSoundHandle playing, float fadeOut = 0.f);
    void resumeSound(PlayingSoundHandle playing, float fadeIn = 0.f);
    void setSoundVolume(PlayingSoundHandle playing, float volume, float fadeSeconds = 0.f);
    void setSoundPan(PlayingSoundHandle playing, float pan);
    void setSoundPitch(PlayingSoundHandle playing, float pitch);
    void setSoundLooping(PlayingSoundHandle playing, bool loop);
    // True until the instance ends or is stopped; paused instances count.
    bool isSoundPlaying(PlayingSoundHandle playing) const;
    bool isSoundPaused(PlayingSoundHandle playing) const;

    // ---- Music ---------------------------------------------------------
    // One music track plays at a time on the music bus.
    bool playMusic(SoundHandle track, const MusicParams& params = {});
    // Loads the file in Stream mode (once per path) and plays it.
    bool playMusic(const char* path, const MusicParams& params = {});
    bool playPlaylist(const std::vector<SoundHandle>& tracks,
                      const PlaylistParams& params = {});
    void nextTrack();
    void stopMusic(float fadeOut = 0.f);
    void pauseMusic(float fadeOut = 0.f);
    void resumeMusic(float fadeIn = 0.f);
    bool isMusicPlaying() const;
    SoundHandle currentMusic() const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
