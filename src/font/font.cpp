// =============================================================================
// Drift 2D Game Engine - Font Implementation
// =============================================================================
// Implements the public drift/font.h API using stb_truetype for TTF loading
// and bitmap atlas generation.  Text is rendered via drift_sprite_draw using
// glyph quads sourced from the atlas texture.
//
// NOTE: This is a simple bitmap rasterisation approach.  SDF-based rendering
// would improve quality at varying scales and should be added in a later pass.
// =============================================================================

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <drift/font.h>
#include <drift/renderer.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// -----------------------------------------------------------------------------
// Internal texture creation from raw RGBA pixels.
// Defined in renderer.cpp -- allows subsystems to create GPU textures without
// going through the file-based drift_texture_load path.
// -----------------------------------------------------------------------------
extern "C" DriftTexture drift_internal_texture_create_from_pixels(
    const uint8_t* pixels, int w, int h);

// =============================================================================
// Internal types and state
// =============================================================================
namespace {

// ASCII printable range baked into the atlas.
static constexpr int GLYPH_FIRST = 32;
static constexpr int GLYPH_LAST  = 126;
static constexpr int GLYPH_COUNT = GLYPH_LAST - GLYPH_FIRST + 1;

struct FontData {
    stbtt_fontinfo         info;
    std::vector<uint8_t>   ttf_buffer;      // raw TTF file kept alive for stbtt
    stbtt_bakedchar        glyphs[GLYPH_COUNT];
    DriftTexture           atlas_texture;
    int                    atlas_w;
    int                    atlas_h;
    float                  size_px;
    float                  line_height;      // vertical advance between lines
    float                  ascent;           // baseline offset from top
    bool                   alive;
};

static std::vector<FontData> g_fonts;         // index 0 unused (ids start at 1)
static uint32_t              g_next_font_id = 1;

// -----------------------------------------------------------------------------
// Helper: read an entire file into a byte buffer.
// Returns an empty vector on failure.
// -----------------------------------------------------------------------------
static std::vector<uint8_t> read_entire_file(const char* path) {
    std::vector<uint8_t> buf;

    FILE* f = fopen(path, "rb");
    if (!f) {
        SDL_Log("drift_font: failed to open '%s'", path);
        return buf;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) {
        fclose(f);
        SDL_Log("drift_font: file '%s' is empty or unreadable", path);
        return buf;
    }

    buf.resize(static_cast<size_t>(len));
    size_t read = fread(buf.data(), 1, buf.size(), f);
    fclose(f);

    if (read != buf.size()) {
        SDL_Log("drift_font: short read on '%s' (expected %ld, got %zu)",
                path, len, read);
        buf.clear();
    }

    return buf;
}

// -----------------------------------------------------------------------------
// Helper: look up a FontData by handle, returning nullptr on invalid id.
// -----------------------------------------------------------------------------
static FontData* get_font(DriftFont font) {
    uint32_t id = font.id;
    if (id == 0 || id >= g_fonts.size()) return nullptr;
    FontData* fd = &g_fonts[id];
    return fd->alive ? fd : nullptr;
}

} // anonymous namespace

// =============================================================================
// PUBLIC API IMPLEMENTATION
// =============================================================================

DriftFont drift_font_load(const char* path, float size_px) {
    DriftFont result;
    result.id = 0;

    if (!path || size_px <= 0.0f) {
        SDL_Log("drift_font_load: invalid parameters");
        return result;
    }

    // --- Read the TTF file ---------------------------------------------------
    std::vector<uint8_t> ttf = read_entire_file(path);
    if (ttf.empty()) {
        return result;
    }

    // --- Initialise stb_truetype info ----------------------------------------
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
        SDL_Log("drift_font_load: stbtt_InitFont failed for '%s'", path);
        return result;
    }

    // --- Bake the bitmap atlas -----------------------------------------------
    // Start at 256x256 and double until baking succeeds (returns > 0 for all
    // characters).  stbtt_BakeFontBitmap returns negative row count if the
    // bitmap is too small.
    stbtt_bakedchar baked_chars[GLYPH_COUNT];
    int atlas_w = 256;
    int atlas_h = 256;
    std::vector<uint8_t> alpha_bitmap;
    int bake_result = -1;

    for (int attempt = 0; attempt < 6; ++attempt) {
        alpha_bitmap.resize(static_cast<size_t>(atlas_w) * atlas_h);
        memset(alpha_bitmap.data(), 0, alpha_bitmap.size());

        bake_result = stbtt_BakeFontBitmap(
            ttf.data(), 0,
            size_px,
            alpha_bitmap.data(), atlas_w, atlas_h,
            GLYPH_FIRST, GLYPH_COUNT,
            baked_chars);

        if (bake_result > 0) {
            break; // success -- bake_result is the first unused row
        }

        // Bitmap too small; double dimensions.
        atlas_w *= 2;
        atlas_h *= 2;

        if (atlas_w > 4096) {
            SDL_Log("drift_font_load: atlas exceeded 4096 for '%s' at %.1f px",
                    path, size_px);
            return result;
        }
    }

    if (bake_result <= 0) {
        SDL_Log("drift_font_load: failed to bake atlas for '%s'", path);
        return result;
    }

    // --- Convert single-channel alpha bitmap to RGBA -------------------------
    // We produce white text; alpha comes from the rasterised glyph coverage.
    size_t pixel_count = static_cast<size_t>(atlas_w) * atlas_h;
    std::vector<uint8_t> rgba(pixel_count * 4);

    for (size_t i = 0; i < pixel_count; ++i) {
        rgba[i * 4 + 0] = 255;               // R
        rgba[i * 4 + 1] = 255;               // G
        rgba[i * 4 + 2] = 255;               // B
        rgba[i * 4 + 3] = alpha_bitmap[i];    // A from bitmap
    }

    // --- Upload atlas as a GPU texture ---------------------------------------
    DriftTexture atlas_tex = drift_internal_texture_create_from_pixels(
        rgba.data(), atlas_w, atlas_h);

    if (atlas_tex.id == 0) {
        SDL_Log("drift_font_load: failed to create atlas texture for '%s'", path);
        return result;
    }

    // --- Compute font metrics ------------------------------------------------
    float scale = stbtt_ScaleForPixelHeight(&info, size_px);

    int ascent_i  = 0;
    int descent_i = 0;
    int line_gap  = 0;
    stbtt_GetFontVMetrics(&info, &ascent_i, &descent_i, &line_gap);

    float ascent      = static_cast<float>(ascent_i)  * scale;
    float descent     = static_cast<float>(descent_i) * scale;
    float line_height = ascent - descent + static_cast<float>(line_gap) * scale;

    // --- Store FontData ------------------------------------------------------
    uint32_t id = g_next_font_id++;
    if (id >= g_fonts.size()) {
        g_fonts.resize(id + 1);
        // Zero-initialise new slots.
        for (size_t i = g_fonts.size() - 1; i < g_fonts.size(); ++i) {
            g_fonts[i].alive = false;
        }
    }

    FontData& fd        = g_fonts[id];
    fd.info             = info;
    fd.ttf_buffer       = std::move(ttf);
    // Patch the stbtt_fontinfo data pointer to our stored buffer.
    fd.info.data        = fd.ttf_buffer.data();
    memcpy(fd.glyphs, baked_chars, sizeof(baked_chars));
    fd.atlas_texture    = atlas_tex;
    fd.atlas_w          = atlas_w;
    fd.atlas_h          = atlas_h;
    fd.size_px          = size_px;
    fd.line_height      = line_height;
    fd.ascent           = ascent;
    fd.alive            = true;

    result.id = id;
    SDL_Log("drift_font_load: loaded '%s' as id %u (%.0fpx, atlas %dx%d)",
            path, id, size_px, atlas_w, atlas_h);
    return result;
}

void drift_font_destroy(DriftFont font) {
    FontData* fd = get_font(font);
    if (!fd) return;

    // Destroy the atlas texture through the renderer.
    if (fd->atlas_texture.id != 0) {
        drift_texture_destroy(fd->atlas_texture);
    }

    fd->ttf_buffer.clear();
    fd->alive = false;
}

void drift_text_draw(DriftFont font, const char* text, DriftVec2 position,
                     DriftColor color, float scale) {
    if (!text || *text == '\0') return;

    FontData* fd = get_font(font);
    if (!fd) return;

    float cursor_x = position.x;
    float cursor_y = position.y + fd->ascent * scale;

    const float inv_atlas_w = 1.0f / static_cast<float>(fd->atlas_w);
    const float inv_atlas_h = 1.0f / static_cast<float>(fd->atlas_h);
    (void)inv_atlas_w; // used implicitly via stbtt quad
    (void)inv_atlas_h;

    // Iterate the string byte-by-byte.  For characters outside the baked
    // ASCII range we skip them (no glyph data available).
    for (const char* p = text; *p != '\0'; ++p) {
        unsigned char ch = static_cast<unsigned char>(*p);

        // Handle newline.
        if (ch == '\n') {
            cursor_x = position.x;
            cursor_y += fd->line_height * scale;
            continue;
        }

        // Skip characters outside the baked range.
        if (ch < GLYPH_FIRST || ch > GLYPH_LAST) {
            continue;
        }

        int glyph_index = ch - GLYPH_FIRST;
        const stbtt_bakedchar& bc = fd->glyphs[glyph_index];

        // Compute quad position (stbtt reports offsets relative to cursor).
        float x0 = cursor_x + bc.xoff * scale;
        float y0 = cursor_y + bc.yoff * scale;
        float glyph_w = static_cast<float>(bc.x1 - bc.x0);
        float glyph_h = static_cast<float>(bc.y1 - bc.y0);

        // Source rectangle in atlas pixel coordinates (for drift_sprite_draw).
        DriftRect src_rect;
        src_rect.x = static_cast<float>(bc.x0);
        src_rect.y = static_cast<float>(bc.y0);
        src_rect.w = glyph_w;
        src_rect.h = glyph_h;

        // Draw the glyph quad via the sprite batcher.
        DriftVec2 glyph_pos = {x0, y0};
        DriftVec2 glyph_scale = {scale, scale};
        DriftVec2 origin = {0.0f, 0.0f};

        drift_sprite_draw(
            fd->atlas_texture,
            glyph_pos,
            src_rect,
            glyph_scale,
            0.0f,           // rotation
            origin,
            color,
            DRIFT_FLIP_NONE,
            1000.0f          // high z_order so text renders on top
        );

        // Advance cursor.
        cursor_x += bc.xadvance * scale;
    }
}

DriftVec2 drift_text_measure(DriftFont font, const char* text, float scale) {
    DriftVec2 result = {0.0f, 0.0f};

    if (!text || *text == '\0') return result;

    FontData* fd = get_font(font);
    if (!fd) return result;

    float cursor_x = 0.0f;
    float max_width = 0.0f;
    int   line_count = 1;

    for (const char* p = text; *p != '\0'; ++p) {
        unsigned char ch = static_cast<unsigned char>(*p);

        if (ch == '\n') {
            if (cursor_x > max_width) {
                max_width = cursor_x;
            }
            cursor_x = 0.0f;
            ++line_count;
            continue;
        }

        if (ch < GLYPH_FIRST || ch > GLYPH_LAST) {
            continue;
        }

        int glyph_index = ch - GLYPH_FIRST;
        cursor_x += fd->glyphs[glyph_index].xadvance;
    }

    if (cursor_x > max_width) {
        max_width = cursor_x;
    }

    result.x = max_width * scale;
    result.y = static_cast<float>(line_count) * fd->line_height * scale;
    return result;
}

// =============================================================================
// Internal helper: create a texture from raw RGBA pixels
// =============================================================================
// This function is declared as an extern in this file and is expected to be
// defined in renderer.cpp (or a closely related translation unit).  If you are
// linking the engine and see an undefined-reference error for this symbol,
// add the following implementation to renderer.cpp:
//
//   DriftTexture drift_internal_texture_create_from_pixels(
//       const uint8_t* pixels, int w, int h) {
//       // Use the existing create_gpu_texture helper and register in
//       // g_textures, then return the DriftTexture handle.
//   }
//
// See the comment block in renderer.cpp near drift_texture_load for context.
