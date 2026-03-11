// =============================================================================
// Drift 2D Game Engine - Sprite & Animation Implementation
// =============================================================================
// Implements the public drift/sprite.h API.
// Grid-based spritesheets, Aseprite .ase file loading (via cute_aseprite.h),
// and frame-based animation playback.
// =============================================================================

#define CUTE_ASEPRITE_IMPLEMENTATION
#include "cute_aseprite.h"

#include <drift/sprite.h>
#include <drift/renderer.h>
#include <drift/drift.h>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

// =============================================================================
// Internal types and state (anonymous namespace)
// =============================================================================
namespace {

// ---- Aseprite tag info stored per spritesheet --------------------------------

struct AseTag {
    std::string name;
    int32_t     from_frame;       // inclusive
    int32_t     to_frame;         // inclusive
    bool        looping;          // ping-pong / loop from ase tag direction
    std::vector<float> durations; // per-frame duration in seconds
};

// ---- Spritesheet internal data -----------------------------------------------

struct SpriteSheetData {
    DriftTexture          texture;
    int32_t               frame_w;
    int32_t               frame_h;
    int32_t               columns;
    int32_t               rows;
    int32_t               total_frames;
    DriftVec2             pivot;
    std::vector<DriftRect> frame_rects;  // pre-computed rects for every frame
    std::vector<AseTag>   tags;          // populated only for .ase sheets
    bool                  alive;
};

static std::vector<SpriteSheetData> g_sheets;         // index 0 unused
static uint32_t                     g_next_sheet_id = 1;

// ---- Animation internal data -------------------------------------------------

struct AnimationData {
    uint32_t              sheet_id;
    std::vector<int32_t>  frames;        // frame indices into the sheet
    std::vector<float>    durations;     // per-frame duration (seconds)
    int32_t               frame_count;
    bool                  looping;
    int32_t               current_frame;
    float                 elapsed;       // time within the current frame
    bool                  finished;
    bool                  alive;
};

static std::vector<AnimationData> g_anims;             // index 0 unused
static uint32_t                   g_next_anim_id = 1;

// ---- Helpers -----------------------------------------------------------------

static SpriteSheetData* sheet_get(uint32_t id) {
    if (id == 0 || id >= g_sheets.size() || !g_sheets[id].alive) return nullptr;
    return &g_sheets[id];
}

static AnimationData* anim_get(uint32_t id) {
    if (id == 0 || id >= g_anims.size() || !g_anims[id].alive) return nullptr;
    return &g_anims[id];
}

static uint32_t alloc_sheet(const SpriteSheetData& data) {
    // Try to reuse a dead slot
    for (uint32_t i = 1; i < (uint32_t)g_sheets.size(); ++i) {
        if (!g_sheets[i].alive) {
            g_sheets[i] = data;
            g_sheets[i].alive = true;
            return i;
        }
    }
    uint32_t id = g_next_sheet_id++;
    if (id >= g_sheets.size()) {
        g_sheets.resize(id + 1);
    }
    g_sheets[id] = data;
    g_sheets[id].alive = true;
    return id;
}

static uint32_t alloc_anim(const AnimationData& data) {
    for (uint32_t i = 1; i < (uint32_t)g_anims.size(); ++i) {
        if (!g_anims[i].alive) {
            g_anims[i] = data;
            g_anims[i].alive = true;
            return i;
        }
    }
    uint32_t id = g_next_anim_id++;
    if (id >= g_anims.size()) {
        g_anims.resize(id + 1);
    }
    g_anims[id] = data;
    g_anims[id].alive = true;
    return id;
}

// Build the frame_rects vector for a grid-based sheet.
static void build_frame_rects(SpriteSheetData& s) {
    s.frame_rects.clear();
    s.frame_rects.reserve(s.total_frames);
    for (int32_t row = 0; row < s.rows; ++row) {
        for (int32_t col = 0; col < s.columns; ++col) {
            DriftRect r;
            r.x = (float)(col * s.frame_w);
            r.y = (float)(row * s.frame_h);
            r.w = (float)s.frame_w;
            r.h = (float)s.frame_h;
            s.frame_rects.push_back(r);
        }
    }
}

// Resolve a flat frame index to the corresponding DriftRect.
static DriftRect frame_rect_for_index(const SpriteSheetData& s, int32_t idx) {
    if (idx < 0 || idx >= (int32_t)s.frame_rects.size()) {
        return DriftRect{0, 0, (float)s.frame_w, (float)s.frame_h};
    }
    return s.frame_rects[idx];
}

} // anonymous namespace

// =============================================================================
// Spritesheet API
// =============================================================================

extern "C" {

DriftSpritesheet drift_spritesheet_create(DriftTexture texture, int32_t frame_w, int32_t frame_h) {
    DriftSpritesheet handle = {0};

    if (frame_w <= 0 || frame_h <= 0) {
        drift_log(DRIFT_LOG_ERROR, "drift_spritesheet_create: invalid frame size %dx%d", frame_w, frame_h);
        return handle;
    }

    int32_t tex_w = 0, tex_h = 0;
    drift_texture_get_size(texture, &tex_w, &tex_h);
    if (tex_w <= 0 || tex_h <= 0) {
        drift_log(DRIFT_LOG_ERROR, "drift_spritesheet_create: cannot determine texture size (id=%u)", texture.id);
        return handle;
    }

    SpriteSheetData s;
    s.texture      = texture;
    s.frame_w      = frame_w;
    s.frame_h      = frame_h;
    s.columns      = tex_w / frame_w;
    s.rows         = tex_h / frame_h;
    s.total_frames = s.columns * s.rows;
    s.pivot        = DRIFT_VEC2_ZERO;
    s.alive        = false; // alloc_sheet sets to true

    build_frame_rects(s);

    handle.id = alloc_sheet(s);
    drift_log(DRIFT_LOG_DEBUG, "Created spritesheet id=%u (%dx%d frames, %d total)",
              handle.id, s.columns, s.rows, s.total_frames);
    return handle;
}

DriftSpritesheet drift_spritesheet_load_ase(const char* path) {
    DriftSpritesheet handle = {0};

    if (!path) {
        drift_log(DRIFT_LOG_ERROR, "drift_spritesheet_load_ase: null path");
        return handle;
    }

    ase_t* ase = cute_aseprite_load_from_file(path, nullptr);
    if (!ase) {
        drift_log(DRIFT_LOG_ERROR, "drift_spritesheet_load_ase: failed to load '%s'", path);
        return handle;
    }

    // ------------------------------------------------------------------
    // Build a SpriteSheetData from the parsed Aseprite document.
    // Each Aseprite frame has its own pixel dimensions.  For the common
    // case every frame is the same size; use frame 0 as the canonical
    // frame size.
    // ------------------------------------------------------------------

    SpriteSheetData s;
    s.frame_w      = ase->w;
    s.frame_h      = ase->h;
    s.columns      = ase->frame_count;   // treat all frames as a single row
    s.rows         = 1;
    s.total_frames = ase->frame_count;
    s.pivot        = DRIFT_VEC2_ZERO;
    s.alive        = false;

    // ------------------------------------------------------------------
    // Build individual frame rects.  Since we do not yet upload an atlas
    // texture from the raw pixel data (that requires renderer integration),
    // we store logical rects as if the frames were laid out in a single
    // horizontal strip.
    // ------------------------------------------------------------------
    s.frame_rects.reserve(ase->frame_count);
    for (int32_t i = 0; i < ase->frame_count; ++i) {
        DriftRect r;
        r.x = (float)(i * s.frame_w);
        r.y = 0.0f;
        r.w = (float)s.frame_w;
        r.h = (float)s.frame_h;
        s.frame_rects.push_back(r);
    }

    // ------------------------------------------------------------------
    // Parse tags for named animation sequences.
    // ------------------------------------------------------------------
    for (int32_t t = 0; t < ase->tag_count; ++t) {
        const ase_tag_t& tag = ase->tags[t];

        AseTag at;
        at.name       = tag.name;
        at.from_frame = tag.from_frame;
        at.to_frame   = tag.to_frame;
        at.looping    = true; // default to looping; callers may override

        // Store per-frame durations from the aseprite frame data (ms -> seconds).
        for (int32_t fi = tag.from_frame; fi <= tag.to_frame; ++fi) {
            float dur_sec = (float)ase->frames[fi].duration_milliseconds / 1000.0f;
            at.durations.push_back(dur_sec);
        }

        s.tags.push_back(std::move(at));
    }

    // ------------------------------------------------------------------
    // Texture upload stub -- log a warning.  The caller is expected to
    // create a DriftTexture from the pixel data (ase->frames[i].pixels)
    // and assign it via a future helper, or the renderer can be extended
    // to support this path.  For now we leave texture.id = 0.
    // ------------------------------------------------------------------
    s.texture.id = DRIFT_INVALID_HANDLE_ID;
    drift_log(DRIFT_LOG_WARN,
              "drift_spritesheet_load_ase: loaded '%s' (%d frames, %d tags) -- "
              "atlas texture upload not yet implemented; texture.id will be 0",
              path, ase->frame_count, ase->tag_count);

    cute_aseprite_free(ase);

    handle.id = alloc_sheet(s);
    drift_log(DRIFT_LOG_DEBUG, "Created ase spritesheet id=%u from '%s'", handle.id, path);
    return handle;
}

void drift_spritesheet_destroy(DriftSpritesheet sheet) {
    SpriteSheetData* s = sheet_get(sheet.id);
    if (!s) return;

    // Release internal memory held by vectors.
    s->frame_rects.clear();
    s->frame_rects.shrink_to_fit();
    s->tags.clear();
    s->tags.shrink_to_fit();
    s->alive = false;
}

DriftRect drift_spritesheet_frame(DriftSpritesheet sheet, int32_t col, int32_t row) {
    SpriteSheetData* s = sheet_get(sheet.id);
    if (!s) return DriftRect{0, 0, 0, 0};

    // Clamp col/row to valid range.
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (col >= s->columns) col = s->columns - 1;
    if (row >= s->rows)    row = s->rows - 1;

    return DriftRect{
        (float)(col * s->frame_w),
        (float)(row * s->frame_h),
        (float)s->frame_w,
        (float)s->frame_h
    };
}

int32_t drift_spritesheet_frame_count(DriftSpritesheet sheet) {
    SpriteSheetData* s = sheet_get(sheet.id);
    return s ? s->total_frames : 0;
}

void drift_spritesheet_set_pivot(DriftSpritesheet sheet, float x, float y) {
    SpriteSheetData* s = sheet_get(sheet.id);
    if (s) {
        s->pivot.x = x;
        s->pivot.y = y;
    }
}

DriftVec2 drift_spritesheet_get_pivot(DriftSpritesheet sheet) {
    SpriteSheetData* s = sheet_get(sheet.id);
    return s ? s->pivot : DRIFT_VEC2_ZERO;
}

DriftTexture drift_spritesheet_get_texture(DriftSpritesheet sheet) {
    SpriteSheetData* s = sheet_get(sheet.id);
    if (s) return s->texture;
    DriftTexture t = {DRIFT_INVALID_HANDLE_ID};
    return t;
}

// =============================================================================
// Animation API
// =============================================================================

DriftAnimation drift_animation_create(DriftSpritesheet sheet, const DriftAnimationDef* def) {
    DriftAnimation handle = {0};

    if (!def || def->frame_count <= 0 || !def->frames || !def->durations) {
        drift_log(DRIFT_LOG_ERROR, "drift_animation_create: invalid DriftAnimationDef");
        return handle;
    }

    SpriteSheetData* s = sheet_get(sheet.id);
    if (!s) {
        drift_log(DRIFT_LOG_ERROR, "drift_animation_create: invalid spritesheet id=%u", sheet.id);
        return handle;
    }

    AnimationData a;
    a.sheet_id      = sheet.id;
    a.frame_count   = def->frame_count;
    a.looping       = def->looping;
    a.current_frame = 0;
    a.elapsed       = 0.0f;
    a.finished      = false;
    a.alive         = false;

    a.frames.resize(def->frame_count);
    a.durations.resize(def->frame_count);
    for (int32_t i = 0; i < def->frame_count; ++i) {
        a.frames[i]    = def->frames[i];
        a.durations[i] = def->durations[i];
    }

    handle.id = alloc_anim(a);
    return handle;
}

DriftAnimation drift_animation_create_from_tag(DriftSpritesheet sheet, const char* tag_name) {
    DriftAnimation handle = {0};

    if (!tag_name) {
        drift_log(DRIFT_LOG_ERROR, "drift_animation_create_from_tag: null tag name");
        return handle;
    }

    SpriteSheetData* s = sheet_get(sheet.id);
    if (!s) {
        drift_log(DRIFT_LOG_ERROR, "drift_animation_create_from_tag: invalid spritesheet id=%u", sheet.id);
        return handle;
    }

    // Search for the tag by name.
    const AseTag* found = nullptr;
    for (const auto& tag : s->tags) {
        if (tag.name == tag_name) {
            found = &tag;
            break;
        }
    }

    if (!found) {
        drift_log(DRIFT_LOG_WARN, "drift_animation_create_from_tag: tag '%s' not found in sheet id=%u",
                  tag_name, sheet.id);
        return handle;
    }

    int32_t count = found->to_frame - found->from_frame + 1;
    if (count <= 0) {
        drift_log(DRIFT_LOG_WARN, "drift_animation_create_from_tag: tag '%s' has no frames", tag_name);
        return handle;
    }

    AnimationData a;
    a.sheet_id      = sheet.id;
    a.frame_count   = count;
    a.looping       = found->looping;
    a.current_frame = 0;
    a.elapsed       = 0.0f;
    a.finished      = false;
    a.alive         = false;

    a.frames.resize(count);
    a.durations.resize(count);
    for (int32_t i = 0; i < count; ++i) {
        a.frames[i]    = found->from_frame + i;
        a.durations[i] = (i < (int32_t)found->durations.size())
                             ? found->durations[i]
                             : 0.1f; // fallback 100 ms
    }

    handle.id = alloc_anim(a);
    return handle;
}

void drift_animation_destroy(DriftAnimation anim) {
    AnimationData* a = anim_get(anim.id);
    if (!a) return;

    a->frames.clear();
    a->frames.shrink_to_fit();
    a->durations.clear();
    a->durations.shrink_to_fit();
    a->alive = false;
}

void drift_animation_reset(DriftAnimation anim) {
    AnimationData* a = anim_get(anim.id);
    if (!a) return;
    a->current_frame = 0;
    a->elapsed       = 0.0f;
    a->finished      = false;
}

DriftRect drift_animation_update(DriftAnimation anim, float dt) {
    AnimationData* a = anim_get(anim.id);
    if (!a || a->frame_count <= 0) return DriftRect{0, 0, 0, 0};

    if (a->finished) {
        // Return the last frame if the animation has finished.
        SpriteSheetData* s = sheet_get(a->sheet_id);
        if (!s) return DriftRect{0, 0, 0, 0};
        return frame_rect_for_index(*s, a->frames[a->frame_count - 1]);
    }

    a->elapsed += dt;

    // Advance through frames as long as accumulated time exceeds the
    // current frame's duration.
    while (a->elapsed >= a->durations[a->current_frame]) {
        a->elapsed -= a->durations[a->current_frame];

        a->current_frame++;
        if (a->current_frame >= a->frame_count) {
            if (a->looping) {
                a->current_frame = 0;
            } else {
                a->current_frame = a->frame_count - 1;
                a->elapsed       = 0.0f;
                a->finished      = true;
                break;
            }
        }
    }

    SpriteSheetData* s = sheet_get(a->sheet_id);
    if (!s) return DriftRect{0, 0, 0, 0};
    return frame_rect_for_index(*s, a->frames[a->current_frame]);
}

DriftRect drift_animation_get_frame(DriftAnimation anim) {
    AnimationData* a = anim_get(anim.id);
    if (!a || a->frame_count <= 0) return DriftRect{0, 0, 0, 0};

    SpriteSheetData* s = sheet_get(a->sheet_id);
    if (!s) return DriftRect{0, 0, 0, 0};
    return frame_rect_for_index(*s, a->frames[a->current_frame]);
}

bool drift_animation_is_finished(DriftAnimation anim) {
    AnimationData* a = anim_get(anim.id);
    return a ? a->finished : true;
}

DriftSpritesheet drift_animation_get_sheet(DriftAnimation anim) {
    AnimationData* a = anim_get(anim.id);
    DriftSpritesheet handle = {0};
    if (a) handle.id = a->sheet_id;
    return handle;
}

} // extern "C"
