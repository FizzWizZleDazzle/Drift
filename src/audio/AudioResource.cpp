#include <drift/resources/AudioResource.hpp>
#include <drift/Log.hpp>

#include "audio/Mixer.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <unordered_map>

namespace drift {

// The mixer runs inside SDL's audio stream callback. On desktop SDL calls it
// from its audio thread; in the browser it is driven by the Web Audio
// callback on the main thread, so the engine needs no thread of its own.
// Every public call takes the stream lock, which is the same lock SDL holds
// while the callback runs.

namespace {
constexpr int kSampleRate = 48000;
}

struct AudioResource::Impl {
    audio::Mixer mixer{kSampleRate};
    SDL_AudioStream* stream = nullptr;
    bool subsystem = false;
    std::vector<float> buffer;
    std::unordered_map<std::string, SoundHandle> musicByPath;

    static void SDLCALL callback(void* user, SDL_AudioStream* stream,
                                 int additional, int /*total*/) {
        auto* self = static_cast<Impl*>(user);
        int frames = additional / static_cast<int>(sizeof(float) * 2);
        if (frames <= 0) return;
        self->buffer.resize(static_cast<size_t>(frames) * 2);
        self->mixer.mix(self->buffer.data(), frames);
        SDL_PutAudioStreamData(stream, self->buffer.data(),
                               frames * static_cast<int>(sizeof(float) * 2));
    }
};

namespace {
// Holds the stream lock for the lifetime of the guard; a no-op without a device.
struct Lock {
    SDL_AudioStream* s;
    explicit Lock(SDL_AudioStream* s) : s(s) { if (s) SDL_LockAudioStream(s); }
    ~Lock() { if (s) SDL_UnlockAudioStream(s); }
};
} // namespace

#define DRIFT_AUDIO_LOCK Lock lock_(impl_->stream)

AudioResource::AudioResource() : impl_(new Impl) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        DRIFT_LOG_ERROR("Audio unavailable: %s", SDL_GetError());
        return;
    }
    impl_->subsystem = true;

    SDL_AudioSpec spec{};
    spec.freq = kSampleRate;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    impl_->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                              &Impl::callback, impl_);
    if (!impl_->stream) {
        DRIFT_LOG_ERROR("Failed to open audio device: %s", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(impl_->stream);
    DRIFT_LOG_INFO("Audio initialised (%d Hz stereo, driver %s)", kSampleRate,
                   SDL_GetCurrentAudioDriver());
}

AudioResource::~AudioResource() {
    if (impl_->stream) SDL_DestroyAudioStream(impl_->stream);
    if (impl_->subsystem) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    delete impl_;
}

int AudioResource::sampleRate() const { return impl_->mixer.sampleRate(); }
bool AudioResource::hasDevice() const { return impl_->stream != nullptr; }

void AudioResource::update(float dt) {
    if (impl_->stream || dt <= 0.f) return;
    int frames = static_cast<int>(dt * kSampleRate);
    impl_->buffer.resize(static_cast<size_t>(frames) * 2);
    if (frames > 0) impl_->mixer.mix(impl_->buffer.data(), frames);
}

// ---- Sounds ---------------------------------------------------------------

SoundHandle AudioResource::loadSound(const char* path, LoadMode mode) {
    size_t size = 0;
    void* data = SDL_LoadFile(path, &size);
    if (!data) {
        DRIFT_LOG_ERROR("Failed to read sound '%s': %s", path, SDL_GetError());
        return {};
    }
    SoundHandle h = loadSoundFromMemory(data, size, mode);
    SDL_free(data);
    if (!h.valid())
        DRIFT_LOG_ERROR("Unsupported or invalid audio file '%s'", path);
    else
        DRIFT_LOG_INFO("Loaded sound '%s' (%.2f s%s)", path, soundDuration(h),
                       mode == LoadMode::Stream ? ", streamed" : "");
    return h;
}

SoundHandle AudioResource::loadSoundFromMemory(const void* data, size_t size, LoadMode mode) {
    if (!data || size == 0) return {};
    auto* p = static_cast<const uint8_t*>(data);
    auto bytes = std::make_shared<const std::vector<uint8_t>>(p, p + size);
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.addEncoded(std::move(bytes), mode);
}

SoundHandle AudioResource::createSound(const float* samples, size_t frames, int channels,
                                       int sampleRate) {
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.addPcm(samples, frames, channels, sampleRate);
}

void AudioResource::destroySound(SoundHandle sound) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.destroySound(sound);
}

double AudioResource::soundDuration(SoundHandle sound) const {
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.soundDuration(sound);
}

void AudioResource::setSoundMaxInstances(SoundHandle sound, int maxInstances) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.setSoundMaxInstances(sound, maxInstances);
}

// ---- Buses ----------------------------------------------------------------

BusHandle AudioResource::masterBus() const { return impl_->mixer.masterBus(); }
BusHandle AudioResource::musicBus() const { return impl_->mixer.musicBus(); }
BusHandle AudioResource::sfxBus() const { return impl_->mixer.sfxBus(); }

BusHandle AudioResource::createBus(const char* name, BusHandle parent) {
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.createBus(name, parent);
}
BusHandle AudioResource::findBus(const char* name) const {
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.findBus(name);
}
void AudioResource::setBusVolume(BusHandle bus, float volume, float fadeSeconds) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.setBusVolume(bus, volume, fadeSeconds);
}
float AudioResource::busVolume(BusHandle bus) const {
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.busVolume(bus);
}
void AudioResource::setBusMuted(BusHandle bus, bool muted) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.setBusMuted(bus, muted);
}
bool AudioResource::isBusMuted(BusHandle bus) const {
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.isBusMuted(bus);
}
void AudioResource::setBusPaused(BusHandle bus, bool paused) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.setBusPaused(bus, paused);
}
bool AudioResource::isBusPaused(BusHandle bus) const {
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.isBusPaused(bus);
}
void AudioResource::stopBus(BusHandle bus, float fadeOut) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.stopBus(bus, fadeOut);
}

// ---- Playing sounds -------------------------------------------------------

PlayingSoundHandle AudioResource::playSound(SoundHandle sound, const PlayParams& params) {
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.play(sound, params);
}
void AudioResource::stopSound(PlayingSoundHandle playing, float fadeOut) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.stop(playing, fadeOut);
}
void AudioResource::pauseSound(PlayingSoundHandle playing, float fadeOut) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.pause(playing, fadeOut);
}
void AudioResource::resumeSound(PlayingSoundHandle playing, float fadeIn) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.resume(playing, fadeIn);
}
void AudioResource::setSoundVolume(PlayingSoundHandle playing, float volume, float fadeSeconds) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.setVolume(playing, volume, fadeSeconds);
}
void AudioResource::setSoundPan(PlayingSoundHandle playing, float pan) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.setPan(playing, pan);
}
void AudioResource::setSoundPitch(PlayingSoundHandle playing, float pitch) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.setPitch(playing, pitch);
}
void AudioResource::setSoundLooping(PlayingSoundHandle playing, bool loop) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.setLooping(playing, loop);
}
bool AudioResource::isSoundPlaying(PlayingSoundHandle playing) const {
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.isPlaying(playing);
}
bool AudioResource::isSoundPaused(PlayingSoundHandle playing) const {
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.isPaused(playing);
}

// ---- Music ----------------------------------------------------------------

bool AudioResource::playMusic(SoundHandle track, const MusicParams& params) {
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.playMusic(track, params);
}

bool AudioResource::playMusic(const char* path, const MusicParams& params) {
    if (!path) return false;
    SoundHandle& cached = impl_->musicByPath[path];
    if (!cached.valid() || soundDuration(cached) <= 0.0)
        cached = loadSound(path, LoadMode::Stream);
    if (!cached.valid()) return false;
    return playMusic(cached, params);
}

bool AudioResource::playPlaylist(const std::vector<SoundHandle>& tracks,
                                 const PlaylistParams& params) {
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.playPlaylist(tracks, params);
}
void AudioResource::nextTrack() {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.nextTrack();
}
void AudioResource::stopMusic(float fadeOut) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.stopMusic(fadeOut);
}
void AudioResource::pauseMusic(float fadeOut) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.pauseMusic(fadeOut);
}
void AudioResource::resumeMusic(float fadeIn) {
    DRIFT_AUDIO_LOCK;
    impl_->mixer.resumeMusic(fadeIn);
}
bool AudioResource::isMusicPlaying() const {
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.isMusicPlaying();
}
SoundHandle AudioResource::currentMusic() const {
    DRIFT_AUDIO_LOCK;
    return impl_->mixer.currentMusic();
}

} // namespace drift
