#pragma once

// Named input actions and axes on top of InputResource. Game code asks for
// "jump" or "move" instead of Key::Space; players can rebind at runtime and
// the bindings round-trip through JSON (store them with SaveData).
//
//   actions->bindAction("jump", InputBinding::key(Key::Space));
//   actions->bindAction("jump", InputBinding::button(GamepadButton::South));
//   actions->bindAxis2D("move", InputBinding::key(Key::A), InputBinding::key(Key::D),
//                               InputBinding::key(Key::W), InputBinding::key(Key::S));
//   ...
//   if (actions->pressed("jump")) ...
//   Vec2 move = actions->axis2D("move");

#include <drift/Types.hpp>
#include <drift/Resource.hpp>
#include <drift/App.hpp>

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace drift {

class InputResource;

// Mirrors SDL_GamepadButton.
enum class GamepadButton : uint8_t {
    South = 0, East, West, North,        // A/B/X/Y on Xbox layout
    Back, Guide, Start,
    LeftStick, RightStick,
    LeftShoulder, RightShoulder,
    DPadUp, DPadDown, DPadLeft, DPadRight,
};

// Mirrors SDL_GamepadAxis. Sticks are -1..1 (Y down), triggers 0..1.
enum class GamepadAxis : uint8_t {
    LeftX = 0, LeftY, RightX, RightY, LeftTrigger, RightTrigger,
};

// One physical input. Every binding reads as a value in 0..1: keys and
// buttons are 0 or 1, a gamepad axis binding reads one half of the axis.
struct InputBinding {
    enum class Source : uint8_t { None, Key, Mouse, GamepadButton, AxisPositive, AxisNegative };
    Source source = Source::None;
    int32_t code = 0;

    static InputBinding key(Key k) { return {Source::Key, static_cast<int32_t>(k)}; }
    static InputBinding mouse(MouseButton b) { return {Source::Mouse, static_cast<int32_t>(b)}; }
    static InputBinding button(GamepadButton b) { return {Source::GamepadButton, static_cast<int32_t>(b)}; }
    // The positive (right / down / pressed trigger) or negative half of an axis.
    static InputBinding axis(GamepadAxis a, bool positive = true) {
        return {positive ? Source::AxisPositive : Source::AxisNegative, static_cast<int32_t>(a)};
    }

    bool valid() const { return source != Source::None; }
    bool operator==(const InputBinding& o) const { return source == o.source && code == o.code; }

    // Stable text form used in saved bindings: "key:Space", "mouse:Left",
    // "button:South", "axis:+LeftX". Also fine to show in a rebinding menu.
    std::string toString() const;
    // Parses toString() output; returns an invalid binding on failure.
    static InputBinding parse(std::string_view text);
};

// Slot indices for setBindings()/bindings() on axes.
namespace AxisSlot {
    constexpr int Negative = 0, Positive = 1;              // 1D axes
    constexpr int Left = 0, Right = 1, Up = 2, Down = 3;   // 2D axes
}

class InputActions : public Resource, public EventHandler {
public:
    DRIFT_RESOURCE(InputActions)

    // ---- Defining actions and axes (adds to existing bindings) ----
    void bindAction(std::string_view action, InputBinding binding);
    // 1D axis: negative side reads -1, positive side +1.
    void bindAxis(std::string_view axis, InputBinding negative, InputBinding positive);
    // 2D axis in screen orientation: up is -Y, down is +Y. Pass an invalid
    // InputBinding{} for a direction you do not want to bind.
    void bindAxis2D(std::string_view axis, InputBinding left, InputBinding right,
                    InputBinding up, InputBinding down);
    // Shorthand: bind a whole stick to a 2D axis.
    void bindStick(std::string_view axis, GamepadAxis x, GamepadAxis y);

    // ---- Rebinding ----
    // Replace the bindings in one slot (0 for actions; AxisSlot for axes).
    void setBindings(std::string_view name, int slot, std::vector<InputBinding> bindings);
    const std::vector<InputBinding>& bindings(std::string_view name, int slot = 0) const;
    // Replace binding `index` in a slot, or append when index is past the end.
    void rebind(std::string_view name, int slot, size_t index, InputBinding binding);
    void remove(std::string_view name);
    bool has(std::string_view name) const;
    std::vector<std::string> names() const;

    // The first key, mouse button, gamepad button or axis (past 0.5) that
    // went down this frame, for "press a key to rebind" menus.
    std::optional<InputBinding> captureNext(const InputResource& input) const;

    // ---- Queries (actions and axes alike) ----
    // An axis counts as held when its magnitude passes pressThreshold, so
    // axes also work as menu navigation buttons.
    bool pressed(std::string_view name) const;
    bool held(std::string_view name) const;
    bool released(std::string_view name) const;
    // Actions: 0..1. 1D axes: -1..1. 2D axes: length of the vector.
    float value(std::string_view name) const;
    float axis(std::string_view name) const;
    Vec2 axis2D(std::string_view name) const;

    // ---- Settings ----
    float deadZone = 0.2f;        // stick values below this read as 0
    float pressThreshold = 0.5f;  // analog value at which an action is held
    int32_t gamepad = -1;         // gamepad slot to read; -1 = any connected

    // ---- Persistence ----
    // {"jump": [["key:Space", "button:South"]], "move": [[...], [...], [...], [...]]}
    nlohmann::json toJson() const;
    // Applies bindings for names that already exist with the same slot
    // count; unknown names and malformed entries are ignored. Returns the
    // number of controls updated.
    int fromJson(const nlohmann::json& j);

    // ---- Driving ----
    // InputActionsPlugin calls these; call them yourself when using an
    // InputActions outside the plugin (several maps for local multiplayer).
    void setSource(const InputResource* input) { source_ = input; }
    void beginFrame();                               // before events are polled
    void refresh(const InputResource& input);        // re-read current values
    void processEvent(const SDL_Event& event) override;

private:
    enum class Kind : uint8_t { Button, Axis1D, Axis2D };
    struct Control {
        Kind kind = Kind::Button;
        std::vector<InputBinding> slots[4];
        float value = 0.f;
        Vec2 vec;
        bool held = false;
        bool wasHeld = false;
        int slotCount() const { return kind == Kind::Button ? 1 : (kind == Kind::Axis1D ? 2 : 4); }
    };

    Control& control(std::string_view name, Kind kind);
    const Control* find(std::string_view name) const;
    float raw(const InputResource& input, const InputBinding& b) const;
    float slotValue(const InputResource& input, const std::vector<InputBinding>& slot, bool applyDeadZone) const;
    void evaluate(Control& c, const InputResource& input) const;

    struct NameHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };
    std::unordered_map<std::string, Control, NameHash, std::equal_to<>> controls_;
    const InputResource* source_ = nullptr;
};

} // namespace drift
