#include <drift/resources/AudioResource.h>
#include <drift/Log.h>

#include <SDL3/SDL.h>

#include <cstring>
#include <cstdlib>

namespace drift {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr int MAX_CHANNELS       = 32;
static constexpr int MAX_SOUNDS         = 256;
static constexpr int MIX_BUFFER_SAMPLES = 4096;
static constexpr int AUDIO_FREQ         = 44100;
static constexpr int AUDIO_CHANNELS     = 2; // stereo

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

struct SoundData {
    uint8_t* buffer  = nullptr;
    uint32_t length  = 0; // in bytes
    bool     in_use  = false;
};

struct PlayingChannel {
    uint32_t sound_id   = 0; // 1-based index into sounds[]
    uint32_t position   = 0; // byte offset into sound buffer
    float    volume     = 0.f;
    float    pan        = 0.f; // -1 = full left, 0 = centre, +1 = full right
    bool     active     = false;
    uint32_t generation = 0; // for handle validation (12-bit, wraps via mask)
};

struct MusicState {
    uint8_t* buffer   = nullptr;
    uint32_t length   = 0; // in bytes
    uint32_t position = 0; // byte offset
    float    volume   = 1.f;
    bool     playing  = false;
    bool     paused   = false;
    bool     looping  = false;
};

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct AudioResource::Impl {
    SDL_AudioDeviceID device_id = 0;
    SDL_AudioStream*  stream    = nullptr;
    SDL_AudioSpec     spec{};

    SoundData sounds[MAX_SOUNDS]{};

    PlayingChannel channels[MAX_CHANNELS]{};
    uint32_t       channel_generation[MAX_CHANNELS]{};

    MusicState music{};

    // Temporary mix buffer (interleaved stereo float samples)
    float mix_buf[MIX_BUFFER_SAMPLES * AUDIO_CHANNELS]{};

    bool initialised = false;

    // -- helpers --

    static float clampf(float v, float lo, float hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    bool validatePlayingHandle(PlayingSoundHandle h) const {
        if (!h.valid()) return false;
        uint32_t ch  = h.index();
        uint32_t gen = h.generation();
        if (ch >= static_cast<uint32_t>(MAX_CHANNELS)) return false;
        if (!channels[ch].active) return false;
        return gen == channels[ch].generation;
    }

    void mixAndPush() {
        const int samples_to_mix = MIX_BUFFER_SAMPLES;
        const int frame_bytes    = static_cast<int>(sizeof(float)) * AUDIO_CHANNELS;

        // Clear mix buffer
        std::memset(mix_buf, 0, sizeof(float) * samples_to_mix * AUDIO_CHANNELS);

        // ---- Mix sound channels -----------------------------------------------
        for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
            PlayingChannel* pc = &channels[ch];
            if (!pc->active) continue;

            uint32_t sid = pc->sound_id;
            if (sid == 0 || sid > MAX_SOUNDS) {
                pc->active = false;
                continue;
            }
            SoundData* sd = &sounds[sid - 1];
            if (!sd->in_use || !sd->buffer) {
                pc->active = false;
                continue;
            }

            // Audio is stored as float stereo at AUDIO_FREQ
            // (SDL_LoadWAV + SDL_ConvertAudioSamples ensure this at load time.)
            const float* src     = reinterpret_cast<const float*>(sd->buffer);
            const int    src_len = static_cast<int>(sd->length / sizeof(float) / AUDIO_CHANNELS);
            int          pos     = static_cast<int>(pc->position);

            float vol   = clampf(pc->volume, 0.f, 2.f);
            float pan   = clampf(pc->pan, -1.f, 1.f);
            float vol_l = vol * clampf(1.f - pan, 0.f, 1.f);
            float vol_r = vol * clampf(1.f + pan, 0.f, 1.f);

            int n = samples_to_mix;
            if (pos + n > src_len)
                n = src_len - pos;
            if (n <= 0) {
                pc->active = false;
                continue;
            }

            for (int i = 0; i < n; ++i) {
                int si = (pos + i) * AUDIO_CHANNELS;
                int di = i * AUDIO_CHANNELS;
                mix_buf[di + 0] += src[si + 0] * vol_l;
                mix_buf[di + 1] += src[si + 1] * vol_r;
            }

            pc->position = static_cast<uint32_t>(pos + n);
            if (pc->position >= static_cast<uint32_t>(src_len))
                pc->active = false;
        }

        // ---- Mix music --------------------------------------------------------
        MusicState* ms = &music;
        if (ms->playing && !ms->paused && ms->buffer) {
            const float* src     = reinterpret_cast<const float*>(ms->buffer);
            const int    src_len = static_cast<int>(ms->length / sizeof(float) / AUDIO_CHANNELS);
            int          pos     = static_cast<int>(ms->position);

            float vol = clampf(ms->volume, 0.f, 2.f);

            int remaining  = samples_to_mix;
            int out_offset = 0;

            while (remaining > 0 && ms->playing) {
                int avail = src_len - pos;
                if (avail <= 0) {
                    if (ms->looping) {
                        pos   = 0;
                        avail = src_len;
                    } else {
                        ms->playing = false;
                        break;
                    }
                }
                int n = remaining < avail ? remaining : avail;

                for (int i = 0; i < n; ++i) {
                    int si = (pos + i) * AUDIO_CHANNELS;
                    int di = (out_offset + i) * AUDIO_CHANNELS;
                    mix_buf[di + 0] += src[si + 0] * vol;
                    mix_buf[di + 1] += src[si + 1] * vol;
                }

                pos        += n;
                out_offset += n;
                remaining  -= n;

                if (pos >= src_len) {
                    if (ms->looping) {
                        pos = 0;
                    } else {
                        ms->playing = false;
                    }
                }
            }

            ms->position = static_cast<uint32_t>(pos);
        }

        // ---- Clamp output -----------------------------------------------------
        int total_floats = samples_to_mix * AUDIO_CHANNELS;
        for (int i = 0; i < total_floats; ++i) {
            mix_buf[i] = clampf(mix_buf[i], -1.f, 1.f);
        }

        // ---- Push to SDL audio stream -----------------------------------------
        if (stream) {
            SDL_PutAudioStreamData(stream, mix_buf, samples_to_mix * frame_bytes);
        }
    }
};

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

AudioResource::AudioResource()
    : impl_(new Impl)
{
    impl_->spec.freq     = AUDIO_FREQ;
    impl_->spec.format   = SDL_AUDIO_F32;
    impl_->spec.channels = AUDIO_CHANNELS;

    impl_->device_id = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                           &impl_->spec);
    if (impl_->device_id == 0) {
        DRIFT_LOG_ERROR("Failed to open audio device: %s", SDL_GetError());
        return;
    }

    impl_->stream = SDL_CreateAudioStream(&impl_->spec, &impl_->spec);
    if (!impl_->stream) {
        DRIFT_LOG_ERROR("Failed to create audio stream: %s", SDL_GetError());
        SDL_CloseAudioDevice(impl_->device_id);
        impl_->device_id = 0;
        return;
    }

    if (!SDL_BindAudioStream(impl_->device_id, impl_->stream)) {
        DRIFT_LOG_ERROR("Failed to bind audio stream: %s", SDL_GetError());
        SDL_DestroyAudioStream(impl_->stream);
        SDL_CloseAudioDevice(impl_->device_id);
        impl_->stream    = nullptr;
        impl_->device_id = 0;
        return;
    }

    impl_->music.volume = 1.f;
    impl_->initialised  = true;

    DRIFT_LOG_INFO("Audio system initialised (%d Hz, %d channels)",
                   impl_->spec.freq, impl_->spec.channels);
}

AudioResource::~AudioResource()
{
    if (impl_) {
        if (impl_->initialised) {
            // Free all loaded sounds
            for (int i = 0; i < MAX_SOUNDS; ++i) {
                if (impl_->sounds[i].in_use && impl_->sounds[i].buffer) {
                    SDL_free(impl_->sounds[i].buffer);
                    impl_->sounds[i].buffer = nullptr;
                    impl_->sounds[i].in_use = false;
                }
            }

            // Free music buffer
            if (impl_->music.buffer) {
                SDL_free(impl_->music.buffer);
                impl_->music.buffer = nullptr;
            }

            if (impl_->stream) {
                SDL_DestroyAudioStream(impl_->stream);
                impl_->stream = nullptr;
            }

            if (impl_->device_id) {
                SDL_CloseAudioDevice(impl_->device_id);
                impl_->device_id = 0;
            }

            impl_->initialised = false;
            DRIFT_LOG_INFO("Audio system shut down");
        }

        delete impl_;
        impl_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

void AudioResource::update()
{
    if (!impl_->initialised) return;

    // Only push more data when the stream is running low to avoid unbounded
    // memory growth.  SDL_GetAudioStreamQueued returns bytes already queued.
    int queued    = SDL_GetAudioStreamQueued(impl_->stream);
    int threshold = MIX_BUFFER_SAMPLES * AUDIO_CHANNELS
                    * static_cast<int>(sizeof(float)) * 2;
    if (queued < threshold) {
        impl_->mixAndPush();
    }
}

// ---------------------------------------------------------------------------
// Sound effects
// ---------------------------------------------------------------------------

SoundHandle AudioResource::loadSound(const char* path)
{
    if (!impl_->initialised) {
        DRIFT_LOG_ERROR("Audio not initialised, cannot load sound");
        return SoundHandle{};
    }

    // Find a free slot
    uint32_t slot = MAX_SOUNDS; // sentinel
    for (uint32_t i = 0; i < MAX_SOUNDS; ++i) {
        if (!impl_->sounds[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot == static_cast<uint32_t>(MAX_SOUNDS)) {
        DRIFT_LOG_ERROR("No free sound slots available");
        return SoundHandle{};
    }

    // Load the WAV file
    SDL_AudioSpec wav_spec;
    uint8_t* wav_buf = nullptr;
    uint32_t wav_len = 0;
    if (!SDL_LoadWAV(path, &wav_spec, &wav_buf, &wav_len)) {
        DRIFT_LOG_ERROR("Failed to load WAV '%s': %s", path, SDL_GetError());
        return SoundHandle{};
    }

    // Convert to our internal format (float32 stereo at AUDIO_FREQ)
    if (wav_spec.freq     != impl_->spec.freq ||
        wav_spec.format   != impl_->spec.format ||
        wav_spec.channels != impl_->spec.channels)
    {
        uint8_t* cvt_buf = nullptr;
        int      cvt_len = 0;

        if (!SDL_ConvertAudioSamples(&wav_spec, wav_buf, static_cast<int>(wav_len),
                                     &impl_->spec, &cvt_buf, &cvt_len))
        {
            DRIFT_LOG_ERROR("Failed to convert audio '%s': %s", path, SDL_GetError());
            SDL_free(wav_buf);
            return SoundHandle{};
        }
        SDL_free(wav_buf);
        impl_->sounds[slot].buffer = cvt_buf;
        impl_->sounds[slot].length = static_cast<uint32_t>(cvt_len);
    } else {
        impl_->sounds[slot].buffer = wav_buf;
        impl_->sounds[slot].length = wav_len;
    }

    impl_->sounds[slot].in_use = true;

    // IDs are 1-based so that 0 is the invalid handle
    SoundHandle result{slot + 1};
    DRIFT_LOG_INFO("Loaded sound '%s' (id=%u, %u bytes)", path, result.id,
                   impl_->sounds[slot].length);
    return result;
}

void AudioResource::destroySound(SoundHandle sound)
{
    if (!sound.valid() || sound.id > MAX_SOUNDS) return;

    uint32_t idx = sound.id - 1;
    SoundData* sd = &impl_->sounds[idx];
    if (!sd->in_use) return;

    // Deactivate any channels referencing this sound
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        if (impl_->channels[ch].active && impl_->channels[ch].sound_id == sound.id)
            impl_->channels[ch].active = false;
    }

    if (sd->buffer) {
        SDL_free(sd->buffer);
        sd->buffer = nullptr;
    }
    sd->length = 0;
    sd->in_use = false;
}

PlayingSoundHandle AudioResource::playSound(SoundHandle sound, float volume, float pan)
{
    if (!sound.valid() || sound.id > MAX_SOUNDS) return PlayingSoundHandle{};
    if (!impl_->sounds[sound.id - 1].in_use) return PlayingSoundHandle{};

    // Find a free channel
    int free_ch = -1;
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        if (!impl_->channels[i].active) {
            free_ch = i;
            break;
        }
    }

    if (free_ch < 0) {
        // All channels busy -- steal the oldest (lowest generation)
        uint32_t min_gen = impl_->channels[0].generation;
        free_ch = 0;
        for (int i = 1; i < MAX_CHANNELS; ++i) {
            if (impl_->channels[i].generation < min_gen) {
                min_gen = impl_->channels[i].generation;
                free_ch = i;
            }
        }
    }

    // Bump generation (mask to 12 bits to match Handle layout)
    impl_->channel_generation[free_ch] =
        (impl_->channel_generation[free_ch] + 1) & 0xFFF;
    uint32_t gen = impl_->channel_generation[free_ch];
    // Avoid generation 0 so that the packed handle is never 0
    if (gen == 0) {
        impl_->channel_generation[free_ch] = 1;
        gen = 1;
    }

    PlayingChannel* pc = &impl_->channels[free_ch];
    pc->sound_id   = sound.id;
    pc->position   = 0;
    pc->volume     = volume;
    pc->pan        = pan;
    pc->active     = true;
    pc->generation = gen;

    return PlayingSoundHandle::make(static_cast<uint32_t>(free_ch), gen);
}

void AudioResource::stopSound(PlayingSoundHandle playing)
{
    if (!impl_->validatePlayingHandle(playing)) return;
    uint32_t ch = playing.index();
    impl_->channels[ch].active = false;
}

void AudioResource::setSoundVolume(PlayingSoundHandle playing, float volume)
{
    if (!impl_->validatePlayingHandle(playing)) return;
    uint32_t ch = playing.index();
    impl_->channels[ch].volume = volume;
}

void AudioResource::setSoundPan(PlayingSoundHandle playing, float pan)
{
    if (!impl_->validatePlayingHandle(playing)) return;
    uint32_t ch = playing.index();
    impl_->channels[ch].pan = pan;
}

bool AudioResource::isSoundPlaying(PlayingSoundHandle playing) const
{
    if (!impl_->validatePlayingHandle(playing)) return false;
    uint32_t ch = playing.index();
    return impl_->channels[ch].active;
}

// ---------------------------------------------------------------------------
// Music
// ---------------------------------------------------------------------------

bool AudioResource::playMusic(const char* path, bool loop)
{
    if (!impl_->initialised) return false;

    // Stop current music if any
    stopMusic();

    // Load the file via SDL_LoadWAV
    SDL_AudioSpec wav_spec;
    uint8_t* wav_buf = nullptr;
    uint32_t wav_len = 0;
    if (!SDL_LoadWAV(path, &wav_spec, &wav_buf, &wav_len)) {
        DRIFT_LOG_ERROR("Failed to load music '%s': %s", path, SDL_GetError());
        return false;
    }

    // Convert to internal format if needed
    if (wav_spec.freq     != impl_->spec.freq ||
        wav_spec.format   != impl_->spec.format ||
        wav_spec.channels != impl_->spec.channels)
    {
        uint8_t* cvt_buf = nullptr;
        int      cvt_len = 0;
        if (!SDL_ConvertAudioSamples(&wav_spec, wav_buf, static_cast<int>(wav_len),
                                     &impl_->spec, &cvt_buf, &cvt_len))
        {
            DRIFT_LOG_ERROR("Failed to convert music '%s': %s", path, SDL_GetError());
            SDL_free(wav_buf);
            return false;
        }
        SDL_free(wav_buf);
        impl_->music.buffer = cvt_buf;
        impl_->music.length = static_cast<uint32_t>(cvt_len);
    } else {
        impl_->music.buffer = wav_buf;
        impl_->music.length = wav_len;
    }

    impl_->music.position = 0;
    impl_->music.playing  = true;
    impl_->music.paused   = false;
    impl_->music.looping  = loop;

    DRIFT_LOG_INFO("Playing music '%s' (loop=%s)", path, loop ? "true" : "false");
    return true;
}

void AudioResource::stopMusic()
{
    impl_->music.playing  = false;
    impl_->music.paused   = false;
    impl_->music.position = 0;

    if (impl_->music.buffer) {
        SDL_free(impl_->music.buffer);
        impl_->music.buffer = nullptr;
    }
    impl_->music.length = 0;
}

void AudioResource::pauseMusic()
{
    if (impl_->music.playing)
        impl_->music.paused = true;
}

void AudioResource::resumeMusic()
{
    if (impl_->music.playing)
        impl_->music.paused = false;
}

void AudioResource::setMusicVolume(float volume)
{
    impl_->music.volume = Impl::clampf(volume, 0.f, 2.f);
}

bool AudioResource::isMusicPlaying() const
{
    return impl_->music.playing && !impl_->music.paused;
}

} // namespace drift
