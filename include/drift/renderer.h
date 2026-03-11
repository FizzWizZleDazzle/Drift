#ifndef DRIFT_RENDERER_H
#define DRIFT_RENDERER_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

DRIFT_API DriftResult drift_renderer_init(void);
DRIFT_API void        drift_renderer_shutdown(void);
DRIFT_API void        drift_renderer_begin_frame(void);
DRIFT_API void        drift_renderer_end_frame(void);
DRIFT_API void        drift_renderer_present(void);

/* Clear */
DRIFT_API void drift_renderer_set_clear_color(DriftColorF color);

/* Texture loading (goes through asset system) */
DRIFT_API DriftTexture drift_texture_load(const char* path);
DRIFT_API void         drift_texture_destroy(DriftTexture texture);
DRIFT_API void         drift_texture_get_size(DriftTexture texture, int32_t* w, int32_t* h);

/* Sprite drawing */
DRIFT_API void drift_sprite_draw(DriftTexture texture, DriftVec2 position, DriftRect src_rect,
                                  DriftVec2 scale, float rotation, DriftVec2 origin,
                                  DriftColor tint, DriftFlip flip, float z_order);

/* Primitive drawing */
DRIFT_API void drift_draw_rect(DriftRect rect, DriftColor color);
DRIFT_API void drift_draw_rect_filled(DriftRect rect, DriftColor color);
DRIFT_API void drift_draw_line(DriftVec2 start, DriftVec2 end, DriftColor color, float thickness);
DRIFT_API void drift_draw_circle(DriftVec2 center, float radius, DriftColor color, int segments);

/* Camera */
DRIFT_API DriftCamera drift_camera_create(void);
DRIFT_API void        drift_camera_destroy(DriftCamera camera);
DRIFT_API void        drift_camera_set_active(DriftCamera camera);
DRIFT_API void        drift_camera_set_position(DriftCamera camera, DriftVec2 position);
DRIFT_API void        drift_camera_set_zoom(DriftCamera camera, float zoom);
DRIFT_API void        drift_camera_set_rotation(DriftCamera camera, float rotation);
DRIFT_API DriftVec2   drift_camera_get_position(DriftCamera camera);
DRIFT_API float       drift_camera_get_zoom(DriftCamera camera);
DRIFT_API DriftVec2   drift_camera_screen_to_world(DriftCamera camera, DriftVec2 screen_pos);
DRIFT_API DriftVec2   drift_camera_world_to_screen(DriftCamera camera, DriftVec2 world_pos);

/* Stats */
DRIFT_API int32_t drift_renderer_get_draw_calls(void);
DRIFT_API int32_t drift_renderer_get_sprite_count(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_RENDERER_H */
