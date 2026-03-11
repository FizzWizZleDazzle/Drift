#ifndef DRIFT_PARTICLES_H
#define DRIFT_PARTICLES_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

DRIFT_API DriftEmitter drift_emitter_create(const DriftEmitterDef* def);
DRIFT_API void         drift_emitter_destroy(DriftEmitter emitter);

DRIFT_API void drift_emitter_set_position(DriftEmitter emitter, DriftVec2 pos);
DRIFT_API void drift_emitter_burst(DriftEmitter emitter, int32_t count);
DRIFT_API void drift_emitter_start(DriftEmitter emitter);
DRIFT_API void drift_emitter_stop(DriftEmitter emitter);
DRIFT_API bool drift_emitter_is_active(DriftEmitter emitter);

DRIFT_API void drift_particles_update(float dt);
DRIFT_API void drift_particles_render(void);

DRIFT_API int32_t drift_particles_get_count(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_PARTICLES_H */
