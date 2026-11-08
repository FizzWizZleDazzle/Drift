// Offline checks for the audio decoders and mixer. Nothing here needs an
// audio device: the mixer renders into plain buffers that the tests inspect.
//
// tests/data holds two quarter-second 440 Hz mono tones at 44.1 kHz, made with
//   ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=0.25"
//          -ac 1 -c:a libvorbis -q:a 0 tone440.ogg
//   ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=0.25"
//          -ac 1 -c:a libmp3lame -b:a 32k tone440.mp3
// WAV data is generated in memory by the tests.

#include "audio/Decoder.hpp"
#include "audio/Mixer.hpp"

#include <drift/World.hpp>
#include <drift/components/AudioSource.hpp>
#include <drift/plugins/AudioPlugin.hpp>
#include <drift/resources/AudioResource.hpp>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace drift;
using drift::audio::Mixer;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                    \
    do {                                                                         \
        ++g_checks;                                                              \
        double va_ = (a), vb_ = (b);                                             \
        if (std::fabs(va_ - vb_) > (eps)) {                                      \
            ++g_failures;                                                        \
            std::printf("  FAIL %s:%d: %s = %g, expected %g (+-%g)\n", __FILE__, \
                        __LINE__, #a, va_, vb_, (double)(eps));                  \
        }                                                                        \
    } while (0)

namespace {

constexpr int kRate = 48000;
constexpr double kPi = 3.14159265358979323846;

audio::Bytes readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    auto v = std::make_shared<std::vector<uint8_t>>(std::istreambuf_iterator<char>(f),
                                                    std::istreambuf_iterator<char>());
    return v;
}

void put16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
void put32(std::vector<uint8_t>& b, uint32_t v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }

// A RIFF/WAVE file with 16-bit PCM or 32-bit float samples.
audio::Bytes makeWav(const std::vector<float>& samples, int channels, int rate, bool asFloat) {
    std::vector<uint8_t> b;
    uint16_t bits = asFloat ? 32 : 16;
    uint32_t dataBytes = static_cast<uint32_t>(samples.size() * bits / 8);
    b.insert(b.end(), {'R', 'I', 'F', 'F'});
    put32(b, 36 + dataBytes);
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(b, 16);
    put16(b, asFloat ? 3 : 1);
    put16(b, static_cast<uint16_t>(channels));
    put32(b, static_cast<uint32_t>(rate));
    put32(b, static_cast<uint32_t>(rate * channels * bits / 8));
    put16(b, static_cast<uint16_t>(channels * bits / 8));
    put16(b, bits);
    b.insert(b.end(), {'d', 'a', 't', 'a'});
    put32(b, dataBytes);
    for (float s : samples) {
        if (asFloat) {
            uint32_t u;
            std::memcpy(&u, &s, 4);
            put32(b, u);
        } else {
            put16(b, static_cast<uint16_t>(static_cast<int16_t>(std::lround(s * 32767.f))));
        }
    }
    return std::make_shared<std::vector<uint8_t>>(std::move(b));
}

std::vector<float> sine(int frames, int rate, double hz, float amp) {
    std::vector<float> v(frames);
    for (int i = 0; i < frames; ++i) v[i] = amp * static_cast<float>(std::sin(2 * kPi * hz * i / rate));
    return v;
}

std::vector<float> render(Mixer& m, int frames) {
    std::vector<float> out(static_cast<size_t>(frames) * 2);
    m.mix(out.data(), frames);
    return out;
}

int zeroCrossings(const std::vector<float>& mono) {
    int n = 0;
    for (size_t i = 1; i < mono.size(); ++i)
        if ((mono[i - 1] < 0.f) != (mono[i] < 0.f)) ++n;
    return n;
}

std::vector<float> decodeAll(audio::Decoder& d) {
    std::vector<float> all;
    float buf[1024 * 2];
    for (;;) {
        uint64_t got = d.read(buf, 1024);
        if (!got) break;
        all.insert(all.end(), buf, buf + got * d.channels());
    }
    return all;
}

// Index of the last non-silent frame + 1 in the left channel.
int audibleLength(const std::vector<float>& out) {
    int last = 0;
    for (size_t i = 0; i < out.size() / 2; ++i)
        if (std::fabs(out[i * 2]) > 1e-6f) last = static_cast<int>(i) + 1;
    return last;
}

SoundHandle constSound(Mixer& m, float value, int frames, int rate = kRate) {
    std::vector<float> v(frames, value);
    return m.addPcm(v.data(), v.size(), 1, rate);
}

// ---------------------------------------------------------------------------

void testWavDecode() {
    std::printf("wav decode\n");
    auto tone = sine(1000, 22050, 440, 0.5f);
    auto dec = audio::openDecoder(makeWav(tone, 1, 22050, false));
    CHECK(dec != nullptr);
    if (!dec) return;
    CHECK(dec->channels() == 1);
    CHECK(dec->sampleRate() == 22050);
    CHECK(dec->totalFrames() == 1000);
    auto pcm = decodeAll(*dec);
    CHECK(pcm.size() == 1000);
    float maxErr = 0.f;
    for (size_t i = 0; i < pcm.size() && i < tone.size(); ++i)
        maxErr = std::max(maxErr, std::fabs(pcm[i] - tone[i]));
    CHECK(maxErr < 1.f / 16384.f);

    // Stereo float WAV
    std::vector<float> st(500 * 2);
    for (int i = 0; i < 500; ++i) { st[i * 2] = 0.25f; st[i * 2 + 1] = -0.75f; }
    Mixer m(kRate);
    SoundHandle s = m.addEncoded(makeWav(st, 2, 44100, true), LoadMode::Decode);
    CHECK(s.valid());
    CHECK(m.soundFrames(s) == 500);
    CHECK(m.soundChannels(s) == 2);
    CHECK(m.soundRate(s) == 44100);
    CHECK_NEAR(m.soundDuration(s), 500.0 / 44100.0, 1e-9);
}

void testOggDecode() {
    std::printf("ogg decode\n");
    auto bytes = readFile(DRIFT_TEST_DATA_DIR "/tone440.ogg");
    CHECK(audio::detectFormat(bytes->data(), bytes->size()) == audio::Format::Ogg);
    auto dec = audio::openDecoder(bytes, true);
    CHECK(dec != nullptr);
    if (!dec) return;
    CHECK(dec->channels() == 1);
    CHECK(dec->sampleRate() == 44100);
    CHECK(dec->totalFrames() == 11025);
    auto pcm = decodeAll(*dec);
    CHECK(pcm.size() == 11025);
    // 440 Hz for 0.25 s crosses zero about 220 times.
    CHECK_NEAR(zeroCrossings(pcm), 220, 4);
    // Seeking back to the start reproduces the first samples.
    CHECK(dec->seek(0));
    float first[16];
    CHECK(dec->read(first, 16) == 16);
    CHECK_NEAR(first[10], pcm[10], 1e-4);
}

void testMp3Decode() {
    std::printf("mp3 decode\n");
    auto bytes = readFile(DRIFT_TEST_DATA_DIR "/tone440.mp3");
    CHECK(audio::detectFormat(bytes->data(), bytes->size()) == audio::Format::Mp3);
    auto dec = audio::openDecoder(bytes, true);
    CHECK(dec != nullptr);
    if (!dec) return;
    CHECK(dec->channels() == 1);
    CHECK(dec->sampleRate() == 44100);
    auto pcm = decodeAll(*dec);
    std::printf("  mp3 frames: reported %llu, decoded %zu\n",
                static_cast<unsigned long long>(dec->totalFrames()), pcm.size());
    CHECK(dec->totalFrames() == pcm.size());
    // LAME's gapless header lets the decoder trim padding to the exact length.
    CHECK(pcm.size() == 11025);
    CHECK_NEAR(zeroCrossings(pcm), 220, 6);
}

void testStreamingPlayback() {
    std::printf("streaming playback\n");
    for (const char* name : {"/tone440.ogg", "/tone440.mp3"}) {
        Mixer m(kRate);
        SoundHandle s = m.addEncoded(readFile(std::string(DRIFT_TEST_DATA_DIR) + name),
                                     LoadMode::Stream);
        CHECK(s.valid());
        CHECK(m.soundFrames(s) == 11025);
        PlayingSoundHandle h = m.play(s, {});
        CHECK(h.valid());
        auto out = render(m, kRate / 2);
        // 0.25 s at 44.1 kHz resampled to 48 kHz is 12000 frames.
        CHECK_NEAR(audibleLength(out), 12000, 4);
        CHECK(!m.isPlaying(h));
        CHECK(m.activeVoices() == 0);
    }
}

// Streamed music looping back to loopStart seeks inside the compressed file.
void testStreamedLoopStart() {
    std::printf("streamed loop start\n");
    for (const char* name : {"/tone440.ogg", "/tone440.mp3"}) {
        auto bytes = readFile(std::string(DRIFT_TEST_DATA_DIR) + name);
        auto dec = audio::openDecoder(bytes);
        auto ref = decodeAll(*dec);
        Mixer m(44100); // same rate as the file, so output samples match exactly
        SoundHandle s = m.addEncoded(bytes, LoadMode::Stream);
        CHECK(m.playMusic(s, {.loop = true, .loopStart = 5000.0 / 44100.0}));
        auto out = render(m, 11025 + 3000);
        float maxErr = 0.f;
        for (int k = 0; k < 11025; ++k)
            maxErr = std::max(maxErr, std::fabs(out[k * 2] - ref[k]));
        for (int k = 0; k < 3000; ++k)
            maxErr = std::max(maxErr, std::fabs(out[(11025 + k) * 2] - ref[5000 + k]));
        std::printf("  %s max error %g\n", name + 1, maxErr);
        CHECK(maxErr < 1e-3f);
    }
}

void testBasicMix() {
    std::printf("basic mix, pitch, resampling\n");
    Mixer m(kRate);
    SoundHandle s = constSound(m, 0.5f, 4800);
    PlayingSoundHandle h = m.play(s, {});
    CHECK(m.isPlaying(h));
    auto out = render(m, 9600);
    CHECK_NEAR(out[0], 0.5, 1e-6);
    CHECK_NEAR(out[1], 0.5, 1e-6);
    CHECK_NEAR(out[4799 * 2], 0.5, 1e-6);
    CHECK_NEAR(audibleLength(out), 4800, 1);
    CHECK(!m.isPlaying(h));

    m.play(s, {.pitch = 2.f});
    out = render(m, 9600);
    CHECK_NEAR(audibleLength(out), 2400, 1);

    m.play(s, {.pitch = 0.5f});
    out = render(m, 12000);
    CHECK_NEAR(audibleLength(out), 9600, 1);

    SoundHandle half = constSound(m, 0.5f, 2400, kRate / 2);
    m.play(half, {});
    out = render(m, 9600);
    CHECK_NEAR(audibleLength(out), 4800, 1);

    // Two voices sum.
    m.play(s, {});
    m.play(s, {});
    out = render(m, 256);
    CHECK_NEAR(out[100], 1.0, 1e-6);
}

void testPan() {
    std::printf("pan\n");
    Mixer m(kRate);
    SoundHandle s = constSound(m, 0.5f, 4800);
    m.play(s, {.pan = -1.f});
    auto out = render(m, 256);
    CHECK_NEAR(out[200], 0.5, 1e-6);
    CHECK_NEAR(out[201], 0.0, 1e-6);
    PlayingSoundHandle h = m.play(s, {.pan = 0.5f});
    m.stop(PlayingSoundHandle{}, 0.f); // invalid handles are ignored
    render(m, 4800);
    h = m.play(s, {.pan = 0.5f});
    out = render(m, 256);
    CHECK_NEAR(out[200], 0.25, 1e-6);
    CHECK_NEAR(out[201], 0.5, 1e-6);
    (void)h;
}

void testLoopAndStop() {
    std::printf("loop, stop with fade\n");
    Mixer m(kRate);
    SoundHandle s = constSound(m, 0.5f, 1000);
    PlayingSoundHandle h = m.play(s, {.loop = true});
    auto out = render(m, 4800);
    CHECK_NEAR(audibleLength(out), 4800, 0);
    CHECK(m.isPlaying(h));
    m.stop(h, 0.1f);
    CHECK(!m.isPlaying(h));
    out = render(m, 9600);
    // Halfway through the fade the level is about half.
    CHECK_NEAR(out[2400 * 2], 0.25, 0.02);
    CHECK_NEAR(audibleLength(out), 4800, Mixer::kSubBlock);
    CHECK(m.activeVoices() == 0);

    // setLooping(false) lets a looping voice finish its current pass.
    h = m.play(s, {.loop = true});
    render(m, 2500);
    m.setLooping(h, false);
    out = render(m, 4800);
    CHECK(audibleLength(out) <= 1000);
    CHECK(!m.isPlaying(h));
}

void testFadeInAndVolume() {
    std::printf("fade in, volume fades\n");
    Mixer m(kRate);
    SoundHandle s = constSound(m, 1.f, kRate);
    PlayingSoundHandle h = m.play(s, {.fadeIn = 0.1f});
    auto out = render(m, 9600);
    CHECK_NEAR(out[0], 0.0, 0.01);
    CHECK_NEAR(out[2400 * 2], 0.5, 0.02);
    CHECK_NEAR(out[6000 * 2], 1.0, 1e-6);
    m.setVolume(h, 0.2f, 0.f);
    out = render(m, 1024);
    CHECK_NEAR(out[600 * 2], 0.2, 1e-6);
    m.setVolume(h, 0.6f, 0.05f); // 2400 frames
    out = render(m, 4800);
    CHECK_NEAR(out[1200 * 2], 0.4, 0.02);
    CHECK_NEAR(out[3000 * 2], 0.6, 1e-6);
}

void testPauseResume() {
    std::printf("pause, resume\n");
    Mixer m(kRate);
    SoundHandle s = constSound(m, 0.5f, 4800);
    PlayingSoundHandle h = m.play(s, {});
    render(m, 1024);
    m.pause(h, 0.f);
    CHECK(m.isPaused(h));
    CHECK(m.isPlaying(h));
    auto out = render(m, 9600);
    CHECK(audibleLength(out) == 0);
    m.resume(h, 0.f);
    CHECK(!m.isPaused(h));
    out = render(m, 9600);
    // The remaining 3776 frames play after the resume.
    CHECK_NEAR(audibleLength(out), 4800 - 1024, 1);

    // Pause with a fade, then resume with a fade.
    h = m.play(s, {.loop = true});
    m.pause(h, 0.05f);
    out = render(m, 4800);
    CHECK(m.isPaused(h));
    CHECK_NEAR(audibleLength(out), 2400, Mixer::kSubBlock);
    m.resume(h, 0.05f);
    out = render(m, 4800);
    CHECK_NEAR(out[1200 * 2], 0.25, 0.02);
    CHECK_NEAR(out[4000 * 2], 0.5, 1e-6);
}

void testBuses() {
    std::printf("buses\n");
    Mixer m(kRate);
    SoundHandle s = constSound(m, 0.5f, kRate * 4);
    CHECK(m.findBus("master") == m.masterBus());
    CHECK(m.findBus("sfx") == m.sfxBus());
    CHECK(m.findBus("music") == m.musicBus());
    BusHandle ui = m.createBus("ui", m.sfxBus());
    CHECK(ui.valid());
    CHECK(m.createBus("ui", {}) == ui);
    CHECK(m.findBus("ui") == ui);

    m.play(s, {.bus = ui, .loop = true});
    m.setBusVolume(m.sfxBus(), 0.5f, 0.f);
    m.setBusVolume(ui, 0.5f, 0.f);
    auto out = render(m, 1024);
    CHECK_NEAR(out[800], 0.125, 1e-6);

    m.setBusMuted(m.masterBus(), true);
    CHECK(m.isBusMuted(m.masterBus()));
    out = render(m, 1024);
    CHECK_NEAR(out[800], 0.0, 1e-6);
    m.setBusMuted(m.masterBus(), false);
    out = render(m, 1024);
    CHECK_NEAR(out[800], 0.125, 1e-6);

    // Bus fade: 1.0 -> 0.0 over 0.5 s on ui (0.5 * 0.5 * level).
    m.setBusVolume(ui, 1.f, 0.f);
    render(m, 512);
    m.setBusVolume(ui, 0.f, 0.5f);
    CHECK_NEAR(m.busVolume(ui), 0.0, 1e-6);
    out = render(m, kRate);
    CHECK_NEAR(out[12000 * 2], 0.125, 0.01);
    CHECK_NEAR(out[30000 * 2], 0.0, 1e-6);

    // Paused buses hold their voices in place.
    m.stopBus(m.masterBus(), 0.f);
    render(m, 1024);
    CHECK(m.activeVoices() == 0);
    m.setBusVolume(ui, 1.f, 0.f);
    SoundHandle shortS = constSound(m, 0.5f, 2000);
    PlayingSoundHandle h = m.play(shortS, {.bus = ui});
    m.setBusPaused(m.sfxBus(), true);
    CHECK(m.isBusPaused(m.sfxBus()));
    out = render(m, 4800);
    CHECK(audibleLength(out) == 0);
    CHECK(m.isPlaying(h));
    m.setBusPaused(m.sfxBus(), false);
    out = render(m, 4800);
    CHECK_NEAR(audibleLength(out), 2000, 1);
}

void testInstanceLimitsAndStealing() {
    std::printf("max instances, voice stealing\n");
    Mixer m(kRate);
    SoundHandle s = constSound(m, 0.01f, kRate);
    m.setSoundMaxInstances(s, 2);
    PlayingSoundHandle a = m.play(s, {});
    PlayingSoundHandle b = m.play(s, {});
    PlayingSoundHandle c = m.play(s, {});
    CHECK(!m.isPlaying(a)); // the oldest instance makes room
    CHECK(m.isPlaying(b));
    CHECK(m.isPlaying(c));
    render(m, 1024);
    CHECK(m.activeVoices() == 2);

    Mixer m2(kRate);
    SoundHandle t = constSound(m2, 0.01f, kRate);
    for (int i = 0; i < Mixer::kMaxVoices; ++i) m2.play(t, {.loop = true, .priority = 5});
    CHECK(m2.activeVoices() == Mixer::kMaxVoices);
    CHECK(!m2.play(t, {.priority = 0}).valid());
    PlayingSoundHandle hi = m2.play(t, {.priority = 10});
    CHECK(hi.valid());
    CHECK(m2.isPlaying(hi));
    CHECK(m2.activeVoices() == Mixer::kMaxVoices);
}

void testDestroyWhilePlaying() {
    std::printf("destroy while playing\n");
    Mixer m(kRate);
    SoundHandle s = constSound(m, 0.5f, kRate);
    PlayingSoundHandle h = m.play(s, {});
    render(m, 512);
    m.destroySound(s);
    CHECK(!m.isPlaying(h));
    CHECK(!m.play(s, {}).valid());
    auto out = render(m, 512);
    CHECK(audibleLength(out) == 0);
    SoundHandle s2 = constSound(m, 0.5f, 100);
    CHECK(s2.index() == s.index());
    CHECK(s2 != s);
}

void testMusicCrossfade() {
    std::printf("music crossfade\n");
    Mixer m(kRate);
    SoundHandle a = constSound(m, 0.4f, kRate);
    SoundHandle b = constSound(m, -0.4f, kRate);
    CHECK(m.playMusic(a, {}));
    CHECK(m.isMusicPlaying());
    CHECK(m.currentMusic() == a);
    auto out = render(m, 4800);
    CHECK_NEAR(out[1000], 0.4, 1e-6);
    CHECK(m.playMusic(b, {.crossfade = 1.f}));
    CHECK(m.currentMusic() == b);
    out = render(m, kRate + 1024);
    CHECK_NEAR(out[(kRate / 2) * 2], 0.0, 0.02); // halfway: equal parts
    CHECK_NEAR(out[(kRate / 4) * 2], 0.2, 0.02);
    CHECK_NEAR(out[(kRate + 512) * 2], -0.4, 1e-6);
    // Music plays on the music bus.
    m.setBusVolume(m.musicBus(), 0.5f, 0.f);
    out = render(m, 1024);
    CHECK_NEAR(out[800], -0.2, 1e-6);
    m.pauseMusic(0.f);
    CHECK(!m.isMusicPlaying());
    m.resumeMusic(0.f);
    CHECK(m.isMusicPlaying());
    m.stopMusic(0.2f);
    CHECK(!m.isMusicPlaying());
    render(m, kRate);
    CHECK(m.activeVoices() == 0);
}

void testIntroLoop() {
    std::printf("intro + loop, loop start\n");
    Mixer m(kRate);
    SoundHandle intro = constSound(m, 0.2f, 1000);
    std::vector<float> ramp(2000);
    for (int i = 0; i < 2000; ++i) ramp[i] = 0.1f + 0.4f * i / 2000.f;
    SoundHandle body = m.addPcm(ramp.data(), ramp.size(), 1, kRate);
    double loopStart = 1000.0 / kRate; // halfway into the body
    CHECK(m.playMusic(body, {.loop = true, .loopStart = loopStart, .intro = intro}));
    auto out = render(m, 1000 + 2000 + 1000 + 10);
    CHECK_NEAR(out[0], 0.2, 1e-6);
    CHECK_NEAR(out[999 * 2], 0.2, 1e-6);
    CHECK_NEAR(out[1000 * 2], ramp[0], 1e-6);      // gapless switch to the body
    CHECK_NEAR(out[2999 * 2], ramp[1999], 1e-6);
    CHECK_NEAR(out[3000 * 2], ramp[1000], 1e-6);   // loops back to loopStart
    CHECK_NEAR(out[3999 * 2], ramp[1999], 1e-6);
    CHECK_NEAR(out[4000 * 2], ramp[1000], 1e-6);
    CHECK(m.currentMusic() == body);
}

void testPlaylist() {
    std::printf("playlist gapless and crossfaded\n");
    Mixer m(kRate);
    SoundHandle a = constSound(m, 0.1f, 1000);
    SoundHandle b = constSound(m, 0.3f, 1500);
    CHECK(m.playPlaylist({a, b}, {}));
    auto out = render(m, 4900);
    CHECK(m.currentMusic() == b);
    auto tail = render(m, 100);
    out.insert(out.end(), tail.begin(), tail.end());
    CHECK_NEAR(out[999 * 2], 0.1, 1e-6);
    CHECK_NEAR(out[1000 * 2], 0.3, 1e-6);
    CHECK_NEAR(out[2499 * 2], 0.3, 1e-6);
    CHECK_NEAR(out[2500 * 2], 0.1, 1e-6); // loops back to the first track
    CHECK_NEAR(out[3500 * 2], 0.3, 1e-6);
    CHECK(audibleLength(out) == 5000);

    // Non-looping playlist stops after the last track.
    m.playPlaylist({a, b}, {.loop = false});
    out = render(m, 5000);
    CHECK_NEAR(audibleLength(out), 2500, Mixer::kSubBlock);
    CHECK(!m.isMusicPlaying());

    // Crossfaded playlist: tracks overlap for the crossfade time.
    SoundHandle c = constSound(m, 0.2f, kRate);
    SoundHandle d = constSound(m, -0.2f, kRate);
    m.playPlaylist({c, d}, {.crossfade = 0.25f});
    out = render(m, kRate);
    CHECK(m.currentMusic() == d);
    CHECK_NEAR(out[1000 * 2], 0.2 * 1000.0 / 12000.0, 0.01); // fading in at the start
    CHECK_NEAR(out[24000 * 2], 0.2, 1e-6);
    // 0.875 s: halfway through the crossfade, the sum is near zero.
    CHECK_NEAR(out[42000 * 2], 0.0, 0.03);
    m.nextTrack();
    CHECK(m.currentMusic() == c);

    // Shuffle never repeats the same track twice in a row.
    SoundHandle e = constSound(m, 0.3f, 600);
    m.playPlaylist({a, b, e}, {.shuffle = true});
    SoundHandle last = m.currentMusic();
    int changes = 0;
    for (int i = 0; i < 40; ++i) {
        m.nextTrack();
        CHECK(m.currentMusic() != last);
        if (m.currentMusic() != last) ++changes;
        last = m.currentMusic();
    }
    CHECK(changes == 40);
}

void testSpatialize() {
    std::printf("spatialize\n");
    AudioSource s;
    s.minDistance = 100.f;
    s.maxDistance = 500.f;
    SpatialMix m = spatialize({0, 0}, {50, 0}, s);
    CHECK_NEAR(m.gain, 1.0, 1e-6);
    CHECK_NEAR(m.pan, 0.1, 1e-6);
    m = spatialize({0, 0}, {300, 0}, s);
    CHECK_NEAR(m.gain, 0.5, 1e-6);
    CHECK_NEAR(m.pan, 0.6, 1e-6);
    m = spatialize({0, 0}, {-600, 0}, s);
    CHECK_NEAR(m.gain, 0.0, 1e-6);
    CHECK_NEAR(m.pan, -1.0, 1e-6);
    m = spatialize({100, 100}, {100, 400}, s); // straight below: centred
    CHECK_NEAR(m.gain, 0.5, 1e-6);
    CHECK_NEAR(m.pan, 0.0, 1e-6);
    s.rolloff = Rolloff::Inverse;
    m = spatialize({0, 0}, {200, 0}, s); // min/d = 0.5, at max 0.2
    CHECK_NEAR(m.gain, (0.5 - 0.2) / 0.8, 1e-5);
    m = spatialize({0, 0}, {499, 0}, s);
    CHECK(m.gain < 0.01f);
}

// AudioResource end to end on SDL's dummy driver: the stream callback pulls
// from the mixer in real time.
void testAudioResource() {
    std::printf("AudioResource on the dummy driver\n");
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
    AudioResource audio;
    CHECK(audio.hasDevice());
    SoundHandle s = audio.loadSound(DRIFT_TEST_DATA_DIR "/tone440.ogg");
    CHECK(s.valid());
    CHECK_NEAR(audio.soundDuration(s), 0.25, 1e-6);
    CHECK(!audio.loadSound(DRIFT_TEST_DATA_DIR "/missing.ogg").valid());
    PlayingSoundHandle h = audio.playSound(s);
    CHECK(audio.isSoundPlaying(h));
    CHECK(audio.playMusic(DRIFT_TEST_DATA_DIR "/tone440.mp3", {.loop = true}));
    for (int i = 0; i < 100 && audio.isSoundPlaying(h); ++i) SDL_Delay(10);
    CHECK(!audio.isSoundPlaying(h)); // finished after about 0.25 s
    CHECK(audio.isMusicPlaying());   // the looping track keeps going
    audio.stopMusic();
    CHECK(!audio.isMusicPlaying());
}

// Without a device, update() advances playback so sounds still end.
void testNoDevice() {
    std::printf("AudioResource without a device\n");
    // Override priority, so an SDL_AUDIO_DRIVER in the environment does not
    // win over the test's choice of driver.
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "no-such-driver", SDL_HINT_OVERRIDE);
    AudioResource audio;
    CHECK(!audio.hasDevice());
    std::vector<float> pcm(4800, 0.25f); // 0.1 s
    SoundHandle s = audio.createSound(pcm.data(), pcm.size(), 1, 48000);
    PlayingSoundHandle h = audio.playSound(s);
    CHECK(audio.isSoundPlaying(h));
    audio.update(0.05f);
    CHECK(audio.isSoundPlaying(h));
    audio.update(0.06f);
    CHECK(!audio.isSoundPlaying(h));
    SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
}

// The AudioSource system on a bare World: autoplay, restart on request, and
// stopping when the entity goes away.
void testAudioSourceSystem() {
    std::printf("AudioSource system\n");
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
    AudioResource audio;
    AudioSourceTracking tracking;
    World world;
    ComponentId tId = world.registerComponent<Transform2D>("Transform2D");
    world.registerComponent<GlobalTransform2D>("GlobalTransform2D");
    ComponentId srcId = world.registerComponent<AudioSource>("AudioSource");
    ComponentId lisId = world.registerComponent<AudioListener>("AudioListener");

    std::vector<float> pcm(4800, 0.25f);
    SoundHandle s = audio.createSound(pcm.data(), pcm.size(), 1, 48000);

    EntityId listener = world.createEntity();
    world.set(listener, tId, Transform2D{{0.f, 0.f}});
    world.set(listener, lisId, AudioListener{});

    EntityId e = world.createEntity();
    world.set(e, tId, Transform2D{{300.f, 0.f}});
    AudioSource src;
    src.sound = s;
    src.loop = true;
    src.spatial = true;
    src.minDistance = 100.f;
    src.maxDistance = 500.f;
    world.set(e, srcId, src);

    EntityId quiet = world.createEntity();
    AudioSource manual;
    manual.sound = s;
    manual.autoplay = false;
    world.set(quiet, srcId, manual);

    auto run = [&]() {
        audioSourceUpdate(
            ResMut<AudioResource>(&audio), ResMut<AudioSourceTracking>(&tracking),
            Query<AudioListener, Optional<Transform2D>, Optional<GlobalTransform2D>>(
                world, world.componentRegistry()),
            QueryMut<AudioSource, Optional<Transform2D>, Optional<GlobalTransform2D>>(
                world, world.componentRegistry()));
    };
    run();
    const AudioSource* live = world.get<AudioSource>(e, srcId);
    CHECK(live->instance.valid());
    CHECK(audio.isSoundPlaying(live->instance));
    CHECK(!world.get<AudioSource>(quiet, srcId)->instance.valid());

    PlayingSoundHandle first = live->instance;
    world.getMut<AudioSource>(e, srcId)->playRequested = true;
    run();
    live = world.get<AudioSource>(e, srcId);
    CHECK(!live->playRequested);
    CHECK(live->instance.valid() && live->instance != first);
    CHECK(!audio.isSoundPlaying(first));
    CHECK(audio.isSoundPlaying(live->instance));

    world.getMut<AudioSource>(quiet, srcId)->playRequested = true;
    run();
    CHECK(audio.isSoundPlaying(world.get<AudioSource>(quiet, srcId)->instance));

    PlayingSoundHandle current = live->instance;
    world.destroyEntity(e);
    run();
    CHECK(!audio.isSoundPlaying(current));
}

} // namespace

int main() {
    testWavDecode();
    testOggDecode();
    testMp3Decode();
    testStreamingPlayback();
    testStreamedLoopStart();
    testBasicMix();
    testPan();
    testLoopAndStop();
    testFadeInAndVolume();
    testPauseResume();
    testBuses();
    testInstanceLimitsAndStealing();
    testDestroyWhilePlaying();
    testMusicCrossfade();
    testIntroLoop();
    testPlaylist();
    testSpatialize();
    testAudioResource();
    testAudioSourceSystem();
    testNoDevice();
    SDL_Quit();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
