#ifndef DRIFT_INPUT_H
#define DRIFT_INPUT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Key codes (mirrors SDL scancodes for common keys) */
typedef enum DriftKey {
    DRIFT_KEY_UNKNOWN = 0,
    DRIFT_KEY_A = 4, DRIFT_KEY_B, DRIFT_KEY_C, DRIFT_KEY_D, DRIFT_KEY_E, DRIFT_KEY_F,
    DRIFT_KEY_G, DRIFT_KEY_H, DRIFT_KEY_I, DRIFT_KEY_J, DRIFT_KEY_K, DRIFT_KEY_L,
    DRIFT_KEY_M, DRIFT_KEY_N, DRIFT_KEY_O, DRIFT_KEY_P, DRIFT_KEY_Q, DRIFT_KEY_R,
    DRIFT_KEY_S, DRIFT_KEY_T, DRIFT_KEY_U, DRIFT_KEY_V, DRIFT_KEY_W, DRIFT_KEY_X,
    DRIFT_KEY_Y, DRIFT_KEY_Z,
    DRIFT_KEY_1 = 30, DRIFT_KEY_2, DRIFT_KEY_3, DRIFT_KEY_4, DRIFT_KEY_5,
    DRIFT_KEY_6, DRIFT_KEY_7, DRIFT_KEY_8, DRIFT_KEY_9, DRIFT_KEY_0,
    DRIFT_KEY_RETURN = 40,
    DRIFT_KEY_ESCAPE = 41,
    DRIFT_KEY_BACKSPACE = 42,
    DRIFT_KEY_TAB = 43,
    DRIFT_KEY_SPACE = 44,
    DRIFT_KEY_F1 = 58, DRIFT_KEY_F2, DRIFT_KEY_F3, DRIFT_KEY_F4, DRIFT_KEY_F5, DRIFT_KEY_F6,
    DRIFT_KEY_F7, DRIFT_KEY_F8, DRIFT_KEY_F9, DRIFT_KEY_F10, DRIFT_KEY_F11, DRIFT_KEY_F12,
    DRIFT_KEY_RIGHT = 79,
    DRIFT_KEY_LEFT = 80,
    DRIFT_KEY_DOWN = 81,
    DRIFT_KEY_UP = 82,
    DRIFT_KEY_LCTRL = 224,
    DRIFT_KEY_LSHIFT = 225,
    DRIFT_KEY_LALT = 226,
    DRIFT_KEY_RCTRL = 228,
    DRIFT_KEY_RSHIFT = 229,
    DRIFT_KEY_RALT = 230,
    DRIFT_KEY_MAX_ = 512,
} DriftKey;

typedef enum DriftMouseButton {
    DRIFT_MOUSE_LEFT = 1,
    DRIFT_MOUSE_MIDDLE = 2,
    DRIFT_MOUSE_RIGHT = 3,
} DriftMouseButton;

/* Input system */
DRIFT_API void drift_input_init(void);
DRIFT_API void drift_input_update(void);  /* call before polling events */
DRIFT_API void drift_input_process_event(const void* sdl_event);

/* Keyboard */
DRIFT_API bool drift_key_pressed(DriftKey key);
DRIFT_API bool drift_key_held(DriftKey key);
DRIFT_API bool drift_key_released(DriftKey key);

/* Mouse */
DRIFT_API DriftVec2 drift_mouse_position(void);
DRIFT_API DriftVec2 drift_mouse_delta(void);
DRIFT_API bool      drift_mouse_button_pressed(DriftMouseButton button);
DRIFT_API bool      drift_mouse_button_held(DriftMouseButton button);
DRIFT_API bool      drift_mouse_button_released(DriftMouseButton button);
DRIFT_API float     drift_mouse_wheel_delta(void);

/* Gamepad */
DRIFT_API bool      drift_gamepad_connected(int32_t index);
DRIFT_API float     drift_gamepad_axis(int32_t index, int32_t axis);
DRIFT_API bool      drift_gamepad_button_pressed(int32_t index, int32_t button);
DRIFT_API bool      drift_gamepad_button_held(int32_t index, int32_t button);
DRIFT_API bool      drift_gamepad_button_released(int32_t index, int32_t button);

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_INPUT_H */
