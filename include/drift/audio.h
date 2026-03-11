#ifndef DRIFT_AUDIO_H
#define DRIFT_AUDIO_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

DRIFT_API DriftResult drift_audio_init(void);
DRIFT_API void        drift_audio_shutdown(void);
DRIFT_API void        drift_audio_update(void);

/* Sound effects */
DRIFT_API DriftSound        drift_sound_load(const char* path);
DRIFT_API void              drift_sound_destroy(DriftSound sound);
DRIFT_API DriftPlayingSound drift_sound_play(DriftSound sound, float volume, float pan);
DRIFT_API void              drift_playing_sound_stop(DriftPlayingSound playing);
DRIFT_API void              drift_playing_sound_set_volume(DriftPlayingSound playing, float volume);
DRIFT_API void              drift_playing_sound_set_pan(DriftPlayingSound playing, float pan);
DRIFT_API bool              drift_playing_sound_is_playing(DriftPlayingSound playing);

/* Music */
DRIFT_API DriftResult drift_music_play(const char* path, bool loop);
DRIFT_API void        drift_music_stop(void);
DRIFT_API void        drift_music_pause(void);
DRIFT_API void        drift_music_resume(void);
DRIFT_API void        drift_music_set_volume(float volume);
DRIFT_API bool        drift_music_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_AUDIO_H */
