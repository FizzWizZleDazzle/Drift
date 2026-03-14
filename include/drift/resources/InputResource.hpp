#pragma once

#include <drift/Resource.hpp>
#include <drift/App.hpp>
#include <drift/Types.hpp>

union SDL_Event;

namespace drift {

class InputResource : public Resource, public EventHandler {
public:
    InputResource();
    ~InputResource() override;

    DRIFT_RESOURCE(InputResource)

    // Called each frame before polling events
    void beginFrame();
    void processEvent(const SDL_Event& event) override;

    // Keyboard
    bool keyPressed(Key key) const;
    bool keyHeld(Key key) const;
    bool keyReleased(Key key) const;

    // Mouse
    Vec2 mousePosition() const;
    Vec2 mouseDelta() const;
    bool mouseButtonPressed(MouseButton button) const;
    bool mouseButtonHeld(MouseButton button) const;
    bool mouseButtonReleased(MouseButton button) const;
    float mouseWheelDelta() const;

    // Gamepad
    bool gamepadConnected(int32_t index) const;
    float gamepadAxis(int32_t index, int32_t axis) const;
    bool gamepadButtonPressed(int32_t index, int32_t button) const;
    bool gamepadButtonHeld(int32_t index, int32_t button) const;
    bool gamepadButtonReleased(int32_t index, int32_t button) const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
