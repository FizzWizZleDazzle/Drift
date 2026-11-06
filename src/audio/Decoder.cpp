#include "audio/Decoder.hpp"

#include <algorithm>
#include <cstring>

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include <dr_wav.h>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include <dr_mp3.h>

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include <stb_vorbis.c>

namespace drift::audio {

Format detectFormat(const uint8_t* d, size_t n) {
    if (n >= 4 && std::memcmp(d, "OggS", 4) == 0) return Format::Ogg;
    if (n >= 12 && (std::memcmp(d, "RIFF", 4) == 0 || std::memcmp(d, "RF64", 4) == 0)
        && std::memcmp(d + 8, "WAVE", 4) == 0) return Format::Wav;
    if (n >= 4 && std::memcmp(d, "riff", 4) == 0) return Format::Wav; // Wave64
    if (n >= 3 && std::memcmp(d, "ID3", 3) == 0) return Format::Mp3;
    if (n >= 2 && d[0] == 0xFF && (d[1] & 0xE0) == 0xE0) return Format::Mp3;
    return Format::Unknown;
}

uint64_t Decoder::read(float* out, uint64_t frames) {
    if (srcChannels_ <= 2) return readNative(out, frames);
    // Keep the first two channels of multichannel files.
    scratch_.resize(static_cast<size_t>(frames) * srcChannels_);
    uint64_t got = readNative(scratch_.data(), frames);
    for (uint64_t i = 0; i < got; ++i) {
        out[i * 2 + 0] = scratch_[i * srcChannels_ + 0];
        out[i * 2 + 1] = scratch_[i * srcChannels_ + 1];
    }
    return got;
}

namespace {

class WavDecoder final : public Decoder {
public:
    explicit WavDecoder(Bytes b) : bytes_(std::move(b)) {}
    ~WavDecoder() override { if (ok_) drwav_uninit(&wav_); }

    bool open(bool) {
        if (!drwav_init_memory(&wav_, bytes_->data(), bytes_->size(), nullptr)) return false;
        ok_ = true;
        srcChannels_ = static_cast<int>(wav_.channels);
        outChannels_ = std::min(srcChannels_, 2);
        sampleRate_ = static_cast<int>(wav_.sampleRate);
        totalFrames_ = wav_.totalPCMFrameCount;
        return srcChannels_ > 0 && sampleRate_ > 0;
    }
    bool seek(uint64_t frame) override { return drwav_seek_to_pcm_frame(&wav_, frame); }

protected:
    uint64_t readNative(float* out, uint64_t frames) override {
        return drwav_read_pcm_frames_f32(&wav_, frames, out);
    }

private:
    Bytes bytes_;
    drwav wav_{};
    bool ok_ = false;
};

class Mp3Decoder final : public Decoder {
public:
    explicit Mp3Decoder(Bytes b) : bytes_(std::move(b)) {}
    ~Mp3Decoder() override { if (ok_) drmp3_uninit(&mp3_); }

    bool open(bool measureLength) {
        if (!drmp3_init_memory(&mp3_, bytes_->data(), bytes_->size(), nullptr)) return false;
        ok_ = true;
        srcChannels_ = static_cast<int>(mp3_.channels);
        outChannels_ = std::min(srcChannels_, 2);
        sampleRate_ = static_cast<int>(mp3_.sampleRate);
        // Uses the Xing/LAME header when present (trimmed of encoder delay
        // and padding); otherwise scans the file once.
        if (measureLength) totalFrames_ = drmp3_get_pcm_frame_count(&mp3_);
        return srcChannels_ > 0 && sampleRate_ > 0;
    }
    bool seek(uint64_t frame) override { return drmp3_seek_to_pcm_frame(&mp3_, frame); }

protected:
    uint64_t readNative(float* out, uint64_t frames) override {
        return drmp3_read_pcm_frames_f32(&mp3_, frames, out);
    }

private:
    Bytes bytes_;
    drmp3 mp3_{};
    bool ok_ = false;
};

class OggDecoder final : public Decoder {
public:
    explicit OggDecoder(Bytes b) : bytes_(std::move(b)) {}
    ~OggDecoder() override { if (v_) stb_vorbis_close(v_); }

    bool open(bool measureLength) {
        int err = 0;
        v_ = stb_vorbis_open_memory(bytes_->data(), static_cast<int>(bytes_->size()), &err, nullptr);
        if (!v_) return false;
        stb_vorbis_info info = stb_vorbis_get_info(v_);
        srcChannels_ = info.channels;
        outChannels_ = std::min(srcChannels_, 2);
        sampleRate_ = static_cast<int>(info.sample_rate);
        if (measureLength) totalFrames_ = stb_vorbis_stream_length_in_samples(v_);
        return srcChannels_ > 0 && sampleRate_ > 0;
    }
    bool seek(uint64_t frame) override {
        return stb_vorbis_seek(v_, static_cast<unsigned int>(frame)) != 0;
    }

protected:
    uint64_t readNative(float* out, uint64_t frames) override {
        uint64_t total = 0;
        while (total < frames) {
            int want = static_cast<int>(std::min<uint64_t>(frames - total, 4096));
            int got = stb_vorbis_get_samples_float_interleaved(
                v_, srcChannels_, out + total * srcChannels_, want * srcChannels_);
            if (got <= 0) break;
            total += static_cast<uint64_t>(got);
        }
        return total;
    }

private:
    Bytes bytes_;
    stb_vorbis* v_ = nullptr;
};

template<typename D>
std::unique_ptr<Decoder> tryOpen(Bytes bytes, bool measureLength) {
    auto d = std::make_unique<D>(std::move(bytes));
    if (!d->open(measureLength)) return nullptr;
    return d;
}

} // namespace

std::unique_ptr<Decoder> openDecoder(Bytes bytes, bool measureLength) {
    if (!bytes || bytes->empty()) return nullptr;
    switch (detectFormat(bytes->data(), bytes->size())) {
        case Format::Wav: return tryOpen<WavDecoder>(std::move(bytes), measureLength);
        case Format::Ogg: return tryOpen<OggDecoder>(std::move(bytes), measureLength);
        case Format::Mp3: return tryOpen<Mp3Decoder>(std::move(bytes), measureLength);
        case Format::Unknown: break;
    }
    return nullptr;
}

} // namespace drift::audio
