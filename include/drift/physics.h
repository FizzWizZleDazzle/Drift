#ifndef DRIFT_PHYSICS_H
#define DRIFT_PHYSICS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* World */
DRIFT_API DriftResult drift_physics_init(DriftVec2 gravity);
DRIFT_API void        drift_physics_shutdown(void);
DRIFT_API void        drift_physics_step(float dt);

/* Bodies */
DRIFT_API DriftBody drift_body_create(const DriftBodyDef* def);
DRIFT_API void      drift_body_destroy(DriftBody body);
DRIFT_API DriftVec2 drift_body_get_position(DriftBody body);
DRIFT_API float     drift_body_get_rotation(DriftBody body);
DRIFT_API void      drift_body_set_position(DriftBody body, DriftVec2 pos);
DRIFT_API void      drift_body_set_rotation(DriftBody body, float rotation);
DRIFT_API DriftVec2 drift_body_get_velocity(DriftBody body);
DRIFT_API void      drift_body_set_velocity(DriftBody body, DriftVec2 vel);
DRIFT_API void      drift_body_apply_force(DriftBody body, DriftVec2 force);
DRIFT_API void      drift_body_apply_impulse(DriftBody body, DriftVec2 impulse);
DRIFT_API void      drift_body_set_gravity_scale(DriftBody body, float scale);

/* Associate user data (entity) with body */
DRIFT_API void         drift_body_set_user_data(DriftBody body, void* data);
DRIFT_API void*        drift_body_get_user_data(DriftBody body);

/* Shapes (colliders) */
DRIFT_API DriftShape drift_shape_add_circle(DriftBody body, DriftVec2 offset, float radius, const DriftShapeDef* def);
DRIFT_API DriftShape drift_shape_add_box(DriftBody body, float half_w, float half_h, const DriftShapeDef* def);
DRIFT_API DriftShape drift_shape_add_polygon(DriftBody body, const DriftVec2* vertices, int32_t count, const DriftShapeDef* def);
DRIFT_API DriftShape drift_shape_add_capsule(DriftBody body, DriftVec2 p1, DriftVec2 p2, float radius, const DriftShapeDef* def);

/* Shape user data */
DRIFT_API void  drift_shape_set_user_data(DriftShape shape, void* data);
DRIFT_API void* drift_shape_get_user_data(DriftShape shape);

/* Contact events (query after step) */
DRIFT_API int32_t               drift_physics_get_contact_begin_count(void);
DRIFT_API const DriftContactEvent* drift_physics_get_contact_begin_events(void);
DRIFT_API int32_t               drift_physics_get_contact_end_count(void);
DRIFT_API const DriftContactEvent* drift_physics_get_contact_end_events(void);
DRIFT_API int32_t               drift_physics_get_hit_count(void);
DRIFT_API const DriftHitEvent*  drift_physics_get_hit_events(void);
DRIFT_API int32_t               drift_physics_get_sensor_begin_count(void);
DRIFT_API const DriftContactEvent* drift_physics_get_sensor_begin_events(void);
DRIFT_API int32_t               drift_physics_get_sensor_end_count(void);
DRIFT_API const DriftContactEvent* drift_physics_get_sensor_end_events(void);

/* Spatial queries */
DRIFT_API DriftRaycastHit drift_physics_raycast(DriftVec2 origin, DriftVec2 direction, float distance, DriftCollisionFilter filter);
DRIFT_API int32_t         drift_physics_overlap_aabb(DriftRect aabb, DriftCollisionFilter filter, DriftShape* out_shapes, int32_t max_shapes);

/* Stats */
DRIFT_API int32_t drift_physics_get_body_count(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_PHYSICS_H */
