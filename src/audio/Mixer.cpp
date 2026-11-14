#include "audio/Mixer.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

namespace drift::audio {

namespace {

constexpr int kChunk = 256;              // decoded frames buffered per voice
constexpr uint32_t kNone = UINT32_MAX;
constexpr int kMusicPriority = INT_MAX / 2;

struct Ramp {
    float value = 1.f;
    float target = 1.f;
    float step = 0.f; // per frame

    void set(float t, float seconds, int rate) {
        target = t;
        if (seconds <= 0.f) {
            value = t;
            step = 0.f;
        } else {
            step = std::fabs(t - value) / (seconds * static_cast<float>(rate));
        }
    }
    void jump(float v) { value = target = v; step = 0.f; }
    void advance(int frames) {
        if (value == target) return;
        float d = step * static_cast<float>(frames);
        if (step <= 0.f) value = target;
        else if (value < target) value = std::min(target, value + d);
        else value = std::max(target, value - d);
    }
    bool settled() const { return value == target; }
};

uint32_t nextGen(uint32_t g) {
    g = (g + 1) & 0xFFF;
    return g == 0 ? 1 : g;
}

} // namespace

struct Mixer::Sound {
    bool alive = false;
    uint32_t gen = 0;
    int channels = 0;
    int rate = 0;
    uint64_t frames = 0;
    std::vector<float> pcm; // decoded sounds
    Bytes encoded;          // streamed sounds
    int maxInstances = 0;
};

struct Mixer::Bus {
    std::string name;
    uint32_t parent = kNone;
    Ramp level;
    bool muted = false;
    bool paused = false;
    float effGain = 1.f;
    bool effPaused = false;
};

struct Mixer::Voice {
    bool active = false;
    uint32_t gen = 0;
    uint32_t sound = kNone;        // the sound this voice was started with
    uint32_t current = kNone;      // the sound currently being read
    std::unique_ptr<Decoder> dec;  // streamed sounds
    uint64_t srcPos = 0;           // frames handed to the chunk buffer
    int channels = 1;
    int rate = 0;
    std::vector<uint32_t> queue;   // sounds to continue with, gapless
    uint32_t chained = 0;          // how many times the source advanced via queue

    float chunk[kChunk * 2]{};
    int chunkLen = 0;
    int chunkIdx = 0;
    float a[2]{};
    float b[2]{};
    double phase = 0.0;
    int endState = 0; // 0 running, 1 source exhausted, 2 finished

    Ramp volume;
    Ramp fade;
    float pan = 0.f;
    float pitch = 1.f;
    bool loop = false;
    double loopStart = 0.0;

    bool paused = false;
    bool stopping = false;
    bool pauseAtFadeEnd = false;
    bool fresh = true;
    int priority = 0;
    uint64_t order = 0;
    uint32_t bus = 0;
    float gainL = 0.f;
    float gainR = 0.f;
};

struct Mixer::Music {
    PlayingSoundHandle voice;
    SoundHandle track;
    bool playlist = false;
    std::vector<SoundHandle> tracks;
    PlaylistParams params;
    int index = 0;
    int queuedIndex = -1;
    uint32_t seenChain = 0;
    uint32_t rng = 0x9E3779B9u;
};

// ---------------------------------------------------------------------------

Mixer::Mixer(int sampleRate)
    : sampleRate_(sampleRate), voices_(kMaxVoices), music_(std::make_unique<Music>()) {
    buses_.resize(3);
    buses_[0].name = "master";
    buses_[1].name = "music";
    buses_[1].parent = 0;
    buses_[2].name = "sfx";
    buses_[2].parent = 0;
}

Mixer::~Mixer() = default;

// ---- lookup ---------------------------------------------------------------

Mixer::Sound* Mixer::sound(SoundHandle s) {
    if (!s.valid() || s.index() >= sounds_.size()) return nullptr;
    Sound& d = sounds_[s.index()];
    return d.alive && d.gen == s.generation() ? &d : nullptr;
}
const Mixer::Sound* Mixer::sound(SoundHandle s) const {
    return const_cast<Mixer*>(this)->sound(s);
}

Mixer::Voice* Mixer::voice(PlayingSoundHandle h) {
    if (!h.valid() || h.index() >= voices_.size()) return nullptr;
    Voice& v = voices_[h.index()];
    return v.active && v.gen == h.generation() ? &v : nullptr;
}
const Mixer::Voice* Mixer::voice(PlayingSoundHandle h) const {
    return const_cast<Mixer*>(this)->voice(h);
}

Mixer::Bus* Mixer::bus(BusHandle b) {
    if (!b.valid() || b.id > buses_.size()) return nullptr;
    return &buses_[b.id - 1];
}
const Mixer::Bus* Mixer::bus(BusHandle b) const {
    return const_cast<Mixer*>(this)->bus(b);
}

bool Mixer::busUnder(uint32_t index, uint32_t ancestor) const {
    while (index != kNone) {
        if (index == ancestor) return true;
        index = buses_[index].parent;
    }
    return false;
}

// ---- sounds ---------------------------------------------------------------

SoundHandle Mixer::store(Sound&& s) {
    uint32_t idx = 0;
    while (idx < sounds_.size() && sounds_[idx].alive) ++idx;
    if (idx == sounds_.size()) sounds_.emplace_back();
    uint32_t gen = nextGen(sounds_[idx].gen);
    sounds_[idx] = std::move(s);
    sounds_[idx].alive = true;
    sounds_[idx].gen = gen;
    return SoundHandle::make(idx, gen);
}

SoundHandle Mixer::addEncoded(Bytes bytes, LoadMode mode) {
    Sound s;
    if (!decode(std::move(bytes), mode, s)) return {};
    return store(std::move(s));
}

bool Mixer::decode(Bytes bytes, LoadMode mode, Sound& s) {
    auto dec = openDecoder(bytes, true);
    if (!dec) return false;
    s.channels = dec->channels();
    s.rate = dec->sampleRate();
    s.frames = dec->totalFrames();
    if (mode == LoadMode::Stream) {
        s.encoded = std::move(bytes);
    } else {
        std::vector<float> pcm;
        if (s.frames) pcm.reserve(static_cast<size_t>(s.frames) * s.channels);
        float buf[4096 * 2];
        for (;;) {
            uint64_t got = dec->read(buf, 4096);
            if (got == 0) break;
            pcm.insert(pcm.end(), buf, buf + got * s.channels);
        }
        s.frames = pcm.size() / s.channels;
        s.pcm = std::move(pcm);
    }
    return true;
}

bool Mixer::replaceSound(SoundHandle s, Bytes bytes) {
    Sound* d = sound(s);
    if (!d) return false;
    Sound next;
    if (!decode(std::move(bytes), d->encoded ? LoadMode::Stream : LoadMode::Decode, next)) return false;
    stopVoicesOf(s.index());
    next.alive = true;
    next.gen = d->gen;
    next.maxInstances = d->maxInstances;
    *d = std::move(next);
    return true;
}

void Mixer::stopVoicesOf(uint32_t idx) {
    for (Voice& v : voices_) {
        if (!v.active) continue;
        if (v.current == idx || v.sound == idx) {
            kill(v);
            continue;
        }
        v.queue.erase(std::remove(v.queue.begin(), v.queue.end(), idx), v.queue.end());
    }
}

SoundHandle Mixer::addPcm(const float* samples, size_t frames, int channels, int rate) {
    if (!samples || frames == 0 || channels < 1 || channels > 2 || rate <= 0) return {};
    Sound s;
    s.channels = channels;
    s.rate = rate;
    s.frames = frames;
    s.pcm.assign(samples, samples + frames * channels);
    return store(std::move(s));
}

void Mixer::destroySound(SoundHandle s) {
    Sound* d = sound(s);
    if (!d) return;
    stopVoicesOf(s.index());
    uint32_t gen = d->gen;
    *d = Sound{};
    d->gen = gen;
}

double Mixer::soundDuration(SoundHandle s) const {
    const Sound* d = sound(s);
    return d && d->rate ? static_cast<double>(d->frames) / d->rate : 0.0;
}
uint64_t Mixer::soundFrames(SoundHandle s) const {
    const Sound* d = sound(s);
    return d ? d->frames : 0;
}
int Mixer::soundChannels(SoundHandle s) const {
    const Sound* d = sound(s);
    return d ? d->channels : 0;
}
int Mixer::soundRate(SoundHandle s) const {
    const Sound* d = sound(s);
    return d ? d->rate : 0;
}

void Mixer::setSoundMaxInstances(SoundHandle s, int maxInstances) {
    if (Sound* d = sound(s)) d->maxInstances = std::max(0, maxInstances);
}

// ---- buses ----------------------------------------------------------------

BusHandle Mixer::createBus(const char* name, BusHandle parent) {
    if (!name) return {};
    BusHandle existing = findBus(name);
    if (existing.valid()) return existing;
    if (!bus(parent)) parent = masterBus();
    Bus b;
    b.name = name;
    b.parent = parent.id - 1;
    buses_.push_back(std::move(b));
    return BusHandle{static_cast<uint32_t>(buses_.size())};
}

BusHandle Mixer::findBus(const char* name) const {
    if (!name) return {};
    for (size_t i = 0; i < buses_.size(); ++i)
        if (buses_[i].name == name) return BusHandle{static_cast<uint32_t>(i + 1)};
    return {};
}

void Mixer::setBusVolume(BusHandle b, float volume, float fadeSeconds) {
    if (Bus* d = bus(b)) d->level.set(std::max(0.f, volume), fadeSeconds, sampleRate_);
}
float Mixer::busVolume(BusHandle b) const {
    const Bus* d = bus(b);
    return d ? d->level.target : 0.f;
}
void Mixer::setBusMuted(BusHandle b, bool muted) {
    if (Bus* d = bus(b)) d->muted = muted;
}
bool Mixer::isBusMuted(BusHandle b) const {
    const Bus* d = bus(b);
    return d && d->muted;
}
void Mixer::setBusPaused(BusHandle b, bool paused) {
    if (Bus* d = bus(b)) d->paused = paused;
}
bool Mixer::isBusPaused(BusHandle b) const {
    const Bus* d = bus(b);
    return d && d->paused;
}

void Mixer::stopBus(BusHandle b, float fadeOut) {
    if (!bus(b)) return;
    for (Voice& v : voices_)
        if (v.active && busUnder(v.bus, b.id - 1)) beginStop(v, fadeOut);
    if (!voice(music_->voice) || voice(music_->voice)->stopping) music_->playlist = false;
}

void Mixer::updateBuses(int frames) {
    for (size_t i = 0; i < buses_.size(); ++i) {
        Bus& b = buses_[i];
        b.level.advance(frames);
        float g = b.muted ? 0.f : b.level.value;
        bool p = b.paused;
        if (b.parent != kNone) {
            g *= buses_[b.parent].effGain;
            p = p || buses_[b.parent].effPaused;
        }
        b.effGain = g;
        b.effPaused = p;
    }
}

// ---- voices ---------------------------------------------------------------

bool Mixer::openSource(Voice& v, uint32_t idx) {
    if (idx >= sounds_.size() || !sounds_[idx].alive) return false;
    const Sound& s = sounds_[idx];
    v.dec.reset();
    if (s.encoded) {
        v.dec = openDecoder(s.encoded);
        if (!v.dec) return false;
    }
    v.current = idx;
    v.srcPos = 0;
    v.channels = s.channels;
    v.rate = s.rate;
    v.chunkLen = v.chunkIdx = 0;
    return true;
}

bool Mixer::refill(Voice& v) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (v.current == kNone) return false;
        const Sound& s = sounds_[v.current];
        uint64_t got = 0;
        if (v.dec) {
            got = v.dec->read(v.chunk, kChunk);
        } else if (v.srcPos < s.frames) {
            got = std::min<uint64_t>(kChunk, s.frames - v.srcPos);
            std::memcpy(v.chunk, s.pcm.data() + v.srcPos * s.channels,
                        sizeof(float) * got * s.channels);
        }
        if (got > 0) {
            v.srcPos += got;
            v.chunkLen = static_cast<int>(got);
            v.chunkIdx = 0;
            return true;
        }
        // End of the current source.
        if (!v.queue.empty()) {
            uint32_t next = v.queue.front();
            v.queue.erase(v.queue.begin());
            if (!openSource(v, next)) return false;
            ++v.chained;
            continue;
        }
        if (v.loop) {
            uint64_t start = static_cast<uint64_t>(std::max(0.0, v.loopStart) * s.rate);
            if (s.frames && start >= s.frames) start = 0;
            if (v.dec) {
                if (!v.dec->seek(start)) return false;
            }
            v.srcPos = start;
            continue;
        }
        return false;
    }
    return false;
}

bool Mixer::fetch(Voice& v, float* frame) {
    if (v.chunkIdx >= v.chunkLen && !refill(v)) return false;
    const float* f = v.chunk + v.chunkIdx * v.channels;
    frame[0] = f[0];
    frame[1] = v.channels == 2 ? f[1] : f[0];
    ++v.chunkIdx;
    return true;
}

PlayingSoundHandle Mixer::startVoice(SoundHandle s, const PlayParams& p,
                                     SoundHandle queued, double loopStart) {
    Sound* d = sound(s);
    if (!d) return {};
    uint32_t sidx = s.index();

    // Per-sound instance cap: fade out the oldest instance.
    if (d->maxInstances > 0) {
        int count = 0;
        Voice* oldest = nullptr;
        for (Voice& v : voices_) {
            if (!v.active || v.stopping || v.sound != sidx) continue;
            ++count;
            if (!oldest || v.order < oldest->order) oldest = &v;
        }
        if (count >= d->maxInstances && oldest) beginStop(*oldest, 0.f);
    }

    // Find a free voice, or steal the least important one.
    Voice* slot = nullptr;
    for (Voice& v : voices_) {
        if (!v.active) { slot = &v; break; }
    }
    if (!slot) {
        auto rank = [](const Voice& v) { return v.stopping ? INT_MIN : v.priority; };
        for (Voice& v : voices_) {
            if (!slot || rank(v) < rank(*slot) ||
                (rank(v) == rank(*slot) && v.order < slot->order))
                slot = &v;
        }
        if (rank(*slot) > p.priority) return {};
        kill(*slot);
    }

    Voice& v = *slot;
    uint32_t gen = nextGen(v.gen);
    v = Voice{};
    v.gen = gen;
    if (!openSource(v, sidx)) return {};
    v.active = true;
    v.sound = sidx;
    if (sound(queued)) v.queue.push_back(queued.index());
    v.volume.jump(std::max(0.f, p.volume));
    v.fade.jump(p.fadeIn > 0.f ? 0.f : 1.f);
    if (p.fadeIn > 0.f) v.fade.set(1.f, p.fadeIn, sampleRate_);
    v.pan = std::clamp(p.pan, -1.f, 1.f);
    v.pitch = std::max(0.01f, p.pitch);
    v.loop = p.loop;
    v.loopStart = loopStart;
    v.priority = p.priority;
    v.order = ++order_;
    BusHandle b = bus(p.bus) ? p.bus : sfxBus();
    v.bus = b.id - 1;

    if (!fetch(v, v.a)) {
        kill(v);
        return {};
    }
    if (!fetch(v, v.b)) {
        v.b[0] = v.b[1] = 0.f;
        v.endState = 1;
    }
    return PlayingSoundHandle::make(static_cast<uint32_t>(&v - voices_.data()), gen);
}

PlayingSoundHandle Mixer::play(SoundHandle s, const PlayParams& p) {
    return startVoice(s, p, {}, 0.0);
}

void Mixer::kill(Voice& v) {
    v.active = false;
    v.dec.reset();
    v.queue.clear();
}

void Mixer::beginStop(Voice& v, float fadeOut) {
    if (!v.active) return;
    // A zero fade still ramps over one sub-block so the cut does not click.
    float minFade = static_cast<float>(kSubBlock) / sampleRate_;
    v.stopping = true;
    v.pauseAtFadeEnd = false;
    v.fade.set(0.f, std::max(fadeOut, minFade), sampleRate_);
    if (v.paused) kill(v);
}

void Mixer::stop(PlayingSoundHandle h, float fadeOut) {
    if (Voice* v = voice(h)) beginStop(*v, fadeOut);
}

void Mixer::pause(PlayingSoundHandle h, float fadeOut) {
    Voice* v = voice(h);
    if (!v || v->stopping || v->paused) return;
    if (fadeOut > 0.f) {
        v->fade.set(0.f, fadeOut, sampleRate_);
        v->pauseAtFadeEnd = true;
    } else {
        v->paused = true;
    }
}

void Mixer::resume(PlayingSoundHandle h, float fadeIn) {
    Voice* v = voice(h);
    if (!v || v->stopping) return;
    v->paused = false;
    v->pauseAtFadeEnd = false;
    if (fadeIn > 0.f) {
        if (v->fade.value >= 1.f) v->fade.jump(0.f);
        v->fade.set(1.f, fadeIn, sampleRate_);
    } else {
        v->fade.jump(1.f);
    }
}

void Mixer::setVolume(PlayingSoundHandle h, float volume, float fadeSeconds) {
    if (Voice* v = voice(h)) v->volume.set(std::max(0.f, volume), fadeSeconds, sampleRate_);
}
void Mixer::setPan(PlayingSoundHandle h, float pan) {
    if (Voice* v = voice(h)) v->pan = std::clamp(pan, -1.f, 1.f);
}
void Mixer::setPitch(PlayingSoundHandle h, float pitch) {
    if (Voice* v = voice(h)) v->pitch = std::max(0.01f, pitch);
}
void Mixer::setLooping(PlayingSoundHandle h, bool loop) {
    if (Voice* v = voice(h)) v->loop = loop;
}
bool Mixer::isPlaying(PlayingSoundHandle h) const {
    const Voice* v = voice(h);
    return v && !v->stopping;
}
bool Mixer::isPaused(PlayingSoundHandle h) const {
    const Voice* v = voice(h);
    return v && !v->stopping && v->paused;
}
int Mixer::activeVoices() const {
    int n = 0;
    for (const Voice& v : voices_) n += v.active ? 1 : 0;
    return n;
}

double Mixer::remainingFrames(const Voice& v) const {
    if (v.current == kNone) return 0.0;
    const Sound& s = sounds_[v.current];
    if (s.frames == 0) return 1e18;
    uint64_t pending = static_cast<uint64_t>(v.chunkLen - v.chunkIdx);
    uint64_t consumed = v.srcPos > pending ? v.srcPos - pending : 0;
    double srcLeft = static_cast<double>(s.frames > consumed ? s.frames - consumed : 0);
    double step = static_cast<double>(v.rate) / sampleRate_ * v.pitch;
    return srcLeft / step;
}

void Mixer::renderVoice(Voice& v, float* out, int frames) {
    const Bus& b = buses_[v.bus];
    if (v.paused || b.effPaused) {
        v.gainL = v.gainR = 0.f;
        v.fresh = false;
        return;
    }

    float panL = std::min(1.f, 1.f - v.pan);
    float panR = std::min(1.f, 1.f + v.pan);
    if (v.fresh) {
        // Start from the gain at the first frame instead of ramping from 0.
        float g0 = v.volume.value * v.fade.value * b.effGain;
        v.gainL = g0 * panL;
        v.gainR = g0 * panR;
        v.fresh = false;
    }
    v.volume.advance(frames);
    v.fade.advance(frames);

    float g = v.volume.value * v.fade.value * b.effGain;
    float tl = g * panL;
    float tr = g * panR;
    float gl = v.gainL, gr = v.gainR;
    float dl = (tl - gl) / frames, dr = (tr - gr) / frames;
    double step = static_cast<double>(v.rate) / sampleRate_ * v.pitch;

    for (int i = 0; i < frames && v.endState != 2; ++i) {
        gl += dl;
        gr += dr;
        float t = static_cast<float>(v.phase);
        out[i * 2 + 0] += (v.a[0] + (v.b[0] - v.a[0]) * t) * gl;
        out[i * 2 + 1] += (v.a[1] + (v.b[1] - v.a[1]) * t) * gr;
        v.phase += step;
        while (v.phase >= 1.0) {
            v.phase -= 1.0;
            v.a[0] = v.b[0];
            v.a[1] = v.b[1];
            if (v.endState == 1) { v.endState = 2; break; }
            if (!fetch(v, v.b)) {
                v.b[0] = v.b[1] = 0.f;
                v.endState = 1;
            }
            // The source may have switched rate via the queue.
            step = static_cast<double>(v.rate) / sampleRate_ * v.pitch;
        }
    }
    v.gainL = tl;
    v.gainR = tr;

    if (v.endState == 2) {
        kill(v);
        return;
    }
    if (v.fade.settled() && v.fade.value <= 0.f) {
        if (v.stopping) kill(v);
        else if (v.pauseAtFadeEnd) {
            v.paused = true;
            v.pauseAtFadeEnd = false;
        }
    }
}

void Mixer::mix(float* out, int frames) {
    std::memset(out, 0, sizeof(float) * 2 * static_cast<size_t>(frames));
    for (int off = 0; off < frames; off += kSubBlock) {
        int n = std::min(kSubBlock, frames - off);
        updateBuses(n);
        for (Voice& v : voices_)
            if (v.active) renderVoice(v, out + off * 2, n);
        updateMusic();
    }
    for (int i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.f, 1.f);
}

// ---- music ----------------------------------------------------------------

bool Mixer::playMusic(SoundHandle track, const MusicParams& p) {
    if (!sound(track)) return false;
    music_->playlist = false;
    if (Voice* old = voice(music_->voice)) beginStop(*old, p.crossfade);

    PlayParams pp;
    pp.bus = musicBus();
    pp.volume = p.volume;
    pp.loop = p.loop;
    pp.fadeIn = p.crossfade;
    pp.priority = kMusicPriority;
    SoundHandle first = sound(p.intro) ? p.intro : track;
    SoundHandle queued = sound(p.intro) ? track : SoundHandle{};
    music_->voice = startVoice(first, pp, queued, p.loopStart);
    music_->track = track;
    return music_->voice.valid();
}

int Mixer::nextPlaylistIndex() {
    Music& m = *music_;
    int n = static_cast<int>(m.tracks.size());
    if (n == 0) return -1;
    if (m.params.shuffle) {
        if (n == 1) return m.params.loop ? 0 : -1;
        m.rng ^= m.rng << 13;
        m.rng ^= m.rng >> 17;
        m.rng ^= m.rng << 5;
        int r = static_cast<int>(m.rng % static_cast<uint32_t>(n - 1));
        return r >= m.index ? r + 1 : r;
    }
    int next = m.index + 1;
    if (next >= n) return m.params.loop ? 0 : -1;
    return next;
}

void Mixer::startPlaylistTrack(int index, float fade) {
    Music& m = *music_;
    Voice* old = voice(m.voice);
    if (old && !old->stopping) beginStop(*old, fade);
    m.index = index;
    PlayParams pp;
    pp.bus = musicBus();
    pp.volume = m.params.volume;
    pp.fadeIn = fade;
    pp.priority = kMusicPriority;
    SoundHandle queued;
    m.queuedIndex = -1;
    if (m.params.crossfade <= 0.f) {
        m.queuedIndex = nextPlaylistIndex();
        if (m.queuedIndex >= 0) queued = m.tracks[m.queuedIndex];
    }
    m.voice = startVoice(m.tracks[index], pp, queued, 0.0);
    m.track = m.tracks[index];
    m.seenChain = 0;
}

bool Mixer::playPlaylist(const std::vector<SoundHandle>& tracks, const PlaylistParams& p) {
    Music& m = *music_;
    m.tracks.clear();
    for (SoundHandle t : tracks)
        if (sound(t)) m.tracks.push_back(t);
    if (m.tracks.empty()) return false;
    m.params = p;
    m.playlist = true;
    m.index = 0;
    if (p.shuffle) {
        m.index = -1;
        m.rng ^= static_cast<uint32_t>(order_ * 2654435761u);
        if (m.rng == 0) m.rng = 1;
        m.index = static_cast<int>(m.rng % m.tracks.size());
    }
    startPlaylistTrack(m.index, p.crossfade);
    return voice(m.voice) != nullptr;
}

void Mixer::nextTrack() {
    Music& m = *music_;
    if (!m.playlist) return;
    int next = nextPlaylistIndex();
    if (next < 0) {
        stopMusic(m.params.crossfade);
        return;
    }
    startPlaylistTrack(next, m.params.crossfade);
}

void Mixer::updateMusic() {
    Music& m = *music_;
    if (!m.playlist) return;
    Voice* v = voice(m.voice);
    if (!v) {
        int next = nextPlaylistIndex();
        if (next < 0) {
            m.playlist = false;
            return;
        }
        startPlaylistTrack(next, 0.f);
        return;
    }
    if (v->stopping) return;
    if (m.params.crossfade <= 0.f) {
        // Gapless: the voice already moved on to the queued track.
        if (v->chained != m.seenChain) {
            m.seenChain = v->chained;
            m.index = m.queuedIndex;
            m.track = m.tracks[m.index];
            m.queuedIndex = nextPlaylistIndex();
            if (m.queuedIndex >= 0) v->queue.push_back(m.tracks[m.queuedIndex].index());
        }
        return;
    }
    double remaining = remainingFrames(*v);
    double fadeFrames = static_cast<double>(m.params.crossfade) * sampleRate_;
    if (remaining <= fadeFrames) {
        int next = nextPlaylistIndex();
        if (next < 0) return; // last track plays out
        float fadeOut = static_cast<float>(remaining / sampleRate_);
        beginStop(*v, fadeOut);
        startPlaylistTrack(next, m.params.crossfade);
    }
}

void Mixer::stopMusic(float fadeOut) {
    music_->playlist = false;
    if (Voice* v = voice(music_->voice)) beginStop(*v, fadeOut);
    music_->voice = {};
    music_->track = {};
}

void Mixer::pauseMusic(float fadeOut) { pause(music_->voice, fadeOut); }
void Mixer::resumeMusic(float fadeIn) { resume(music_->voice, fadeIn); }

bool Mixer::isMusicPlaying() const {
    const Voice* v = voice(music_->voice);
    return v && !v->stopping && !v->paused && !v->pauseAtFadeEnd;
}

SoundHandle Mixer::currentMusic() const {
    return voice(music_->voice) ? music_->track : SoundHandle{};
}

} // namespace drift::audio
