#include "drift/resources/InputResource.hpp"
#include "drift/Log.hpp"

#include <SDL3/SDL.h>

#include <cstring>

namespace drift {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr int MAX_KEYS         = static_cast<int>(Key::Max_);
static constexpr int MAX_GAMEPADS     = 4;
static constexpr int MAX_GAMEPAD_AXES = 6;  // SDL_GAMEPAD_AXIS_COUNT
static constexpr int MAX_GAMEPAD_BTNS = 15; // SDL_GAMEPAD_BUTTON_COUNT
static constexpr int MAX_MOUSE_BTNS   = 5;

// ---------------------------------------------------------------------------
// Gamepad state (per-slot)
// ---------------------------------------------------------------------------

struct GamepadState {
    SDL_Gamepad*   gamepad     = nullptr;
    SDL_JoystickID instance_id = 0;
    float          axes[MAX_GAMEPAD_AXES]           = {};
    bool           buttons_current[MAX_GAMEPAD_BTNS]  = {};
    bool           buttons_previous[MAX_GAMEPAD_BTNS] = {};
};

// ---------------------------------------------------------------------------
// InputResource::Impl -- replaces old global s_input
// ---------------------------------------------------------------------------

struct InputResource::Impl {
    // Keyboard
    bool key_current[MAX_KEYS]  = {};
    bool key_previous[MAX_KEYS] = {};

    // Mouse
    Vec2  mouse_pos;
    Vec2  mouse_delta;
    bool  mouse_buttons_current[MAX_MOUSE_BTNS + 1]  = {};   // 1-indexed
    bool  mouse_buttons_previous[MAX_MOUSE_BTNS + 1] = {};
    float mouse_wheel_delta = 0.0f;

    // Gamepads
    GamepadState gamepads[MAX_GAMEPADS] = {};

    // Helpers
    int findGamepadByInstance(SDL_JoystickID id) const {
        for (int i = 0; i < MAX_GAMEPADS; ++i) {
            if (gamepads[i].gamepad && gamepads[i].instance_id == id)
                return i;
        }
        return -1;
    }

    int findFreeGamepadSlot() const {
        for (int i = 0; i < MAX_GAMEPADS; ++i) {
            if (!gamepads[i].gamepad)
                return i;
        }
        return -1;
    }
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

InputResource::InputResource()
    : impl_(new Impl)
{
    DRIFT_LOG_INFO("Input system initialised");
}

InputResource::~InputResource()
{
    // Close any open gamepads
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (impl_->gamepads[i].gamepad) {
            SDL_CloseGamepad(impl_->gamepads[i].gamepad);
            impl_->gamepads[i].gamepad = nullptr;
        }
    }
    delete impl_;
}

// ---------------------------------------------------------------------------
// Frame begin -- snapshot current -> previous, reset accumulators
// ---------------------------------------------------------------------------

void InputResource::beginFrame()
{
    // Keyboard: current -> previous
    std::memcpy(impl_->key_previous, impl_->key_current, sizeof(impl_->key_current));

    // Mouse buttons: current -> previous
    std::memcpy(impl_->mouse_buttons_previous, impl_->mouse_buttons_current,
                sizeof(impl_->mouse_buttons_current));

    // Reset per-frame mouse accumulators
    impl_->mouse_delta.x     = 0.0f;
    impl_->mouse_delta.y     = 0.0f;
    impl_->mouse_wheel_delta = 0.0f;

    // Gamepad buttons: current -> previous
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        std::memcpy(impl_->gamepads[i].buttons_previous,
                    impl_->gamepads[i].buttons_current,
                    sizeof(impl_->gamepads[i].buttons_current));
    }
}

// ---------------------------------------------------------------------------
// Event processing
// ---------------------------------------------------------------------------

void InputResource::processEvent(const SDL_Event& event)
{
    switch (event.type) {

    // ----- Keyboard --------------------------------------------------------
    case SDL_EVENT_KEY_DOWN: {
        int sc = static_cast<int>(event.key.scancode);
        if (sc >= 0 && sc < MAX_KEYS)
            impl_->key_current[sc] = true;
    } break;

    case SDL_EVENT_KEY_UP: {
        int sc = static_cast<int>(event.key.scancode);
        if (sc >= 0 && sc < MAX_KEYS)
            impl_->key_current[sc] = false;
    } break;

    // ----- Mouse -----------------------------------------------------------
    case SDL_EVENT_MOUSE_MOTION: {
        impl_->mouse_pos.x    = event.motion.x;
        impl_->mouse_pos.y    = event.motion.y;
        impl_->mouse_delta.x += event.motion.xrel;
        impl_->mouse_delta.y += event.motion.yrel;
    } break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        int btn = static_cast<int>(event.button.button);
        if (btn >= 1 && btn <= MAX_MOUSE_BTNS)
            impl_->mouse_buttons_current[btn] = true;
    } break;

    case SDL_EVENT_MOUSE_BUTTON_UP: {
        int btn = static_cast<int>(event.button.button);
        if (btn >= 1 && btn <= MAX_MOUSE_BTNS)
            impl_->mouse_buttons_current[btn] = false;
    } break;

    case SDL_EVENT_MOUSE_WHEEL: {
        impl_->mouse_wheel_delta += event.wheel.y;
    } break;

    // ----- Gamepad ---------------------------------------------------------
    case SDL_EVENT_GAMEPAD_ADDED: {
        int slot = impl_->findFreeGamepadSlot();
        if (slot < 0) {
            DRIFT_LOG_WARN("No free gamepad slot for new device");
            break;
        }
        SDL_Gamepad* gp = SDL_OpenGamepad(event.gdevice.which);
        if (!gp) {
            DRIFT_LOG_ERROR("Failed to open gamepad: %s", SDL_GetError());
            break;
        }
        GamepadState& gs = impl_->gamepads[slot];
        gs.gamepad     = gp;
        gs.instance_id = event.gdevice.which;
        std::memset(gs.axes,             0, sizeof(gs.axes));
        std::memset(gs.buttons_current,  0, sizeof(gs.buttons_current));
        std::memset(gs.buttons_previous, 0, sizeof(gs.buttons_previous));
        DRIFT_LOG_INFO("Gamepad connected in slot %d", slot);
    } break;

    case SDL_EVENT_GAMEPAD_REMOVED: {
        int slot = impl_->findGamepadByInstance(event.gdevice.which);
        if (slot >= 0) {
            SDL_CloseGamepad(impl_->gamepads[slot].gamepad);
            impl_->gamepads[slot] = GamepadState{};
            DRIFT_LOG_INFO("Gamepad disconnected from slot %d", slot);
        }
    } break;

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        int slot = impl_->findGamepadByInstance(event.gbutton.which);
        if (slot >= 0) {
            int btn = static_cast<int>(event.gbutton.button);
            if (btn >= 0 && btn < MAX_GAMEPAD_BTNS)
                impl_->gamepads[slot].buttons_current[btn] = true;
        }
    } break;

    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        int slot = impl_->findGamepadByInstance(event.gbutton.which);
        if (slot >= 0) {
            int btn = static_cast<int>(event.gbutton.button);
            if (btn >= 0 && btn < MAX_GAMEPAD_BTNS)
                impl_->gamepads[slot].buttons_current[btn] = false;
        }
    } break;

    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        int slot = impl_->findGamepadByInstance(event.gaxis.which);
        if (slot >= 0) {
            int axis = static_cast<int>(event.gaxis.axis);
            if (axis >= 0 && axis < MAX_GAMEPAD_AXES) {
                // SDL axis range is -32768..32767 -- normalise to -1..1
                impl_->gamepads[slot].axes[axis] =
                    static_cast<float>(event.gaxis.value) / 32768.0f;
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

bool InputResource::keyPressed(Key key) const
{
    int k = static_cast<int>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return impl_->key_current[k] && !impl_->key_previous[k];
}

bool InputResource::keyHeld(Key key) const
{
    int k = static_cast<int>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return impl_->key_current[k];
}

bool InputResource::keyReleased(Key key) const
{
    int k = static_cast<int>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return !impl_->key_current[k] && impl_->key_previous[k];
}

// ---------------------------------------------------------------------------
// Mouse queries
// ---------------------------------------------------------------------------

Vec2 InputResource::mousePosition() const
{
    return impl_->mouse_pos;
}

Vec2 InputResource::mouseDelta() const
{
    return impl_->mouse_delta;
}

bool InputResource::mouseButtonPressed(MouseButton button) const
{
    int b = static_cast<int>(button);
    if (b < 1 || b > MAX_MOUSE_BTNS) return false;
    return impl_->mouse_buttons_current[b] && !impl_->mouse_buttons_previous[b];
}

bool InputResource::mouseButtonHeld(MouseButton button) const
{
    int b = static_cast<int>(button);
    if (b < 1 || b > MAX_MOUSE_BTNS) return false;
    return impl_->mouse_buttons_current[b];
}

bool InputResource::mouseButtonReleased(MouseButton button) const
{
    int b = static_cast<int>(button);
    if (b < 1 || b > MAX_MOUSE_BTNS) return false;
    return !impl_->mouse_buttons_current[b] && impl_->mouse_buttons_previous[b];
}

float InputResource::mouseWheelDelta() const
{
    return impl_->mouse_wheel_delta;
}

// ---------------------------------------------------------------------------
// Gamepad queries
// ---------------------------------------------------------------------------

bool InputResource::gamepadConnected(int32_t index) const
{
    if (index < 0 || index >= MAX_GAMEPADS) return false;
    return impl_->gamepads[index].gamepad != nullptr;
}

float InputResource::gamepadAxis(int32_t index, int32_t axis) const
{
    if (index < 0 || index >= MAX_GAMEPADS)     return 0.0f;
    if (axis  < 0 || axis  >= MAX_GAMEPAD_AXES) return 0.0f;
    if (!impl_->gamepads[index].gamepad)         return 0.0f;
    return impl_->gamepads[index].axes[axis];
}

bool InputResource::gamepadButtonPressed(int32_t index, int32_t button) const
{
    if (index  < 0 || index  >= MAX_GAMEPADS)     return false;
    if (button < 0 || button >= MAX_GAMEPAD_BTNS) return false;
    if (!impl_->gamepads[index].gamepad)           return false;
    return impl_->gamepads[index].buttons_current[button]
        && !impl_->gamepads[index].buttons_previous[button];
}

bool InputResource::gamepadButtonHeld(int32_t index, int32_t button) const
{
    if (index  < 0 || index  >= MAX_GAMEPADS)     return false;
    if (button < 0 || button >= MAX_GAMEPAD_BTNS) return false;
    if (!impl_->gamepads[index].gamepad)           return false;
    return impl_->gamepads[index].buttons_current[button];
}

bool InputResource::gamepadButtonReleased(int32_t index, int32_t button) const
{
    if (index  < 0 || index  >= MAX_GAMEPADS)     return false;
    if (button < 0 || button >= MAX_GAMEPAD_BTNS) return false;
    if (!impl_->gamepads[index].gamepad)           return false;
    return !impl_->gamepads[index].buttons_current[button]
        && impl_->gamepads[index].buttons_previous[button];
}

} // namespace drift
