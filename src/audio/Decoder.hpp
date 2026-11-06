#pragma once

// Audio decoders for WAV (dr_wav), OGG Vorbis (stb_vorbis) and MP3 (dr_mp3).
// Decoders read from an in-memory copy of the encoded file and produce
// interleaved float frames with one or two channels. Files with more than two
// channels keep their first two.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace drift::audio {

using Bytes = std::shared_ptr<const std::vector<uint8_t>>;

enum class Format { Unknown, Wav, Ogg, Mp3 };

// Detects the container from the first bytes of the file.
Format detectFormat(const uint8_t* data, size_t size);

class Decoder {
public:
    virtual ~Decoder() = default;

    int channels() const { return outChannels_; }
    int sampleRate() const { return sampleRate_; }
    // Total length in frames; 0 when unknown.
    uint64_t totalFrames() const { return totalFrames_; }

    // Reads up to `frames` interleaved frames into `out` (channels() floats
    // per frame). Returns the number of frames read; 0 at the end.
    uint64_t read(float* out, uint64_t frames);
    virtual bool seek(uint64_t frame) = 0;

protected:
    virtual uint64_t readNative(float* out, uint64_t frames) = 0;

    int srcChannels_ = 0;
    int outChannels_ = 0;
    int sampleRate_ = 0;
    uint64_t totalFrames_ = 0;

private:
    std::vector<float> scratch_;
};

// Opens a decoder over `bytes`; the decoder keeps the bytes alive.
// Returns null if the format is unknown or the data is invalid.
// totalFrames() is filled in only with `measureLength`, because measuring can
// mean scanning the whole file (MP3 without a length header).
std::unique_ptr<Decoder> openDecoder(Bytes bytes, bool measureLength = false);

} // namespace drift::audio
