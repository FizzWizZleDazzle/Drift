#pragma once

#include <drift/Resource.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace drift {

class RendererResource;

enum class TextAlign : uint8_t { Left = 0, Center, Right };
enum class TextVAlign : uint8_t { Top = 0, Middle, Bottom };

// How a font is loaded.
struct FontOptions {
    // Atlas sampling. Unset: Linear (smooth) for TrueType, Nearest (crisp)
    // for bitmap fonts. Nearest also rasterizes TrueType glyphs at exactly
    // the requested size, for pixel fonts shipped as .ttf.
    std::optional<TextureFilter> filter;
};

// How a piece of text is laid out and drawn. Every field has a default.
struct TextStyle {
    float      size = 0.f;                   // pixel height; 0 = the font's load size
    Color      color = Color::white();
    TextAlign  align = TextAlign::Left;      // where position.x sits on each line
    TextVAlign valign = TextVAlign::Top;     // where position.y sits on the block
    float      wrapWidth = 0.f;              // wrap lines at this width; 0 = no wrapping
    float      lineSpacing = 1.f;            // multiplier on the font's line height
    float      outlineWidth = 0.f;           // pixels; 0 = no outline
    Color      outlineColor = Color::black();
    Vec2       shadowOffset = {};            // pixels; {0,0} = no shadow
    Color      shadowColor = {0, 0, 0, 160};
    bool       richText = false;             // parse [color=#rrggbb]...[/color] tags
    float      zOrder = 0.f;
    std::optional<uint8_t> layer;            // unset = the renderer's current layer
};

class FontResource : public Resource {
public:
    explicit FontResource(RendererResource& renderer);
    ~FontResource() override;

    DRIFT_RESOURCE(FontResource)
    bool mainThreadOnly() const override { return true; }

    // Loads a TrueType/OpenType file (.ttf, .otf) or an AngelCode BMFont
    // text file (.fnt, with its page PNGs next to it). sizePx is the size
    // drawText uses when TextStyle::size is 0; for bitmap fonts 0 means the
    // size the font was made at.
    FontHandle loadFont(const char* path, float sizePx, FontOptions options = {});
    // TrueType/OpenType data already in memory. The bytes are copied.
    FontHandle loadFontFromMemory(const uint8_t* data, size_t size, float sizePx,
                                  FontOptions options = {});
    void destroyFont(FontHandle font);

    // The font built into the engine (Noto Sans: Latin, Greek, Cyrillic).
    // An empty FontHandle passed to drawText or measureText means this font.
    FontHandle defaultFont() const;

    // Characters a font lacks are drawn from its fallback. Every font falls
    // back to the default font until you change it; pass {} for none.
    void setFallbackFont(FontHandle font, FontHandle fallback);

    // Draws UTF-8 text. With the default style, position is the top-left
    // corner of the first line.
    void drawText(FontHandle font, std::string_view text, Vec2 position,
                  const TextStyle& style = {});
    // Same, with rotation and scale around position (as a Text entity draws).
    void drawText(FontHandle font, std::string_view text, const Transform2D& transform,
                  const TextStyle& style = {});

    // Size of the laid-out block (widest line, total height) for the given
    // style; matches what drawText covers, outline and shadow excluded.
    Vec2 measureText(FontHandle font, std::string_view text, const TextStyle& style = {}) const;
    // Distance between baselines at a size (0 = the font's load size).
    float lineHeight(FontHandle font, float size = 0.f) const;

    // Called once per frame by FontPlugin before any text is drawn.
    void beginFrame();

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
