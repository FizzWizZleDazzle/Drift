# Fonts

`FontResource` loads fonts and draws text: UTF-8 strings with kerning, alignment, word wrapping, outlines, drop shadows, and inline color tags. A built-in font makes text work with no asset files, TrueType and OpenType fonts cover smooth text at any size, and AngelCode BMFont files cover pixel-art fonts.

For text that belongs to an entity, use the [Text component](../components/text.md); it draws through this resource.

**Header:** `#include <drift/resources/FontResource.hpp>`

**Plugin:** `FontPlugin` (included in `DefaultPlugins`)

## Quick start

```cpp
void drawHud(ResMut<FontResource> fonts) {
    fonts->drawText({}, "Hello, world", {20.f, 20.f});
}
```

An empty `FontHandle` (`{}`) means the built-in font.

## The built-in font

The engine embeds a subset of [Noto Sans](https://github.com/notofonts/latin-greek-cyrillic) Regular: Basic Latin, Latin-1, Latin Extended-A, Greek, Cyrillic (Russian, Ukrainian, Belarusian, Serbian, Bulgarian), and common punctuation and currency signs. `defaultFont()` returns its handle; its load size is 20 px.

Noto Sans is a clean, highly legible sans-serif, and the subset keeps the engine binary small. It is licensed under the SIL Open Font License 1.1; the license text is `src/font/default/OFL.txt`. If you ship a game built with Drift, ship that license with it.

Every font you load falls back to the built-in font for characters it lacks, so a pixel font with only ASCII still shows accented letters and Cyrillic.

## Loading fonts

```cpp
FontHandle loadFont(const char* path, float sizePx, FontOptions options = {});
FontHandle loadFontFromMemory(const uint8_t* data, size_t size, float sizePx,
                              FontOptions options = {});
void       destroyFont(FontHandle font);
bool       reloadFont(FontHandle font, const char* path);
FontHandle defaultFont() const;
void       setFallbackFont(FontHandle font, FontHandle fallback);
```

`loadFont` reads TrueType and OpenType files (`.ttf`, `.otf`) and AngelCode BMFont files (`.fnt`). `sizePx` is the size `drawText` uses when the style does not set one. A single handle draws at any size: glyphs are rasterized on demand for each size you use, so there is no need to load the same file twice.

```cpp
struct Fonts : public Resource {
    DRIFT_RESOURCE(Fonts)
    FontHandle body;
    FontHandle pixel;
};

void loadFonts(ResMut<FontResource> fonts, ResMut<Fonts> f) {
    f->body  = fonts->loadFont("assets/fonts/roboto.ttf", 24.f);
    f->pixel = fonts->loadFont("assets/fonts/pixel.fnt", 0.f);   // 0 = the size it was made at
}
```

`FontOptions::filter` picks how glyphs are sampled. Left unset, TrueType fonts use `TextureFilter::Linear` (smooth) and bitmap fonts use `TextureFilter::Nearest` (crisp pixels). Setting `Nearest` on a TrueType font also rasterizes it at exactly the requested size, which suits pixel fonts distributed as `.ttf`:

```cpp
fonts->loadFont("assets/fonts/pixel.ttf", 16.f, {.filter = TextureFilter::Nearest});
```

`setFallbackFont(font, fallback)` changes where missing characters come from; pass `{}` to turn fallback off. With no fallback, a missing character draws as the font's missing-glyph box (TrueType) or `?` (bitmap fonts).

Destroyed handles draw nothing. The built-in font cannot be destroyed.

`reloadFont` reads a font again from its file at the size it was loaded with, keeping its handle. [Hot reload](asset-server.md#hot-reload) uses it.

### Bitmap fonts

Drift reads the text variant of the [AngelCode BMFont](https://www.angelcode.com/products/bmfont/doc/file_format.html) format, which BMFont, Hiero, Littera, ShoeBox, and most pixel-font tools export. Page images are loaded from the paths in the `.fnt` file, relative to it. Glyphs are drawn white-tinted, so `color` tints them; colored glyphs keep their colors under a white tint. A grayscale page uses its brightness as coverage. Kerning pairs in the file are applied.

The size of a bitmap font is the `size` in its `info` line. Draw at whole multiples of it (an 8 px font at 16, 24, 32) to keep pixels square. Binary and XML `.fnt` files are not supported; export the text format.

## Drawing text

```cpp
void drawText(FontHandle font, std::string_view text, Vec2 position,
              const TextStyle& style = {});
void drawText(FontHandle font, std::string_view text, const Transform2D& transform,
              const TextStyle& style = {});
```

Text is UTF-8. `\n` starts a new line and `\t` advances four spaces. With the default style, `position` is the top-left corner of the first line. The `Transform2D` overload rotates and scales the text around its position.

Draw text during the Render phase, like any other draw. Text goes on the renderer's current layer (`setLayer`) unless the style names one, so HUD code can set `ScreenLayer` once and draw everything.

### TextStyle

```cpp
struct TextStyle {
    float      size = 0.f;                   // pixel height; 0 = the font's load size
    Color      color = Color::white();
    TextAlign  align = TextAlign::Left;      // Left, Center, Right
    TextVAlign valign = TextVAlign::Top;     // Top, Middle, Bottom
    float      wrapWidth = 0.f;              // 0 = no wrapping
    float      lineSpacing = 1.f;
    float      outlineWidth = 0.f;           // 0 = no outline
    Color      outlineColor = Color::black();
    Vec2       shadowOffset = {};            // {0,0} = no shadow
    Color      shadowColor = {0, 0, 0, 160};
    bool       richText = false;
    float      zOrder = 0.f;
    std::optional<uint8_t> layer;            // unset = the renderer's current layer
};
```

Every field has a default, so designated initializers keep calls short:

```cpp
fonts->drawText({}, "GAME OVER", {640.f, 360.f},
                {.size = 64.f, .color = Color::red(),
                 .align = TextAlign::Center, .valign = TextVAlign::Middle,
                 .outlineWidth = 3.f, .shadowOffset = {4.f, 4.f}});
```

| Field | Description |
|-------|-------------|
| `size` | Height in pixels from the highest ascender to the lowest descender. |
| `align` | Where `position.x` sits on each line: its left end, its center, or its right end. Lines of a wrapped paragraph are aligned individually. |
| `valign` | Where `position.y` sits on the whole block: its top, middle, or bottom. |
| `wrapWidth` | Breaks lines at spaces so no line is wider than this. A word longer than the width is split. Trailing spaces do not count toward a line's width. |
| `lineSpacing` | Multiplies the distance between baselines. |
| `outlineWidth`, `outlineColor` | A border around every glyph, `outlineWidth` pixels thick. |
| `shadowOffset`, `shadowColor` | A copy of the text drawn behind it at this offset. With an outline, the shadow has the outline's shape. |
| `richText` | Parses inline color tags (below). |
| `zOrder`, `layer` | Sorting, as for sprites. The outline and shadow sort just below the text's own `zOrder`. |

The outline and shadow fade with the text: their alpha is multiplied by the alpha of `color`.

### Color tags

With `richText = true`, `[color=#rrggbb]` or `[color=#rrggbbaa]` switches color until the matching `[/color]`. Tags nest. Write `[[` for a literal `[`. Tags take no space in layout or measurement.

```cpp
fonts->drawText({}, "Press [color=#ffd060]E[/color] to open", pos, {.richText = true});
```

## Measuring text

```cpp
Vec2  measureText(FontHandle font, std::string_view text, const TextStyle& style = {}) const;
float lineHeight(FontHandle font, float size = 0.f) const;
```

`measureText` lays the text out exactly as `drawText` would, with the same size, wrapping, and line spacing, and returns the block's width (its widest line) and height. Use it for panels, buttons, and manual layout. Outline and shadow are not included. An empty string measures `{0, 0}`.

`lineHeight` is the distance between baselines at a size with `lineSpacing` 1. A block of `n` lines is `lineHeight + (n - 1) * lineHeight * lineSpacing` tall.

```cpp
TextStyle style{.size = 18.f, .wrapWidth = 300.f};
Vec2 box = fonts->measureText({}, dialog, style);
renderer->drawRoundedRectFilled({x - 8.f, y - 8.f, box.x + 16.f, box.y + 16.f}, 6.f, panelColor);
fonts->drawText({}, dialog, {x, y}, style);
```

## Rendering quality

TrueType glyphs are rasterized for the pixels they cover on screen: the size times the window's scale over the logical resolution, times the default camera's zoom for world layers. Text stays sharp when the window is larger than the logical resolution and when the camera zooms in. Scaling a `Transform2D` stretches the rasterized glyphs instead; for large text, raise `size`. In `ScaleMode::Integer` the scene renders at the logical resolution, so text does too.

Unrotated text is placed on whole pixels. Rotated text is drawn with the same glyphs, filtered.

Glyphs live in shared atlas textures that grow page by page as new characters and sizes appear. If text keeps asking for new sizes (an animated `size`, a slow camera zoom) and the atlas outgrows its budget, it is cleared at the start of the next frame and refilled with what is drawn.

### Outlines

An outline is a second glyph image, the glyph grown by the outline width with a round brush, rasterized once into the atlas and drawn under the text. This was chosen over signed distance fields and over drawing the text several times at offsets:

- Unlike multi-sample offsets, it costs one extra quad per glyph at any width and has no gaps at sharp corners of thick outlines.
- Unlike a signed distance field, it needs no special shader on any backend, keeps small text as sharp as plain rasterization, and works the same for bitmap fonts, whose outline follows their pixels.

Each outline width is its own atlas entry, so animating `outlineWidth` creates many entries.

## Limitations

- No complex-script shaping: Arabic, Hebrew, and Indic scripts do not join or reorder, and there is no right-to-left layout. Ligatures are not applied.
- Characters outside the built-in subset (CJK, emoji) need a font that has them.
- Wrapping breaks only at spaces.
- stb_truetype trusts the font data; load fonts you ship, not untrusted files.

## Complete example

A HUD with a score, a right-aligned timer, and a centered game-over message:

```cpp
struct HUD : public Resource {
    DRIFT_RESOURCE(HUD)
    FontHandle font;
};

void setupHUD(ResMut<FontResource> fonts, ResMut<HUD> hud) {
    hud->font = fonts->loadFont("assets/fonts/monospace.ttf", 20.f);
}

void drawHUD(ResMut<FontResource> fonts, Res<HUD> hud, Res<Time> time,
             Res<GameState> state, ResMut<RendererResource> renderer) {
    float w = renderer->logicalWidth(), h = renderer->logicalHeight();
    renderer->setLayer(ScreenLayer);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "Score: %d", state->score);
    fonts->drawText(hud->font, buf, {10.f, 10.f}, {.shadowOffset = {2.f, 2.f}});

    int seconds = static_cast<int>(time->elapsed);
    std::snprintf(buf, sizeof(buf), "%02d:%02d", seconds / 60, seconds % 60);
    fonts->drawText(hud->font, buf, {w - 10.f, 10.f}, {.align = TextAlign::Right});

    if (state->gameOver) {
        fonts->drawText(hud->font, "GAME OVER", {w * 0.5f, h * 0.5f},
                        {.size = 48.f, .color = Color::red(),
                         .align = TextAlign::Center, .valign = TextVAlign::Middle,
                         .outlineWidth = 3.f});
    }
    renderer->setLayer(0);
}
```

See the [Text example](../examples/text.md) for every feature on one screen.
