#ifndef DRIFT_H
#define DRIFT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Main engine lifecycle */
typedef void (*DriftUpdateFn)(float dt, void* user_data);

DRIFT_API DriftResult drift_init(const DriftConfig* config);
DRIFT_API void        drift_shutdown(void);
DRIFT_API DriftResult drift_run(DriftUpdateFn update_fn, void* user_data);

/* Frame timing */
DRIFT_API float drift_get_delta_time(void);
DRIFT_API float drift_get_fps(void);
DRIFT_API uint64_t drift_get_frame_count(void);
DRIFT_API double drift_get_time(void);

/* Window */
DRIFT_API void drift_get_window_size(int32_t* w, int32_t* h);
DRIFT_API void drift_set_window_title(const char* title);
DRIFT_API bool drift_should_quit(void);
DRIFT_API void drift_request_quit(void);

/* Logging */
typedef enum DriftLogLevel {
    DRIFT_LOG_TRACE = 0,
    DRIFT_LOG_DEBUG,
    DRIFT_LOG_INFO,
    DRIFT_LOG_WARN,
    DRIFT_LOG_ERROR,
} DriftLogLevel;

DRIFT_API void drift_log(DriftLogLevel level, const char* fmt, ...);

#define DRIFT_LOG_TRACE(...) drift_log(DRIFT_LOG_TRACE, __VA_ARGS__)
#define DRIFT_LOG_DEBUG(...) drift_log(DRIFT_LOG_DEBUG, __VA_ARGS__)
#define DRIFT_LOG_INFO(...)  drift_log(DRIFT_LOG_INFO,  __VA_ARGS__)
#define DRIFT_LOG_WARN(...)  drift_log(DRIFT_LOG_WARN,  __VA_ARGS__)
#define DRIFT_LOG_ERROR(...) drift_log(DRIFT_LOG_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_H */
