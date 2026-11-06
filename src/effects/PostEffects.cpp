// =============================================================================
// Drift 2D Game Engine - Post effect library
// =============================================================================
// Ready-made post effects on top of RendererResource::addPostEffect. Each
// enabled effect is one renderer post effect that reads its live parameters
// every frame. Shaders are compiled the first time an effect needs them.
// =============================================================================

#include <drift/effects/PostEffects.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/Log.hpp>

#include "effects_shaders.gen.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

namespace drift {

namespace {

enum class Pass : int {
    BloomPrefilter, BloomDown, BloomUp, BloomComposite,
    Blur, Vignette, Crt, ColorGrading, Pixelate, Tone, Fade,
    Count
};

struct ShaderSource { const char* wgsl; const char* glsl; };

ShaderSource sourceFor(Pass p) {
    namespace s = effects::shaders;
    switch (p) {
    case Pass::BloomPrefilter: return {s::bloom_prefilter_wgsl, s::bloom_prefilter_frag_glsl};
    case Pass::BloomDown:      return {s::bloom_down_wgsl, s::bloom_down_frag_glsl};
    case Pass::BloomUp:        return {s::bloom_up_wgsl, s::bloom_up_frag_glsl};
    case Pass::BloomComposite: return {s::bloom_composite_wgsl, s::bloom_composite_frag_glsl};
    case Pass::Blur:           return {s::blur_wgsl, s::blur_frag_glsl};
    case Pass::Vignette:       return {s::vignette_wgsl, s::vignette_frag_glsl};
    case Pass::Crt:            return {s::crt_wgsl, s::crt_frag_glsl};
    case Pass::ColorGrading:   return {s::color_grading_wgsl, s::color_grading_frag_glsl};
    case Pass::Pixelate:       return {s::pixelate_wgsl, s::pixelate_frag_glsl};
    case Pass::Tone:           return {s::tone_wgsl, s::tone_frag_glsl};
    case Pass::Fade:           return {s::fade_wgsl, s::fade_frag_glsl};
    case Pass::Count:          break;
    }
    return {"", ""};
}

Vec4 v4(ColorF c, float scale = 1.f) { return {c.r * scale, c.g * scale, c.b * scale, c.a}; }

template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

} // namespace

struct PostEffects::Impl {
    RendererResource& r;

    struct Entry {
        int32_t id = 0;
        int32_t order = 0;
        bool enabled = true;
        int32_t rendererId = 0;
        PostEffectParams params;

        // Intermediate targets (bloom levels, blur scratch).
        std::vector<TextureHandle> targets;
        int32_t targetW = 0, targetH = 0;

        // ScreenFade tween.
        bool tweening = false;
        float from = 0.f, to = 0.f, elapsed = 0.f, duration = 0.f;
    };

    std::map<int32_t, std::unique_ptr<Entry>> entries;
    int32_t nextId = 1;
    MaterialHandle materials[static_cast<int>(Pass::Count)];
    bool failed[static_cast<int>(Pass::Count)] = {};

    explicit Impl(RendererResource& renderer) : r(renderer) {}

    MaterialHandle material(Pass p) {
        int i = static_cast<int>(p);
        if (materials[i].valid() || failed[i]) return materials[i];
        ShaderSource src = sourceFor(p);
        ShaderDesc d;
        d.wgsl = src.wgsl;
        d.glslFragment = src.glsl;
        ShaderHandle sh = r.createShader(d);
        if (!sh.valid()) {
            failed[i] = true;
            return {};
        }
        materials[i] = r.createMaterial(sh);
        return materials[i];
    }

    Entry* find(int32_t id) {
        auto it = entries.find(id);
        return it == entries.end() ? nullptr : it->second.get();
    }

    void attach(Entry& e) {
        if (e.rendererId || !e.enabled) return;
        Entry* ep = &e;
        e.rendererId = r.addPostEffect(e.order, [this, ep](RendererResource&, TextureHandle src,
                                                           const ViewInfo& out) {
            run(*ep, src, out);
        });
    }

    void detach(Entry& e) {
        if (!e.rendererId) return;
        r.removePostEffect(e.rendererId);
        e.rendererId = 0;
    }

    void releaseTargets(Entry& e) {
        for (TextureHandle t : e.targets) r.destroyTexture(t);
        e.targets.clear();
        e.targetW = e.targetH = 0;
    }

    // One shader over the whole scene into the output.
    void fullscreen(TextureHandle src, const ViewInfo& out, MaterialHandle m) {
        r.beginPass(out.target);
        r.drawFullscreen(src, BlendMode::Opaque, m);
        r.endPass();
    }

    void run(Entry& e, TextureHandle src, const ViewInfo& out) {
        std::visit(Overloaded{
            [&](const Bloom& b) { runBloom(e, b, src, out); },
            [&](const Blur& b) { runBlur(e, b, src, out); },
            [&](const ColorGrading& c) {
                MaterialHandle m = material(Pass::ColorGrading);
                r.setMaterialParam(m, 0, {c.brightness, c.contrast, c.saturation, c.lutStrength});
                r.setMaterialParam(m, 1, v4(c.tint));
                float lutSize = c.lut.valid() ? r.textureSize(c.lut).y : 0.f;
                r.setMaterialParam(m, 2, {lutSize, 0.f, 0.f, 0.f});
                r.setMaterialTexture(m, 1, lutSize > 1.f ? c.lut : r.whiteTexture());
                fullscreen(src, out, m);
            },
            [&](const Pixelate& p) {
                MaterialHandle m = material(Pass::Pixelate);
                r.setMaterialParam(m, 0, {p.pixelSize, 0.f, 0.f, 0.f});
                fullscreen(src, out, m);
            },
            [&](const Grayscale& g) {
                MaterialHandle m = material(Pass::Tone);
                r.setMaterialParam(m, 0, {std::clamp(g.amount, 0.f, 1.f), 0.f, 0.f, 0.f});
                fullscreen(src, out, m);
            },
            [&](const Sepia& s) {
                MaterialHandle m = material(Pass::Tone);
                r.setMaterialParam(m, 0, {0.f, std::clamp(s.amount, 0.f, 1.f), 0.f, 0.f});
                fullscreen(src, out, m);
            },
            [&](const Vignette& v) {
                MaterialHandle m = material(Pass::Vignette);
                r.setMaterialParam(m, 0, {std::clamp(v.intensity, 0.f, 1.f), v.radius,
                                          std::max(v.softness, 0.0001f), 0.f});
                r.setMaterialParam(m, 1, v4(v.color));
                fullscreen(src, out, m);
            },
            [&](const Crt& c) {
                MaterialHandle m = material(Pass::Crt);
                r.setMaterialParam(m, 0, {c.curvature, std::clamp(c.scanlines, 0.f, 1.f),
                                          c.scanlineSpacing, c.chromaticAberration});
                r.setMaterialParam(m, 1, {std::clamp(c.vignette, 0.f, 1.f), 0.f, 0.f, 0.f});
                fullscreen(src, out, m);
            },
            [&](const ScreenFade& f) {
                MaterialHandle m = material(Pass::Fade);
                r.setMaterialParam(m, 0, v4(f.color));
                r.setMaterialParam(m, 1, {f.amount, 0.f, 0.f, 0.f});
                fullscreen(src, out, m);
            },
        }, e.params);
    }

    // Bright pass at half resolution, a chain of smaller levels, then each
    // level upsampled and added onto the next larger one; the largest level
    // is added over the scene.
    void runBloom(Entry& e, const Bloom& b, TextureHandle src, const ViewInfo& out) {
        int levels = std::clamp(b.levels, 1, 8);
        int32_t w = std::max(1, static_cast<int32_t>(out.targetSize.x) / 2);
        int32_t h = std::max(1, static_cast<int32_t>(out.targetSize.y) / 2);
        if (e.targetW != w || e.targetH != h || static_cast<int>(e.targets.size()) != levels) {
            releaseTargets(e);
            for (int i = 0; i < levels; ++i) {
                e.targets.push_back(r.createRenderTarget(std::max(1, w >> i), std::max(1, h >> i),
                                                         {TextureFilter::Linear}));
            }
            e.targetW = w;
            e.targetH = h;
        }
        MaterialHandle pre = material(Pass::BloomPrefilter);
        MaterialHandle down = material(Pass::BloomDown);
        MaterialHandle up = material(Pass::BloomUp);
        MaterialHandle comp = material(Pass::BloomComposite);
        if (!pre.valid() || !down.valid() || !up.valid() || !comp.valid()) {
            fullscreen(src, out, {});
            return;
        }

        r.setMaterialParam(pre, 0, {std::max(b.threshold, 0.f), std::clamp(b.softKnee, 0.f, 1.f), 0.f, 0.f});
        r.beginPass(e.targets[0]);
        r.drawFullscreen(src, BlendMode::Opaque, pre);
        r.endPass();

        r.setMaterialParam(down, 0, {std::max(b.radius, 0.f), 0.f, 0.f, 0.f});
        for (int i = 1; i < levels; ++i) {
            r.beginPass(e.targets[i]);
            r.drawFullscreen(e.targets[i - 1], BlendMode::Opaque, down);
            r.endPass();
        }
        r.setMaterialParam(up, 0, {std::max(b.radius, 0.f), 0.f, 0.f, 0.f});
        for (int i = levels - 1; i > 0; --i) {
            r.beginPass(e.targets[i - 1]);
            r.drawFullscreen(e.targets[i], BlendMode::Additive, up);
            r.endPass();
        }

        // The upsample chain sums every level; scale back so `intensity`
        // means about the same at any level count.
        r.setMaterialParam(comp, 0, v4(b.tint, b.intensity / std::sqrt(static_cast<float>(levels))));
        r.setMaterialTexture(comp, 1, e.targets[0]);
        fullscreen(src, out, comp);
    }

    void runBlur(Entry& e, const Blur& b, TextureHandle src, const ViewInfo& out) {
        int32_t w = static_cast<int32_t>(out.targetSize.x);
        int32_t h = static_cast<int32_t>(out.targetSize.y);
        if (e.targetW != w || e.targetH != h || e.targets.empty()) {
            releaseTargets(e);
            e.targets.push_back(r.createRenderTarget(std::max(1, w), std::max(1, h), {TextureFilter::Linear}));
            e.targetW = w;
            e.targetH = h;
        }
        MaterialHandle m = material(Pass::Blur);
        if (!m.valid()) {
            fullscreen(src, out, {});
            return;
        }
        int iterations = std::clamp(b.iterations, 1, 8);
        TextureHandle input = src;
        for (int i = 0; i < iterations; ++i) {
            r.setMaterialParam(m, 0, {b.radius, 0.f, 0.f, 0.f});
            r.beginPass(e.targets[0]);
            r.drawFullscreen(input, BlendMode::Opaque, m);
            r.endPass();
            r.setMaterialParam(m, 0, {0.f, b.radius, 0.f, 0.f});
            fullscreen(e.targets[0], out, m);
            input = out.target;
        }
    }
};

PostEffects::PostEffects(RendererResource& renderer) : impl_(new Impl(renderer)) {}

// The renderer frees every texture and shader it owns on shutdown, and may
// already be gone here, so nothing is released through it.
PostEffects::~PostEffects() { delete impl_; }

EffectId PostEffects::addImpl(PostEffectParams params, int32_t order) {
    auto e = std::make_unique<Impl::Entry>();
    e->id = impl_->nextId++;
    e->order = order;
    e->params = std::move(params);
    Impl::Entry& ref = *e;
    impl_->entries.emplace(e->id, std::move(e));
    impl_->attach(ref);
    return {ref.id};
}

PostEffectParams* PostEffects::paramsImpl(EffectId id) {
    Impl::Entry* e = impl_->find(id.id);
    return e ? &e->params : nullptr;
}

void PostEffects::remove(EffectId id) {
    Impl::Entry* e = impl_->find(id.id);
    if (!e) return;
    impl_->detach(*e);
    impl_->releaseTargets(*e);
    impl_->entries.erase(id.id);
}

void PostEffects::setEnabled(EffectId id, bool enabled) {
    Impl::Entry* e = impl_->find(id.id);
    if (!e || e->enabled == enabled) return;
    e->enabled = enabled;
    if (enabled) impl_->attach(*e);
    else impl_->detach(*e);
}

bool PostEffects::enabled(EffectId id) const {
    Impl::Entry* e = impl_->find(id.id);
    return e && e->enabled;
}

void PostEffects::setOrder(EffectId id, int32_t order) {
    Impl::Entry* e = impl_->find(id.id);
    if (!e || e->order == order) return;
    e->order = order;
    if (e->rendererId) {
        impl_->detach(*e);
        impl_->attach(*e);
    }
}

void PostEffects::fadeTo(EffectId fade, float target, float seconds) {
    Impl::Entry* e = impl_->find(fade.id);
    ScreenFade* f = e ? std::get_if<ScreenFade>(&e->params) : nullptr;
    if (!f) return;
    if (seconds <= 0.f) {
        f->amount = target;
        e->tweening = false;
        return;
    }
    e->tweening = true;
    e->from = f->amount;
    e->to = target;
    e->elapsed = 0.f;
    e->duration = seconds;
}

void PostEffects::flash(EffectId fade, ColorF color, float seconds) {
    Impl::Entry* e = impl_->find(fade.id);
    ScreenFade* f = e ? std::get_if<ScreenFade>(&e->params) : nullptr;
    if (!f) return;
    f->color = color;
    f->amount = 1.f;
    fadeTo(fade, 0.f, seconds);
}

void PostEffects::tick(float dt) {
    for (auto& [id, e] : impl_->entries) {
        if (!e->tweening) continue;
        ScreenFade* f = std::get_if<ScreenFade>(&e->params);
        if (!f) {
            e->tweening = false;
            continue;
        }
        e->elapsed += dt;
        float t = std::min(e->elapsed / e->duration, 1.f);
        f->amount = e->from + (e->to - e->from) * t;
        if (t >= 1.f) e->tweening = false;
    }
}

TextureHandle PostEffects::createColorLut(RendererResource& renderer, int size,
                                          const std::function<ColorF(ColorF)>& fn) {
    if (size < 2 || size > 64 || !fn) return {};
    int w = size * size, h = size;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    float inv = 1.f / static_cast<float>(size - 1);
    auto byte = [](float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
    for (int g = 0; g < size; ++g)
        for (int b = 0; b < size; ++b)
            for (int r = 0; r < size; ++r) {
                ColorF c = fn({r * inv, g * inv, b * inv, 1.f});
                uint8_t* p = &px[(static_cast<size_t>(g) * w + b * size + r) * 4];
                p[0] = byte(c.r); p[1] = byte(c.g); p[2] = byte(c.b); p[3] = 255;
            }
    return renderer.createTextureFromPixels(px.data(), w, h, {TextureFilter::Linear});
}

} // namespace drift
