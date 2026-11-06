#pragma once

// The software mixer behind AudioResource. It has no device or thread of its
// own: mix() renders the next block of interleaved stereo float samples, and
// every parameter change takes effect at the next 256-frame sub-block.
// AudioResource drives it from SDL's audio stream callback; tests drive it
// directly to render audio offline.

#include <drift/resources/AudioResource.hpp>

#include "audio/Decoder.hpp"

#include <memory>
#include <string>
#include <vector>

namespace drift::audio {

class Mixer {
public:
    static constexpr int kSubBlock = 256;
    static constexpr int kMaxVoices = 64;

    explicit Mixer(int sampleRate);
    ~Mixer();

    int sampleRate() const { return sampleRate_; }

    // Renders `frames` stereo frames into `out` (overwrites it).
    void mix(float* out, int frames);

    // Sounds
    SoundHandle addEncoded(Bytes bytes, LoadMode mode);
    SoundHandle addPcm(const float* samples, size_t frames, int channels, int rate);
    void destroySound(SoundHandle s);
    double soundDuration(SoundHandle s) const;
    uint64_t soundFrames(SoundHandle s) const;
    int soundChannels(SoundHandle s) const;
    int soundRate(SoundHandle s) const;
    void setSoundMaxInstances(SoundHandle s, int maxInstances);

    // Buses
    BusHandle masterBus() const { return BusHandle{1}; }
    BusHandle musicBus() const { return BusHandle{2}; }
    BusHandle sfxBus() const { return BusHandle{3}; }
    BusHandle createBus(const char* name, BusHandle parent);
    BusHandle findBus(const char* name) const;
    void setBusVolume(BusHandle b, float volume, float fadeSeconds);
    float busVolume(BusHandle b) const;
    void setBusMuted(BusHandle b, bool muted);
    bool isBusMuted(BusHandle b) const;
    void setBusPaused(BusHandle b, bool paused);
    bool isBusPaused(BusHandle b) const;
    void stopBus(BusHandle b, float fadeOut);

    // Voices
    PlayingSoundHandle play(SoundHandle s, const PlayParams& p);
    void stop(PlayingSoundHandle h, float fadeOut);
    void pause(PlayingSoundHandle h, float fadeOut);
    void resume(PlayingSoundHandle h, float fadeIn);
    void setVolume(PlayingSoundHandle h, float volume, float fadeSeconds);
    void setPan(PlayingSoundHandle h, float pan);
    void setPitch(PlayingSoundHandle h, float pitch);
    void setLooping(PlayingSoundHandle h, bool loop);
    bool isPlaying(PlayingSoundHandle h) const;
    bool isPaused(PlayingSoundHandle h) const;
    int activeVoices() const;

    // Music
    bool playMusic(SoundHandle track, const MusicParams& p);
    bool playPlaylist(const std::vector<SoundHandle>& tracks, const PlaylistParams& p);
    void nextTrack();
    void stopMusic(float fadeOut);
    void pauseMusic(float fadeOut);
    void resumeMusic(float fadeIn);
    bool isMusicPlaying() const;
    SoundHandle currentMusic() const;

private:
    struct Sound;
    struct Bus;
    struct Voice;
    struct Music;

    SoundHandle store(Sound&& s);
    Sound* sound(SoundHandle s);
    const Sound* sound(SoundHandle s) const;
    Voice* voice(PlayingSoundHandle h);
    const Voice* voice(PlayingSoundHandle h) const;
    Bus* bus(BusHandle b);
    const Bus* bus(BusHandle b) const;
    bool busUnder(uint32_t index, uint32_t ancestor) const;

    PlayingSoundHandle startVoice(SoundHandle s, const PlayParams& p,
                                  SoundHandle queued, double loopStart);
    void beginStop(Voice& v, float fadeOut);
    void kill(Voice& v);
    bool openSource(Voice& v, uint32_t soundIndex);
    bool refill(Voice& v);
    bool fetch(Voice& v, float* frame);
    void renderVoice(Voice& v, float* out, int frames);
    void updateBuses(int frames);
    void updateMusic();
    double remainingFrames(const Voice& v) const;

    // Music helpers
    int nextPlaylistIndex();
    void startPlaylistTrack(int index, float fade);

    int sampleRate_;
    std::vector<Sound> sounds_;
    std::vector<Bus> buses_;
    std::vector<Voice> voices_;
    std::unique_ptr<Music> music_;
    uint64_t order_ = 0;
};

} // namespace drift::audio
