#ifndef DRIFT_UI_H
#define DRIFT_UI_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

DRIFT_API DriftResult drift_ui_init(void);
DRIFT_API void        drift_ui_shutdown(void);
DRIFT_API void        drift_ui_begin_frame(void);
DRIFT_API void        drift_ui_end_frame(void);
DRIFT_API void        drift_ui_render(void);
DRIFT_API void        drift_ui_process_event(const void* sdl_event);

/* Game UI theming */
DRIFT_API void drift_ui_set_game_theme(const DriftUITheme* theme);
DRIFT_API void drift_ui_push_font(DriftFont font);
DRIFT_API void drift_ui_pop_font(void);

/* Widgets */
DRIFT_API bool drift_ui_button(const char* label, DriftRect rect);
DRIFT_API void drift_ui_label(const char* text, DriftVec2 pos);
DRIFT_API void drift_ui_image(DriftTexture texture, DriftRect rect, DriftRect src_rect);
DRIFT_API bool drift_ui_slider(const char* label, float* value, float min_val, float max_val);
DRIFT_API void drift_ui_progress_bar(float fraction, DriftRect rect);

/* Panels */
typedef enum DriftPanelFlags {
    DRIFT_PANEL_NONE       = 0,
    DRIFT_PANEL_NO_BG      = 1 << 0,
    DRIFT_PANEL_BORDER     = 1 << 1,
    DRIFT_PANEL_SCROLLABLE = 1 << 2,
} DriftPanelFlags;

DRIFT_API void drift_ui_panel_begin(DriftRect rect, DriftPanelFlags flags);
DRIFT_API void drift_ui_panel_end(void);

/* 9-slice */
DRIFT_API void drift_ui_9slice(DriftTexture texture, DriftRect rect, DriftRect borders);

/* Positioning helpers */
DRIFT_API void drift_ui_set_anchor(DriftAnchor anchor);

/* Dev overlay */
#ifdef DRIFT_DEV
DRIFT_API void drift_ui_dev_toggle(void);
DRIFT_API void drift_ui_dev_render(void);
DRIFT_API bool drift_ui_dev_is_visible(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_UI_H */
