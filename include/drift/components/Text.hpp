#pragma once

#include <drift/Types.hpp>
#include <drift/Handle.hpp>
#include <drift/resources/FontResource.hpp>

#include <cstring>
#include <string_view>

namespace drift {

// Text drawn at the entity's transform, like Sprite. The string lives inside
// the component (up to Capacity - 1 bytes of UTF-8); for longer text call
// FontResource::drawText from a render system.
struct Text {
    static constexpr size_t Capacity = 512;

    char       text[Capacity] = {};
    FontHandle font;                         // empty = the built-in default font
    float      size = 0.f;                   // pixel height; 0 = the font's load size
    Color      color = Color::white();
    TextAlign  align = TextAlign::Left;
    TextVAlign valign = TextVAlign::Top;
    float      wrapWidth = 0.f;              // 0 = no wrapping
    float      lineSpacing = 1.f;
    float      outlineWidth = 0.f;           // 0 = no outline
    Color      outlineColor = Color::black();
    Vec2       shadowOffset = {};            // {0,0} = no shadow
    Color      shadowColor = {0, 0, 0, 160};
    bool       richText = false;
    float      zOrder = 0.f;
    uint8_t    layer = 0;                    // 0..30 world layers, ScreenLayer for HUD text
    bool       visible = true;

    // Replaces the string; longer input is cut at a UTF-8 character boundary.
    void set(std::string_view s) {
        size_t n = s.size() < Capacity - 1 ? s.size() : Capacity - 1;
        if (n < s.size()) {
            while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
        }
        std::memcpy(text, s.data(), n);
        text[n] = '\0';
    }
    std::string_view str() const { return {text, std::strlen(text)}; }

    TextStyle style() const {
        TextStyle s;
        s.size = size;
        s.color = color;
        s.align = align;
        s.valign = valign;
        s.wrapWidth = wrapWidth;
        s.lineSpacing = lineSpacing;
        s.outlineWidth = outlineWidth;
        s.outlineColor = outlineColor;
        s.shadowOffset = shadowOffset;
        s.shadowColor = shadowColor;
        s.richText = richText;
        s.zOrder = zOrder;
        s.layer = layer;
        return s;
    }
};

} // namespace drift
