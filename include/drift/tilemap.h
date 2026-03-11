#ifndef DRIFT_TILEMAP_H
#define DRIFT_TILEMAP_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

DRIFT_API DriftTilemap drift_tilemap_load(const char* path);
DRIFT_API void         drift_tilemap_destroy(DriftTilemap map);

DRIFT_API void         drift_tilemap_draw(DriftTilemap map);
DRIFT_API int32_t      drift_tilemap_get_tile(DriftTilemap map, int32_t layer, int32_t x, int32_t y);
DRIFT_API void         drift_tilemap_set_tile(DriftTilemap map, int32_t layer, int32_t x, int32_t y, int32_t tile_id);

DRIFT_API int32_t      drift_tilemap_get_layer_count(DriftTilemap map);
DRIFT_API int32_t      drift_tilemap_get_width(DriftTilemap map);
DRIFT_API int32_t      drift_tilemap_get_height(DriftTilemap map);
DRIFT_API int32_t      drift_tilemap_get_tile_width(DriftTilemap map);
DRIFT_API int32_t      drift_tilemap_get_tile_height(DriftTilemap map);

/* Collision shapes for physics integration */
DRIFT_API int32_t      drift_tilemap_get_collision_rects(DriftTilemap map, DriftRect* out_rects, int32_t max_rects);

/* Object layer queries (spawn points, etc.) */
typedef struct DriftTilemapObject {
    const char* name;
    const char* type;
    DriftRect   rect;
    int32_t     id;
} DriftTilemapObject;

DRIFT_API int32_t      drift_tilemap_get_objects(DriftTilemap map, const char* layer_name,
                                                  DriftTilemapObject* out_objects, int32_t max_objects);

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_TILEMAP_H */
