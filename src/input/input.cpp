#include "drift/input.h"
#include "drift/drift.h"

#include <SDL3/SDL.h>

#include <string.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr int DRIFT_MAX_GAMEPADS     = 4;
static constexpr int DRIFT_MAX_GAMEPAD_AXES = 6;  // SDL_GAMEPAD_AXIS_COUNT
static constexpr int DRIFT_MAX_GAMEPAD_BTNS = 15; // SDL_GAMEPAD_BUTTON_COUNT
static constexpr int DRIFT_MAX_MOUSE_BTNS   = 5;

// ---------------------------------------------------------------------------
// Gamepad state
// ---------------------------------------------------------------------------

struct GamepadState {
    SDL_Gamepad *gamepad;
    SDL_JoystickID instance_id;
    float axes[DRIFT_MAX_GAMEPAD_AXES];
    bool  buttons_current[DRIFT_MAX_GAMEPAD_BTNS];
    bool  buttons_previous[DRIFT_MAX_GAMEPAD_BTNS];
};

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

static struct {
    // Keyboard
    bool key_current[DRIFT_KEY_MAX_];
    bool key_previous[DRIFT_KEY_MAX_];

    // Mouse
    DriftVec2 mouse_pos;
    DriftVec2 mouse_delta;
    bool      mouse_buttons_current[DRIFT_MAX_MOUSE_BTNS + 1];  // 1-indexed
    bool      mouse_buttons_previous[DRIFT_MAX_MOUSE_BTNS + 1];
    float     mouse_wheel_delta;

    // Gamepads
    GamepadState gamepads[DRIFT_MAX_GAMEPADS];
} s_input;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Find gamepad slot by SDL joystick instance id.  Returns -1 if not found.
static int find_gamepad_by_instance(SDL_JoystickID id)
{
    for (int i = 0; i < DRIFT_MAX_GAMEPADS; ++i) {
        if (s_input.gamepads[i].gamepad && s_input.gamepads[i].instance_id == id)
            return i;
    }
    return -1;
}

// Find first free gamepad slot.  Returns -1 if none available.
static int find_free_gamepad_slot(void)
{
    for (int i = 0; i < DRIFT_MAX_GAMEPADS; ++i) {
        if (!s_input.gamepads[i].gamepad)
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void drift_input_init(void)
{
    memset(&s_input, 0, sizeof(s_input));
    DRIFT_LOG_INFO("Input system initialised");
}

void drift_input_update(void)
{
    // Snapshot current -> previous for keyboard
    memcpy(s_input.key_previous, s_input.key_current, sizeof(s_input.key_current));

    // Snapshot current -> previous for mouse buttons
    memcpy(s_input.mouse_buttons_previous, s_input.mouse_buttons_current,
           sizeof(s_input.mouse_buttons_current));

    // Reset per-frame mouse accumulators
    s_input.mouse_delta.x    = 0.0f;
    s_input.mouse_delta.y    = 0.0f;
    s_input.mouse_wheel_delta = 0.0f;

    // Snapshot current -> previous for gamepad buttons
    for (int i = 0; i < DRIFT_MAX_GAMEPADS; ++i) {
        memcpy(s_input.gamepads[i].buttons_previous,
               s_input.gamepads[i].buttons_current,
               sizeof(s_input.gamepads[i].buttons_current));
    }
}

void drift_input_process_event(const void *sdl_event)
{
    const SDL_Event *ev = static_cast<const SDL_Event *>(sdl_event);

    switch (ev->type) {

    // ----- Keyboard --------------------------------------------------------
    case SDL_EVENT_KEY_DOWN: {
        int sc = static_cast<int>(ev->key.scancode);
        if (sc >= 0 && sc < DRIFT_KEY_MAX_)
            s_input.key_current[sc] = true;
    } break;

    case SDL_EVENT_KEY_UP: {
        int sc = static_cast<int>(ev->key.scancode);
        if (sc >= 0 && sc < DRIFT_KEY_MAX_)
            s_input.key_current[sc] = false;
    } break;

    // ----- Mouse -----------------------------------------------------------
    case SDL_EVENT_MOUSE_MOTION: {
        s_input.mouse_pos.x   = ev->motion.x;
        s_input.mouse_pos.y   = ev->motion.y;
        s_input.mouse_delta.x += ev->motion.xrel;
        s_input.mouse_delta.y += ev->motion.yrel;
    } break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        int btn = static_cast<int>(ev->button.button);
        if (btn >= 1 && btn <= DRIFT_MAX_MOUSE_BTNS)
            s_input.mouse_buttons_current[btn] = true;
    } break;

    case SDL_EVENT_MOUSE_BUTTON_UP: {
        int btn = static_cast<int>(ev->button.button);
        if (btn >= 1 && btn <= DRIFT_MAX_MOUSE_BTNS)
            s_input.mouse_buttons_current[btn] = false;
    } break;

    case SDL_EVENT_MOUSE_WHEEL: {
        s_input.mouse_wheel_delta += ev->wheel.y;
    } break;

    // ----- Gamepad ---------------------------------------------------------
    case SDL_EVENT_GAMEPAD_ADDED: {
        int slot = find_free_gamepad_slot();
        if (slot < 0) {
            DRIFT_LOG_WARN("No free gamepad slot for new device");
            break;
        }
        SDL_Gamepad *gp = SDL_OpenGamepad(ev->gdevice.which);
        if (!gp) {
            DRIFT_LOG_ERROR("Failed to open gamepad: %s", SDL_GetError());
            break;
        }
        GamepadState *gs = &s_input.gamepads[slot];
        gs->gamepad     = gp;
        gs->instance_id = ev->gdevice.which;
        memset(gs->axes,             0, sizeof(gs->axes));
        memset(gs->buttons_current,  0, sizeof(gs->buttons_current));
        memset(gs->buttons_previous, 0, sizeof(gs->buttons_previous));
        DRIFT_LOG_INFO("Gamepad connected in slot %d", slot);
    } break;

    case SDL_EVENT_GAMEPAD_REMOVED: {
        int slot = find_gamepad_by_instance(ev->gdevice.which);
        if (slot >= 0) {
            SDL_CloseGamepad(s_input.gamepads[slot].gamepad);
            memset(&s_input.gamepads[slot], 0, sizeof(GamepadState));
            DRIFT_LOG_INFO("Gamepad disconnected from slot %d", slot);
        }
    } break;

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        int slot = find_gamepad_by_instance(ev->gbutton.which);
        if (slot >= 0) {
            int btn = static_cast<int>(ev->gbutton.button);
            if (btn >= 0 && btn < DRIFT_MAX_GAMEPAD_BTNS)
                s_input.gamepads[slot].buttons_current[btn] = true;
        }
    } break;

    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        int slot = find_gamepad_by_instance(ev->gbutton.which);
        if (slot >= 0) {
            int btn = static_cast<int>(ev->gbutton.button);
            if (btn >= 0 && btn < DRIFT_MAX_GAMEPAD_BTNS)
                s_input.gamepads[slot].buttons_current[btn] = false;
        }
    } break;

    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        int slot = find_gamepad_by_instance(ev->gaxis.which);
        if (slot >= 0) {
            int axis = static_cast<int>(ev->gaxis.axis);
            if (axis >= 0 && axis < DRIFT_MAX_GAMEPAD_AXES) {
                // SDL axis range is -32768..32767 -- normalise to -1..1
                s_input.gamepads[slot].axes[axis] =
                    static_cast<float>(ev->gaxis.value) / 32767.0f;
            }
        }
    } break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Keyboard queries
// ---------------------------------------------------------------------------

bool drift_key_pressed(DriftKey key)
{
    int k = static_cast<int>(key);
    if (k < 0 || k >= DRIFT_KEY_MAX_) return false;
    return s_input.key_current[k] && !s_input.key_previous[k];
}

bool drift_key_held(DriftKey key)
{
    int k = static_cast<int>(key);
    if (k < 0 || k >= DRIFT_KEY_MAX_) return false;
    return s_input.key_current[k];
}

bool drift_key_released(DriftKey key)
{
    int k = static_cast<int>(key);
    if (k < 0 || k >= DRIFT_KEY_MAX_) return false;
    return !s_input.key_current[k] && s_input.key_previous[k];
}

// ---------------------------------------------------------------------------
// Mouse queries
// ---------------------------------------------------------------------------

DriftVec2 drift_mouse_position(void)
{
    return s_input.mouse_pos;
}

DriftVec2 drift_mouse_delta(void)
{
    return s_input.mouse_delta;
}

bool drift_mouse_button_pressed(DriftMouseButton button)
{
    int b = static_cast<int>(button);
    if (b < 1 || b > DRIFT_MAX_MOUSE_BTNS) return false;
    return s_input.mouse_buttons_current[b] && !s_input.mouse_buttons_previous[b];
}

bool drift_mouse_button_held(DriftMouseButton button)
{
    int b = static_cast<int>(button);
    if (b < 1 || b > DRIFT_MAX_MOUSE_BTNS) return false;
    return s_input.mouse_buttons_current[b];
}

bool drift_mouse_button_released(DriftMouseButton button)
{
    int b = static_cast<int>(button);
    if (b < 1 || b > DRIFT_MAX_MOUSE_BTNS) return false;
    return !s_input.mouse_buttons_current[b] && s_input.mouse_buttons_previous[b];
}

float drift_mouse_wheel_delta(void)
{
    return s_input.mouse_wheel_delta;
}

// ---------------------------------------------------------------------------
// Gamepad queries
// ---------------------------------------------------------------------------

bool drift_gamepad_connected(int32_t index)
{
    if (index < 0 || index >= DRIFT_MAX_GAMEPADS) return false;
    return s_input.gamepads[index].gamepad != nullptr;
}

float drift_gamepad_axis(int32_t index, int32_t axis)
{
    if (index < 0 || index >= DRIFT_MAX_GAMEPADS) return 0.0f;
    if (axis  < 0 || axis  >= DRIFT_MAX_GAMEPAD_AXES) return 0.0f;
    if (!s_input.gamepads[index].gamepad) return 0.0f;
    return s_input.gamepads[index].axes[axis];
}

bool drift_gamepad_button_pressed(int32_t index, int32_t button)
{
    if (index  < 0 || index  >= DRIFT_MAX_GAMEPADS) return false;
    if (button < 0 || button >= DRIFT_MAX_GAMEPAD_BTNS) return false;
    if (!s_input.gamepads[index].gamepad) return false;
    return s_input.gamepads[index].buttons_current[button]
        && !s_input.gamepads[index].buttons_previous[button];
}

bool drift_gamepad_button_held(int32_t index, int32_t button)
{
    if (index  < 0 || index  >= DRIFT_MAX_GAMEPADS) return false;
    if (button < 0 || button >= DRIFT_MAX_GAMEPAD_BTNS) return false;
    if (!s_input.gamepads[index].gamepad) return false;
    return s_input.gamepads[index].buttons_current[button];
}

bool drift_gamepad_button_released(int32_t index, int32_t button)
{
    if (index  < 0 || index  >= DRIFT_MAX_GAMEPADS) return false;
    if (button < 0 || button >= DRIFT_MAX_GAMEPAD_BTNS) return false;
    if (!s_input.gamepads[index].gamepad) return false;
    return !s_input.gamepads[index].buttons_current[button]
        && s_input.gamepads[index].buttons_previous[button];
}
