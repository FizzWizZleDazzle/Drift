#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <drift/resources/FontResource.h>
#include <drift/resources/RendererResource.h>
#include <drift/Log.h>
#include "core/HandlePool.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace drift {

static constexpr int GLYPH_FIRST = 32;
static constexpr int GLYPH_LAST  = 126;
static constexpr int GLYPH_COUNT = GLYPH_LAST - GLYPH_FIRST + 1;

struct FontData {
    stbtt_fontinfo info;
    std::vector<uint8_t> ttfBuffer;
    stbtt_bakedchar glyphs[GLYPH_COUNT];
    TextureHandle atlasTexture;
    int atlasW;
    int atlasH;
    float sizePx;
    float lineHeight;
    float ascent;
};

struct FontResource::Impl {
    RendererResource& renderer;
    HandlePool<FontTag, FontData> fonts;

    Impl(RendererResource& r) : renderer(r) {}
};

FontResource::FontResource(RendererResource& renderer)
    : impl_(new Impl(renderer)) {}

FontResource::~FontResource() { delete impl_; }

static std::vector<uint8_t> readFile(const char* path) {
    std::vector<uint8_t> buf;
    FILE* f = std::fopen(path, "rb");
    if (!f) return buf;
    std::fseek(f, 0, SEEK_END);
    long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len <= 0) { std::fclose(f); return buf; }
    buf.resize(static_cast<size_t>(len));
    std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    return buf;
}

FontHandle FontResource::loadFont(const char* path, float sizePx) {
    if (!path || sizePx <= 0.f) return FontHandle{};

    auto ttf = readFile(path);
    if (ttf.empty()) return FontHandle{};

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
        DRIFT_LOG_ERROR("stbtt_InitFont failed for '%s'", path);
        return FontHandle{};
    }

    stbtt_bakedchar bakedChars[GLYPH_COUNT];
    int atlasW = 256, atlasH = 256;
    std::vector<uint8_t> alpha;
    int bakeResult = -1;

    for (int attempt = 0; attempt < 6; ++attempt) {
        alpha.resize(static_cast<size_t>(atlasW) * atlasH);
        std::memset(alpha.data(), 0, alpha.size());
        bakeResult = stbtt_BakeFontBitmap(ttf.data(), 0, sizePx,
            alpha.data(), atlasW, atlasH, GLYPH_FIRST, GLYPH_COUNT, bakedChars);
        if (bakeResult > 0) break;
        atlasW *= 2;
        atlasH *= 2;
        if (atlasW > 4096) return FontHandle{};
    }
    if (bakeResult <= 0) return FontHandle{};

    // Convert alpha to RGBA
    size_t pixelCount = static_cast<size_t>(atlasW) * atlasH;
    std::vector<uint8_t> rgba(pixelCount * 4);
    for (size_t i = 0; i < pixelCount; ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = alpha[i];
    }

    TextureHandle atlasTex = impl_->renderer.createTextureFromPixels(rgba.data(), atlasW, atlasH);
    if (!atlasTex.valid()) return FontHandle{};

    float scale = stbtt_ScaleForPixelHeight(&info, sizePx);
    int ascentI = 0, descentI = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascentI, &descentI, &lineGap);
    float ascent = static_cast<float>(ascentI) * scale;
    float descent = static_cast<float>(descentI) * scale;
    float lineHeight = ascent - descent + static_cast<float>(lineGap) * scale;

    FontData data;
    data.info = info;
    data.ttfBuffer = std::move(ttf);
    data.info.data = data.ttfBuffer.data();
    std::memcpy(data.glyphs, bakedChars, sizeof(bakedChars));
    data.atlasTexture = atlasTex;
    data.atlasW = atlasW;
    data.atlasH = atlasH;
    data.sizePx = sizePx;
    data.lineHeight = lineHeight;
    data.ascent = ascent;

    return impl_->fonts.create(std::move(data));
}

void FontResource::destroyFont(FontHandle font) {
    auto* fd = impl_->fonts.get(font);
    if (fd && fd->atlasTexture.valid()) {
        impl_->renderer.destroyTexture(fd->atlasTexture);
    }
    impl_->fonts.destroy(font);
}

void FontResource::drawText(FontHandle font, const char* text, Vec2 position,
                            Color color, float scale) {
    if (!text || *text == '\0') return;
    auto* fd = impl_->fonts.get(font);
    if (!fd) return;

    float cursorX = position.x;
    float cursorY = position.y + fd->ascent * scale;

    for (const char* p = text; *p; ++p) {
        unsigned char ch = static_cast<unsigned char>(*p);
        if (ch == '\n') {
            cursorX = position.x;
            cursorY += fd->lineHeight * scale;
            continue;
        }
        if (ch < GLYPH_FIRST || ch > GLYPH_LAST) continue;

        int gi = ch - GLYPH_FIRST;
        const stbtt_bakedchar& bc = fd->glyphs[gi];

        float x0 = cursorX + bc.xoff * scale;
        float y0 = cursorY + bc.yoff * scale;
        float glyphW = static_cast<float>(bc.x1 - bc.x0);
        float glyphH = static_cast<float>(bc.y1 - bc.y0);

        Rect srcRect{static_cast<float>(bc.x0), static_cast<float>(bc.y0), glyphW, glyphH};

        impl_->renderer.drawSprite(
            fd->atlasTexture, {x0, y0}, srcRect,
            {scale, scale}, 0.f, {}, color, Flip::None, 1000.f);

        cursorX += bc.xadvance * scale;
    }
}

Vec2 FontResource::measureText(FontHandle font, const char* text, float scale) const {
    if (!text || *text == '\0') return {};
    auto* fd = impl_->fonts.get(font);
    if (!fd) return {};

    float cursorX = 0.f, maxWidth = 0.f;
    int lineCount = 1;

    for (const char* p = text; *p; ++p) {
        unsigned char ch = static_cast<unsigned char>(*p);
        if (ch == '\n') {
            if (cursorX > maxWidth) maxWidth = cursorX;
            cursorX = 0.f;
            ++lineCount;
            continue;
        }
        if (ch < GLYPH_FIRST || ch > GLYPH_LAST) continue;
        cursorX += fd->glyphs[ch - GLYPH_FIRST].xadvance;
    }
    if (cursorX > maxWidth) maxWidth = cursorX;

    return {maxWidth * scale, static_cast<float>(lineCount) * fd->lineHeight * scale};
}

} // namespace drift
