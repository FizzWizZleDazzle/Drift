#pragma once

#include <drift/Handle.hpp>
#include <drift/Resource.hpp>
#include <drift/Types.hpp>

#include <cstdint>
#include <functional>
#include <variant>

namespace drift {

class RendererResource;

// ---- Effect parameters ------------------------------------------------------
// Each struct is one ready-made post effect. Pass it to PostEffects::add;
// change its fields at runtime through PostEffects::get<T>(id).
// `defaultOrder` places it in a sensible chain; add() can override it.

// Bright areas bleed light into their surroundings.
struct Bloom {
    static constexpr int32_t defaultOrder = 100;
    float  threshold = 0.7f;     // brightness where bloom starts, 0..1
    float  softKnee = 0.5f;      // 0 = hard cutoff, 1 = gradual
    float  intensity = 1.f;
    float  radius = 1.f;         // spread of each blur step
    int    levels = 5;           // downsampled steps, 1..8; more = wider glow
    ColorF tint = {1.f, 1.f, 1.f, 1.f};
};

// Brightness, contrast, saturation, tint, and an optional color lookup
// table. The LUT is a strip of N slices of N x N (N*N wide, N high): red
// along x inside a slice, green along y, blue picks the slice.
struct ColorGrading {
    static constexpr int32_t defaultOrder = 200;
    float         brightness = 1.f;   // multiplier
    float         contrast = 1.f;     // 1 = unchanged, around mid gray
    float         saturation = 1.f;   // 0 = gray, 1 = unchanged, >1 = vivid
    ColorF        tint = {1.f, 1.f, 1.f, 1.f};
    TextureHandle lut;                // optional; see createColorLut
    float         lutStrength = 1.f;
};

// Gaussian blur of the whole scene.
struct Blur {
    static constexpr int32_t defaultOrder = 300;
    float radius = 1.f;          // spread multiplier
    int   iterations = 2;        // horizontal + vertical passes, 1..8
};

// Large square pixels.
struct Pixelate {
    static constexpr int32_t defaultOrder = 400;
    float pixelSize = 6.f;       // in scene pixels
};

struct Grayscale {
    static constexpr int32_t defaultOrder = 500;
    float amount = 1.f;          // 0..1
};

struct Sepia {
    static constexpr int32_t defaultOrder = 500;
    float amount = 1.f;          // 0..1
};

// Darkens (or colors) the screen edges.
struct Vignette {
    static constexpr int32_t defaultOrder = 600;
    float  intensity = 0.6f;     // 0..1
    float  radius = 0.75f;       // distance from the center where darkening is full (0.5 = edge midpoints)
    float  softness = 0.45f;     // darkening starts this far inside radius
    ColorF color = {0.f, 0.f, 0.f, 1.f};
};

// Old monitor: curved screen, scanlines and color fringes.
struct Crt {
    static constexpr int32_t defaultOrder = 700;
    float curvature = 0.15f;           // 0 = flat
    float scanlines = 0.35f;           // scanline darkness, 0..1
    float scanlineSpacing = 3.f;       // scene pixels per scanline
    float chromaticAberration = 1.5f;  // channel offset in scene pixels at the edges
    float vignette = 0.3f;             // edge darkening, 0..1
};

// Blends the screen toward a color. amount 1 = solid color. Animate it with
// PostEffects::fadeTo and PostEffects::flash, or set it yourself.
struct ScreenFade {
    static constexpr int32_t defaultOrder = 900;
    ColorF color = {0.f, 0.f, 0.f, 1.f};
    float  amount = 0.f;
};

using PostEffectParams = std::variant<Bloom, ColorGrading, Blur, Pixelate, Grayscale, Sepia,
                                      Vignette, Crt, ScreenFade>;

struct EffectId {
    int32_t id = 0;
    bool valid() const { return id != 0; }
    bool operator==(const EffectId&) const = default;
};

// ---- Library ----------------------------------------------------------------
// Owns the effects and runs them as renderer post effects, after every
// camera and before ScreenLayer. Added by PostEffectsPlugin.
//
//   EffectId bloom = effects->add(Bloom{.threshold = 0.6f});
//   effects->get<Bloom>(bloom)->intensity = 2.f;
//   effects->setEnabled(bloom, false);
class PostEffects : public Resource {
public:
    explicit PostEffects(RendererResource& renderer);
    ~PostEffects() override;

    DRIFT_RESOURCE(PostEffects)
    bool mainThreadOnly() const override { return true; }

    template <typename T>
    EffectId add(T params, int32_t order = T::defaultOrder) {
        return addImpl(PostEffectParams{std::move(params)}, order);
    }

    // Live parameters; nullptr if `id` is gone or holds another effect type.
    template <typename T>
    T* get(EffectId id) {
        PostEffectParams* p = paramsImpl(id);
        return p ? std::get_if<T>(p) : nullptr;
    }

    void remove(EffectId id);
    void setEnabled(EffectId id, bool enabled);
    bool enabled(EffectId id) const;
    void setOrder(EffectId id, int32_t order);

    // ScreenFade helpers, advanced every frame by PostEffectsPlugin.
    // fadeTo moves `amount` to `target` over `seconds`; flash jumps to full
    // `color` and fades back to 0.
    void fadeTo(EffectId fade, float target, float seconds);
    void flash(EffectId fade, ColorF color, float seconds);
    void tick(float dt);

    // Builds a size*size by size LUT texture (see ColorGrading) where each
    // entry is fn(input color). size 16 or 32 is typical.
    static TextureHandle createColorLut(RendererResource& renderer, int size,
                                        const std::function<ColorF(ColorF)>& fn);

private:
    EffectId addImpl(PostEffectParams params, int32_t order);
    PostEffectParams* paramsImpl(EffectId id);

    struct Impl;
    Impl* impl_;
};

} // namespace drift
