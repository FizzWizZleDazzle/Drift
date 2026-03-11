#ifndef DRIFT_FONT_H
#define DRIFT_FONT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

DRIFT_API DriftFont drift_font_load(const char* path, float size_px);
DRIFT_API void      drift_font_destroy(DriftFont font);

DRIFT_API void      drift_text_draw(DriftFont font, const char* text, DriftVec2 position,
                                     DriftColor color, float scale);
DRIFT_API DriftVec2 drift_text_measure(DriftFont font, const char* text, float scale);

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_FONT_H */
