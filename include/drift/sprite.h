#ifndef DRIFT_SPRITE_H
#define DRIFT_SPRITE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Grid spritesheet */
DRIFT_API DriftSpritesheet drift_spritesheet_create(DriftTexture texture, int32_t frame_w, int32_t frame_h);
DRIFT_API DriftSpritesheet drift_spritesheet_load_ase(const char* path);
DRIFT_API void             drift_spritesheet_destroy(DriftSpritesheet sheet);

DRIFT_API DriftRect drift_spritesheet_frame(DriftSpritesheet sheet, int32_t col, int32_t row);
DRIFT_API int32_t   drift_spritesheet_frame_count(DriftSpritesheet sheet);
DRIFT_API void      drift_spritesheet_set_pivot(DriftSpritesheet sheet, float x, float y);
DRIFT_API DriftVec2 drift_spritesheet_get_pivot(DriftSpritesheet sheet);
DRIFT_API DriftTexture drift_spritesheet_get_texture(DriftSpritesheet sheet);

/* Animation */
typedef struct DriftAnimationDef {
    int32_t* frames;        /* array of frame indices */
    float*   durations;     /* duration per frame in seconds */
    int32_t  frame_count;
    bool     looping;
} DriftAnimationDef;

DRIFT_API DriftAnimation drift_animation_create(DriftSpritesheet sheet, const DriftAnimationDef* def);
DRIFT_API DriftAnimation drift_animation_create_from_tag(DriftSpritesheet sheet, const char* tag_name);
DRIFT_API void           drift_animation_destroy(DriftAnimation anim);
DRIFT_API void           drift_animation_reset(DriftAnimation anim);
DRIFT_API DriftRect      drift_animation_update(DriftAnimation anim, float dt);
DRIFT_API DriftRect      drift_animation_get_frame(DriftAnimation anim);
DRIFT_API bool           drift_animation_is_finished(DriftAnimation anim);
DRIFT_API DriftSpritesheet drift_animation_get_sheet(DriftAnimation anim);

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_SPRITE_H */
