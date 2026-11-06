# Post Effect Library

Ready-made full-screen effects: bloom, color grading, blur, pixelate, grayscale, sepia, vignette, CRT, and screen fades and flashes. Each one is enabled with a single call and tuned at runtime. To write your own effect instead, see [Shaders and Effects](shaders-and-effects.md).

**Header:** `#include <drift/effects/PostEffects.hpp>`

The `PostEffects` resource comes with `DefaultPlugins` (through `PostEffectsPlugin`). An effect costs nothing until you add it.

## Using an effect

```cpp
void setup(ResMut<PostEffects> fx, ResMut<Game> game) {
    game->bloom = fx->add(Bloom{.threshold = 0.6f, .intensity = 1.5f});
    game->vignette = fx->add(Vignette{});
}

void update(ResMut<PostEffects> fx, Res<Game> game, Res<InputResource> input) {
    // Change parameters at any time; the next frame uses them.
    fx->get<Bloom>(game->bloom)->intensity = 2.f;

    if (input->keyPressed(Key::V))
        fx->setEnabled(game->vignette, !fx->enabled(game->vignette));
}
```

| Call | Description |
|------|-------------|
| `EffectId add(T params, int32_t order = T::defaultOrder)` | Adds and enables an effect. |
| `T* get<T>(EffectId)` | Live parameters. `nullptr` if the id was removed or holds another effect type. |
| `setEnabled(EffectId, bool)` / `enabled(EffectId)` | Turn an effect off and on without losing its parameters. |
| `setOrder(EffectId, int32_t)` | Move an effect in the chain. |
| `remove(EffectId)` | Delete an effect and free its render targets. |

Effects run on the whole scene after every camera has drawn, in ascending order, each reading the previous one's result. The defaults give a sensible chain: bloom, color grading, blur, pixelate, grayscale and sepia, vignette, CRT, then screen fade. The HUD on `ScreenLayer` is drawn after all of them and stays unaffected.

Library effects share the chain with effects added through `RendererResource::addPostEffect`, ordered by the same numbers.

## Bloom

Bright parts of the image bleed light into their surroundings. The bright areas are extracted at half resolution, blurred through a chain of smaller targets, and added back over the scene.

| Field | Default | Description |
|-------|---------|-------------|
| `threshold` | `0.7` | Brightness (0..1) where bloom starts. |
| `softKnee` | `0.5` | `0` gives a hard cutoff at the threshold; `1` eases in. |
| `intensity` | `1.0` | Strength of the added glow. |
| `radius` | `1.0` | Spread of each blur step. |
| `levels` | `5` | Number of downsampled steps (1..8). More levels give a wider glow. |
| `tint` | white | Color of the glow. |

Bloom works on the final, clamped scene colors, so it responds to what looks bright on screen. Additive sprites over a lit scene are a good source.

## ColorGrading

| Field | Default | Description |
|-------|---------|-------------|
| `brightness` | `1.0` | Multiplies every color. |
| `contrast` | `1.0` | Pushes colors away from (above 1) or toward (below 1) mid gray. |
| `saturation` | `1.0` | `0` is gray, above 1 is more vivid. |
| `tint` | white | Multiplies every color. |
| `lut` | none | Optional color lookup table texture. |
| `lutStrength` | `1.0` | Blend between the graded colors and the LUT result. |

The lookup table is a strip of N slices of N x N pixels (N*N wide, N high). Inside a slice, red increases left to right and green top to bottom; blue picks the slice. Load one with linear filtering, or build one in code:

```cpp
TextureHandle warm = PostEffects::createColorLut(*renderer, 16, [](ColorF c) {
    return ColorF{std::pow(c.r, 0.8f), c.g, std::pow(c.b, 1.3f) * 0.85f, 1.f};
});
fx->add(ColorGrading{.saturation = 1.2f, .lut = warm});
```

The LUT is applied after brightness, contrast, saturation and tint.

## Blur

Gaussian blur of the whole scene, for pause menus and out-of-focus moments.

| Field | Default | Description |
|-------|---------|-------------|
| `radius` | `1.0` | Spread of each pass. |
| `iterations` | `2` | Horizontal plus vertical passes (1..8). More iterations blur further. |

## Pixelate

| Field | Default | Description |
|-------|---------|-------------|
| `pixelSize` | `6` | Size of each block in scene pixels. |

## Grayscale and Sepia

Two separate effects with one field each: `amount`, from `0` (unchanged) to `1` (full effect). Animate `amount` to drain or restore color.

## Vignette

Darkens or colors the edges of the screen.

| Field | Default | Description |
|-------|---------|-------------|
| `intensity` | `0.6` | How strong the edge color gets (0..1). |
| `radius` | `0.75` | Distance from the center where the effect is full. `0.5` is the middle of each edge. |
| `softness` | `0.45` | The effect starts this far inside `radius`. |
| `color` | black | Edge color. Red works as a damage indicator. |

## Crt

An old monitor: curved glass, scanlines and color fringes at the edges.

| Field | Default | Description |
|-------|---------|-------------|
| `curvature` | `0.15` | Barrel distortion. `0` is flat. Corners outside the curved screen are black. |
| `scanlines` | `0.35` | Darkness of the scanlines (0..1). |
| `scanlineSpacing` | `3` | Scene pixels per scanline. |
| `chromaticAberration` | `1.5` | Red and blue offset at the screen edges, in scene pixels. |
| `vignette` | `0.3` | Edge darkening (0..1). |

## ScreenFade

Blends the screen toward a color. `amount` `0` leaves the scene unchanged, `1` is the solid color. Set it yourself or let the library animate it:

```cpp
EffectId fade = fx->add(ScreenFade{});

fx->fadeTo(fade, 1.f, 0.8f);                     // fade to black over 0.8 s
fx->fadeTo(fade, 0.f, 0.8f);                     // and back
fx->flash(fade, {1.f, 1.f, 1.f, 1.f}, 0.3f);     // white hit flash, fades out over 0.3 s
```

| Field | Default | Description |
|-------|---------|-------------|
| `color` | black | Target color. `flash` sets it. |
| `amount` | `0` | Blend toward `color` (0..1). |

`ScreenFade` runs before the HUD, so menus and text on `ScreenLayer` stay visible during a fade.

## Backends

Every effect has a GLSL version for the OpenGL backend and a WGSL version for WebGPU.
