#pragma once

#include <functional>
#include <string>
#include <vector>

#include <drift/Resource.hpp>
#include <drift/App.hpp>
#include <drift/Types.hpp>

union SDL_Event;

namespace drift {

// One finger on a touch screen, in logical screen coordinates. A touch
// stays in touches() for the frame it ended in, with `released` set.
struct Touch {
    int64_t id = 0;        // stable while the finger is down
    Vec2 position;
    Vec2 start;            // where it went down
    Vec2 delta;            // movement this frame
    bool pressed = false;  // went down this frame
    bool released = false; // went up this frame
};

class InputResource : public Resource, public EventHandler {
public:
    InputResource();
    ~InputResource() override;

    DRIFT_RESOURCE(InputResource)

    // Called each frame before polling events
    void beginFrame();
    void processEvent(const SDL_Event& event) override;

    // Keyboard. Pressed and released are true in the frame the key went
    // down or up, even when both happened inside one frame; the same holds
    // for mouse and gamepad buttons.
    bool keyPressed(Key key) const;
    bool keyHeld(Key key) const;
    bool keyReleased(Key key) const;

    // Logical resolution scaling (called by App after window creation)
    // Maps window coordinates to logical screen coordinates for mouse queries.
    void setCoordinateMapper(std::function<Vec2(Vec2)> windowToLogical);

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

    // Text typed this frame (UTF-8), while text input is on. Starting text
    // input also brings up the on-screen keyboard where there is one.
    void startTextInput();
    void stopTextInput();
    bool textInputActive() const;
    const std::string& textInput() const;

    // Fingers on a touch screen. Browsers and SDL also turn the first
    // finger into mouse events, so a game that reads the mouse works on
    // touch screens too.
    const std::vector<Touch>& touches() const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
