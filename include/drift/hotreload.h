#ifndef DRIFT_HOTRELOAD_H
#define DRIFT_HOTRELOAD_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* State passed to game plugins */
typedef struct DriftState {
    void* ecs_world;     /* DriftWorld* */
    void* renderer;      /* internal renderer state */
    void* physics;       /* internal physics state */
    void* audio;         /* internal audio state */
    void* assets;        /* internal asset state */
    void* input;         /* internal input state */
    float dt;
} DriftState;

/* Game plugin contract */
typedef bool (*DriftGameLoadFn)(DriftState* state);
typedef bool (*DriftGameUpdateFn)(DriftState* state, float dt);
typedef void (*DriftGameUnloadFn)(DriftState* state);

DRIFT_API DriftPlugin drift_plugin_load(const char* path);
DRIFT_API void        drift_plugin_unload(DriftPlugin plugin);
DRIFT_API bool        drift_plugin_update(DriftPlugin plugin);
DRIFT_API bool        drift_plugin_is_valid(DriftPlugin plugin);

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_HOTRELOAD_H */
