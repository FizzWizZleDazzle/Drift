#include "drift/audio.h"
#include "drift/drift.h"

#include <SDL3/SDL.h>

#include <string.h>
#include <stdlib.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr int   DRIFT_MAX_CHANNELS       = 32;
static constexpr int   DRIFT_MAX_SOUNDS         = 256;
static constexpr int   DRIFT_MIX_BUFFER_SAMPLES = 4096;
static constexpr int   DRIFT_AUDIO_FREQ         = 44100;
static constexpr int   DRIFT_AUDIO_CHANNELS     = 2; // stereo

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

struct SoundData {
    uint8_t *buffer;
    uint32_t length;      // in bytes
    bool     in_use;
};

struct PlayingChannel {
    uint32_t sound_id;     // 1-based index into s_audio.sounds
    uint32_t position;     // byte offset into sound buffer
    float    volume;
    float    pan;          // -1 = full left, 0 = centre, +1 = full right
    bool     active;
    uint16_t generation;   // for handle validation
};

struct MusicState {
    uint8_t *buffer;
    uint32_t length;       // in bytes
    uint32_t position;     // byte offset
    float    volume;
    bool     playing;
    bool     paused;
    bool     looping;
};

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

static struct {
    SDL_AudioDeviceID device_id;
    SDL_AudioStream  *stream;
    SDL_AudioSpec     spec;

    SoundData      sounds[DRIFT_MAX_SOUNDS];
    uint32_t       sound_count; // next available slot (never shrinks for stable ids)

    PlayingChannel channels[DRIFT_MAX_CHANNELS];
    uint16_t       channel_generation[DRIFT_MAX_CHANNELS];

    MusicState     music;

    // Temporary mix buffer (interleaved stereo float samples)
    float          mix_buf[DRIFT_MIX_BUFFER_SAMPLES * DRIFT_AUDIO_CHANNELS];

    bool           initialised;
} s_audio;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Pack channel index + generation into a single uint32_t handle.
// Bits 0..15  = channel index
// Bits 16..31 = generation
static inline uint32_t make_playing_handle(int channel, uint16_t gen)
{
    return (static_cast<uint32_t>(gen) << 16) | static_cast<uint32_t>(channel & 0xFFFF);
}

static inline int handle_channel(uint32_t handle)
{
    return static_cast<int>(handle & 0xFFFF);
}

static inline uint16_t handle_generation(uint32_t handle)
{
    return static_cast<uint16_t>(handle >> 16);
}

static bool validate_playing_handle(uint32_t handle)
{
    if (handle == 0) return false;
    int ch = handle_channel(handle);
    if (ch < 0 || ch >= DRIFT_MAX_CHANNELS) return false;
    if (!s_audio.channels[ch].active) return false;
    return handle_generation(handle) == s_audio.channels[ch].generation;
}

// Clamp a float to [lo, hi].
static inline float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ---------------------------------------------------------------------------
// Mixing
// ---------------------------------------------------------------------------

// Mix all active channels + music into s_audio.mix_buf and push to the stream.
// Called from drift_audio_update on the main thread.
static void mix_and_push(void)
{
    // How many samples (per channel) we generate each update.
    // Keep it small enough for low latency but large enough to avoid starvation.
    const int samples_to_mix = DRIFT_MIX_BUFFER_SAMPLES;
    const int frame_bytes    = static_cast<int>(sizeof(float)) * DRIFT_AUDIO_CHANNELS;

    // Clear mix buffer
    memset(s_audio.mix_buf, 0, sizeof(float) * samples_to_mix * DRIFT_AUDIO_CHANNELS);

    // ---- Mix sound channels -----------------------------------------------
    for (int ch = 0; ch < DRIFT_MAX_CHANNELS; ++ch) {
        PlayingChannel *pc = &s_audio.channels[ch];
        if (!pc->active) continue;

        uint32_t sid = pc->sound_id;
        if (sid == 0 || sid > DRIFT_MAX_SOUNDS) {
            pc->active = false;
            continue;
        }
        SoundData *sd = &s_audio.sounds[sid - 1];
        if (!sd->in_use || !sd->buffer) {
            pc->active = false;
            continue;
        }

        // We treat the stored audio as float stereo at DRIFT_AUDIO_FREQ.
        // (SDL_LoadWAV + SDL_ConvertAudioSamples ensure this at load time.)
        const float *src     = reinterpret_cast<const float *>(sd->buffer);
        const int    src_len = static_cast<int>(sd->length / sizeof(float) / DRIFT_AUDIO_CHANNELS);
        int          pos     = static_cast<int>(pc->position);

        float vol   = clampf(pc->volume, 0.0f, 2.0f);
        float pan   = clampf(pc->pan, -1.0f, 1.0f);
        float vol_l = vol * clampf(1.0f - pan, 0.0f, 1.0f);
        float vol_r = vol * clampf(1.0f + pan, 0.0f, 1.0f);

        int n = samples_to_mix;
        if (pos + n > src_len)
            n = src_len - pos;
        if (n <= 0) {
            pc->active = false;
            continue;
        }

        for (int i = 0; i < n; ++i) {
            int si = (pos + i) * DRIFT_AUDIO_CHANNELS;
            int di = i * DRIFT_AUDIO_CHANNELS;
            s_audio.mix_buf[di + 0] += src[si + 0] * vol_l;
            s_audio.mix_buf[di + 1] += src[si + 1] * vol_r;
        }

        pc->position = static_cast<uint32_t>(pos + n);
        if (pc->position >= static_cast<uint32_t>(src_len))
            pc->active = false;
    }

    // ---- Mix music --------------------------------------------------------
    MusicState *ms = &s_audio.music;
    if (ms->playing && !ms->paused && ms->buffer) {
        const float *src     = reinterpret_cast<const float *>(ms->buffer);
        const int    src_len = static_cast<int>(ms->length / sizeof(float) / DRIFT_AUDIO_CHANNELS);
        int          pos     = static_cast<int>(ms->position);

        float vol = clampf(ms->volume, 0.0f, 2.0f);

        int remaining = samples_to_mix;
        int out_offset = 0;

        while (remaining > 0 && ms->playing) {
            int avail = src_len - pos;
            if (avail <= 0) {
                if (ms->looping) {
                    pos = 0;
                    avail = src_len;
                } else {
                    ms->playing = false;
                    break;
                }
            }
            int n = remaining < avail ? remaining : avail;

            for (int i = 0; i < n; ++i) {
                int si = (pos + i) * DRIFT_AUDIO_CHANNELS;
                int di = (out_offset + i) * DRIFT_AUDIO_CHANNELS;
                s_audio.mix_buf[di + 0] += src[si + 0] * vol;
                s_audio.mix_buf[di + 1] += src[si + 1] * vol;
            }

            pos += n;
            out_offset += n;
            remaining -= n;

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
    int total_floats = samples_to_mix * DRIFT_AUDIO_CHANNELS;
    for (int i = 0; i < total_floats; ++i) {
        s_audio.mix_buf[i] = clampf(s_audio.mix_buf[i], -1.0f, 1.0f);
    }

    // ---- Push to SDL audio stream -----------------------------------------
    if (s_audio.stream) {
        SDL_PutAudioStreamData(s_audio.stream, s_audio.mix_buf,
                               samples_to_mix * frame_bytes);
    }
}

// ---------------------------------------------------------------------------
// Public API -- lifecycle
// ---------------------------------------------------------------------------

DriftResult drift_audio_init(void)
{
    memset(&s_audio, 0, sizeof(s_audio));

    // Desired output spec
    s_audio.spec.freq     = DRIFT_AUDIO_FREQ;
    s_audio.spec.format   = SDL_AUDIO_F32;
    s_audio.spec.channels = DRIFT_AUDIO_CHANNELS;

    // Open default output device
    s_audio.device_id = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &s_audio.spec);
    if (s_audio.device_id == 0) {
        DRIFT_LOG_ERROR("Failed to open audio device: %s", SDL_GetError());
        return DRIFT_ERROR_AUDIO;
    }

    // Create an audio stream bound to the device
    s_audio.stream = SDL_CreateAudioStream(&s_audio.spec, &s_audio.spec);
    if (!s_audio.stream) {
        DRIFT_LOG_ERROR("Failed to create audio stream: %s", SDL_GetError());
        SDL_CloseAudioDevice(s_audio.device_id);
        s_audio.device_id = 0;
        return DRIFT_ERROR_AUDIO;
    }

    if (!SDL_BindAudioStream(s_audio.device_id, s_audio.stream)) {
        DRIFT_LOG_ERROR("Failed to bind audio stream: %s", SDL_GetError());
        SDL_DestroyAudioStream(s_audio.stream);
        SDL_CloseAudioDevice(s_audio.device_id);
        s_audio.stream    = nullptr;
        s_audio.device_id = 0;
        return DRIFT_ERROR_AUDIO;
    }

    s_audio.music.volume = 1.0f;
    s_audio.initialised  = true;

    DRIFT_LOG_INFO("Audio system initialised (%d Hz, %d channels)",
                   s_audio.spec.freq, s_audio.spec.channels);
    return DRIFT_OK;
}

void drift_audio_shutdown(void)
{
    if (!s_audio.initialised) return;

    // Free all loaded sounds
    for (uint32_t i = 0; i < DRIFT_MAX_SOUNDS; ++i) {
        if (s_audio.sounds[i].in_use && s_audio.sounds[i].buffer) {
            SDL_free(s_audio.sounds[i].buffer);
            s_audio.sounds[i].buffer = nullptr;
            s_audio.sounds[i].in_use = false;
        }
    }

    // Free music buffer
    if (s_audio.music.buffer) {
        SDL_free(s_audio.music.buffer);
        s_audio.music.buffer = nullptr;
    }

    if (s_audio.stream) {
        SDL_DestroyAudioStream(s_audio.stream);
        s_audio.stream = nullptr;
    }

    if (s_audio.device_id) {
        SDL_CloseAudioDevice(s_audio.device_id);
        s_audio.device_id = 0;
    }

    s_audio.initialised = false;
    DRIFT_LOG_INFO("Audio system shut down");
}

void drift_audio_update(void)
{
    if (!s_audio.initialised) return;

    // Only push more data when the stream is running low to avoid unbounded
    // memory growth.  SDL_GetAudioStreamQueued returns bytes already queued.
    int queued = SDL_GetAudioStreamQueued(s_audio.stream);
    int threshold = DRIFT_MIX_BUFFER_SAMPLES * DRIFT_AUDIO_CHANNELS
                    * static_cast<int>(sizeof(float)) * 2;
    if (queued < threshold) {
        mix_and_push();
    }
}

// ---------------------------------------------------------------------------
// Public API -- sound effects
// ---------------------------------------------------------------------------

DriftSound drift_sound_load(const char *path)
{
    DriftSound result = {0};

    if (!s_audio.initialised) {
        DRIFT_LOG_ERROR("Audio not initialised, cannot load sound");
        return result;
    }

    // Find a free slot
    uint32_t slot = DRIFT_MAX_SOUNDS; // sentinel
    for (uint32_t i = 0; i < DRIFT_MAX_SOUNDS; ++i) {
        if (!s_audio.sounds[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot == DRIFT_MAX_SOUNDS) {
        DRIFT_LOG_ERROR("No free sound slots available");
        return result;
    }

    // Load the WAV file
    SDL_AudioSpec wav_spec;
    uint8_t *wav_buf  = nullptr;
    uint32_t wav_len  = 0;
    if (!SDL_LoadWAV(path, &wav_spec, &wav_buf, &wav_len)) {
        DRIFT_LOG_ERROR("Failed to load WAV '%s': %s", path, SDL_GetError());
        return result;
    }

    // Convert to our internal format (float32 stereo at DRIFT_AUDIO_FREQ)
    uint8_t *cvt_buf = nullptr;
    int      cvt_len = 0;

    if (wav_spec.freq     != s_audio.spec.freq ||
        wav_spec.format   != s_audio.spec.format ||
        wav_spec.channels != s_audio.spec.channels)
    {
        if (!SDL_ConvertAudioSamples(&wav_spec, wav_buf, static_cast<int>(wav_len),
                                     &s_audio.spec, &cvt_buf, &cvt_len))
        {
            DRIFT_LOG_ERROR("Failed to convert audio '%s': %s", path, SDL_GetError());
            SDL_free(wav_buf);
            return result;
        }
        SDL_free(wav_buf);
        s_audio.sounds[slot].buffer = cvt_buf;
        s_audio.sounds[slot].length = static_cast<uint32_t>(cvt_len);
    } else {
        s_audio.sounds[slot].buffer = wav_buf;
        s_audio.sounds[slot].length = wav_len;
    }

    s_audio.sounds[slot].in_use = true;

    // IDs are 1-based so that 0 is the invalid handle
    result.id = slot + 1;
    DRIFT_LOG_INFO("Loaded sound '%s' (id=%u, %u bytes)", path, result.id,
                   s_audio.sounds[slot].length);
    return result;
}

void drift_sound_destroy(DriftSound sound)
{
    if (sound.id == 0 || sound.id > DRIFT_MAX_SOUNDS) return;

    uint32_t idx = sound.id - 1;
    SoundData *sd = &s_audio.sounds[idx];
    if (!sd->in_use) return;

    // Deactivate any channels referencing this sound
    for (int ch = 0; ch < DRIFT_MAX_CHANNELS; ++ch) {
        if (s_audio.channels[ch].active && s_audio.channels[ch].sound_id == sound.id)
            s_audio.channels[ch].active = false;
    }

    if (sd->buffer) {
        SDL_free(sd->buffer);
        sd->buffer = nullptr;
    }
    sd->length = 0;
    sd->in_use = false;
}

DriftPlayingSound drift_sound_play(DriftSound sound, float volume, float pan)
{
    DriftPlayingSound result = {0};

    if (sound.id == 0 || sound.id > DRIFT_MAX_SOUNDS) return result;
    if (!s_audio.sounds[sound.id - 1].in_use) return result;

    // Find a free channel
    int free_ch = -1;
    for (int i = 0; i < DRIFT_MAX_CHANNELS; ++i) {
        if (!s_audio.channels[i].active) {
            free_ch = i;
            break;
        }
    }

    if (free_ch < 0) {
        // All channels busy -- steal the oldest (lowest generation)
        uint16_t min_gen = s_audio.channels[0].generation;
        free_ch = 0;
        for (int i = 1; i < DRIFT_MAX_CHANNELS; ++i) {
            if (s_audio.channels[i].generation < min_gen) {
                min_gen = s_audio.channels[i].generation;
                free_ch = i;
            }
        }
    }

    // Bump generation
    s_audio.channel_generation[free_ch]++;
    uint16_t gen = s_audio.channel_generation[free_ch];

    PlayingChannel *pc = &s_audio.channels[free_ch];
    pc->sound_id   = sound.id;
    pc->position   = 0;
    pc->volume     = volume;
    pc->pan        = pan;
    pc->active     = true;
    pc->generation = gen;

    result.id = make_playing_handle(free_ch, gen);
    return result;
}

void drift_playing_sound_stop(DriftPlayingSound playing)
{
    if (!validate_playing_handle(playing.id)) return;
    int ch = handle_channel(playing.id);
    s_audio.channels[ch].active = false;
}

void drift_playing_sound_set_volume(DriftPlayingSound playing, float volume)
{
    if (!validate_playing_handle(playing.id)) return;
    int ch = handle_channel(playing.id);
    s_audio.channels[ch].volume = volume;
}

void drift_playing_sound_set_pan(DriftPlayingSound playing, float pan)
{
    if (!validate_playing_handle(playing.id)) return;
    int ch = handle_channel(playing.id);
    s_audio.channels[ch].pan = pan;
}

bool drift_playing_sound_is_playing(DriftPlayingSound playing)
{
    if (!validate_playing_handle(playing.id)) return false;
    int ch = handle_channel(playing.id);
    return s_audio.channels[ch].active;
}

// ---------------------------------------------------------------------------
// Public API -- music
// ---------------------------------------------------------------------------

DriftResult drift_music_play(const char *path, bool loop)
{
    if (!s_audio.initialised) return DRIFT_ERROR_AUDIO;

    // Stop current music if any
    drift_music_stop();

    // Load the file via SDL_LoadWAV
    SDL_AudioSpec wav_spec;
    uint8_t *wav_buf  = nullptr;
    uint32_t wav_len  = 0;
    if (!SDL_LoadWAV(path, &wav_spec, &wav_buf, &wav_len)) {
        DRIFT_LOG_ERROR("Failed to load music '%s': %s", path, SDL_GetError());
        return DRIFT_ERROR_IO;
    }

    // Convert to internal format if needed
    if (wav_spec.freq     != s_audio.spec.freq ||
        wav_spec.format   != s_audio.spec.format ||
        wav_spec.channels != s_audio.spec.channels)
    {
        uint8_t *cvt_buf = nullptr;
        int      cvt_len = 0;
        if (!SDL_ConvertAudioSamples(&wav_spec, wav_buf, static_cast<int>(wav_len),
                                     &s_audio.spec, &cvt_buf, &cvt_len))
        {
            DRIFT_LOG_ERROR("Failed to convert music '%s': %s", path, SDL_GetError());
            SDL_free(wav_buf);
            return DRIFT_ERROR_FORMAT;
        }
        SDL_free(wav_buf);
        s_audio.music.buffer = cvt_buf;
        s_audio.music.length = static_cast<uint32_t>(cvt_len);
    } else {
        s_audio.music.buffer = wav_buf;
        s_audio.music.length = wav_len;
    }

    s_audio.music.position = 0;
    s_audio.music.playing  = true;
    s_audio.music.paused   = false;
    s_audio.music.looping  = loop;

    DRIFT_LOG_INFO("Playing music '%s' (loop=%s)", path, loop ? "true" : "false");
    return DRIFT_OK;
}

void drift_music_stop(void)
{
    s_audio.music.playing  = false;
    s_audio.music.paused   = false;
    s_audio.music.position = 0;

    if (s_audio.music.buffer) {
        SDL_free(s_audio.music.buffer);
        s_audio.music.buffer = nullptr;
    }
    s_audio.music.length = 0;
}

void drift_music_pause(void)
{
    if (s_audio.music.playing)
        s_audio.music.paused = true;
}

void drift_music_resume(void)
{
    if (s_audio.music.playing)
        s_audio.music.paused = false;
}

void drift_music_set_volume(float volume)
{
    s_audio.music.volume = clampf(volume, 0.0f, 2.0f);
}

bool drift_music_is_playing(void)
{
    return s_audio.music.playing && !s_audio.music.paused;
}
