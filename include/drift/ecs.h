#ifndef DRIFT_ECS_H
#define DRIFT_ECS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque world handle */
typedef struct DriftWorld DriftWorld;

/* System callback */
typedef void (*DriftSystemCallback)(DriftWorld* world, float dt, void* user_data);

/* Query iterator */
typedef struct DriftQueryIter {
    void* _internal;
    int32_t count;
} DriftQueryIter;

/* World lifecycle */
DRIFT_API DriftWorld* drift_ecs_world_create(void);
DRIFT_API void        drift_ecs_world_destroy(DriftWorld* world);
DRIFT_API void        drift_ecs_progress(DriftWorld* world, float dt);

/* Components */
DRIFT_API DriftComponentId drift_ecs_component_register(DriftWorld* world, const char* name, size_t size, size_t alignment);
DRIFT_API DriftComponentId drift_ecs_component_lookup(DriftWorld* world, const char* name);

/* Entities */
DRIFT_API DriftEntity drift_ecs_entity_create(DriftWorld* world);
DRIFT_API void        drift_ecs_entity_destroy(DriftWorld* world, DriftEntity entity);
DRIFT_API bool        drift_ecs_entity_is_alive(DriftWorld* world, DriftEntity entity);
DRIFT_API void        drift_ecs_entity_add(DriftWorld* world, DriftEntity entity, DriftComponentId component);
DRIFT_API void        drift_ecs_entity_remove(DriftWorld* world, DriftEntity entity, DriftComponentId component);
DRIFT_API void        drift_ecs_entity_set(DriftWorld* world, DriftEntity entity, DriftComponentId component, const void* data, size_t size);
DRIFT_API const void* drift_ecs_entity_get(DriftWorld* world, DriftEntity entity, DriftComponentId component);
DRIFT_API void*       drift_ecs_entity_get_mut(DriftWorld* world, DriftEntity entity, DriftComponentId component);
DRIFT_API bool        drift_ecs_entity_has(DriftWorld* world, DriftEntity entity, DriftComponentId component);

/* Systems */
DRIFT_API DriftSystemId drift_ecs_system_register(DriftWorld* world, const char* name, DriftPhase phase,
                                                   const char* query_expr, DriftSystemCallback callback, void* user_data);

/* Singleton resources */
DRIFT_API void        drift_ecs_set_singleton(DriftWorld* world, DriftComponentId component, const void* data, size_t size);
DRIFT_API const void* drift_ecs_get_singleton(DriftWorld* world, DriftComponentId component);
DRIFT_API void*       drift_ecs_get_singleton_mut(DriftWorld* world, DriftComponentId component);

/* Query */
DRIFT_API DriftQueryIter drift_ecs_query_iter(DriftWorld* world, const char* query_expr);
DRIFT_API bool           drift_ecs_query_next(DriftQueryIter* iter);
DRIFT_API void*          drift_ecs_query_field(DriftQueryIter* iter, int32_t index, size_t size);
DRIFT_API DriftEntity*   drift_ecs_query_entities(DriftQueryIter* iter);
DRIFT_API void           drift_ecs_query_fini(DriftQueryIter* iter);

/* Built-in component IDs (set after world_create) */
DRIFT_API DriftComponentId drift_ecs_id_transform2d(DriftWorld* world);

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_ECS_H */
