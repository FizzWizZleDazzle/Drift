#include <drift/gameplay/InputActions.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/Log.hpp>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

constexpr int kMaxGamepads = 4;
constexpr int kMaxKeys = static_cast<int>(Key::Max_);

constexpr const char* kButtonNames[] = {
    "South", "East", "West", "North", "Back", "Guide", "Start",
    "LeftStick", "RightStick", "LeftShoulder", "RightShoulder",
    "DPadUp", "DPadDown", "DPadLeft", "DPadRight",
};
constexpr int kButtonCount = sizeof(kButtonNames) / sizeof(kButtonNames[0]);

constexpr const char* kAxisNames[] = {
    "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger",
};
constexpr int kAxisCount = sizeof(kAxisNames) / sizeof(kAxisNames[0]);

constexpr const char* kMouseNames[] = {"", "Left", "Middle", "Right", "X1", "X2"};
constexpr int kMouseCount = sizeof(kMouseNames) / sizeof(kMouseNames[0]);

int indexOf(const char* const* names, int count, std::string_view s) {
    for (int i = 0; i < count; ++i) if (s == names[i]) return i;
    return -1;
}

const std::vector<InputBinding> kEmpty;

} // namespace

// ---------------------------------------------------------------------------
// InputBinding text form
// ---------------------------------------------------------------------------

std::string InputBinding::toString() const {
    switch (source) {
        case Source::None: return "";
        case Source::Key: {
            const char* n = SDL_GetScancodeName(static_cast<SDL_Scancode>(code));
            if (n && *n) return std::string("key:") + n;
            return "key:#" + std::to_string(code);
        }
        case Source::Mouse:
            if (code > 0 && code < kMouseCount) return std::string("mouse:") + kMouseNames[code];
            return "mouse:#" + std::to_string(code);
        case Source::GamepadButton:
            if (code >= 0 && code < kButtonCount) return std::string("button:") + kButtonNames[code];
            return "button:#" + std::to_string(code);
        case Source::AxisPositive:
        case Source::AxisNegative: {
            std::string s = source == Source::AxisPositive ? "axis:+" : "axis:-";
            if (code >= 0 && code < kAxisCount) return s + kAxisNames[code];
            return s + "#" + std::to_string(code);
        }
    }
    return "";
}

InputBinding InputBinding::parse(std::string_view text) {
    size_t colon = text.find(':');
    if (colon == std::string_view::npos) return {};
    std::string_view kind = text.substr(0, colon);
    std::string_view rest = text.substr(colon + 1);

    auto numeric = [](std::string_view s, int32_t& out) {
        if (s.size() < 2 || s[0] != '#') return false;
        int32_t v = 0;
        for (char c : s.substr(1)) {
            if (c < '0' || c > '9') return false;
            v = v * 10 + (c - '0');
            if (v > 1 << 20) return false;
        }
        out = v;
        return true;
    };

    int32_t n = 0;
    if (kind == "key") {
        if (numeric(rest, n)) return n < kMaxKeys ? InputBinding{Source::Key, n} : InputBinding{};
        std::string name(rest);
        SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
        if (sc == SDL_SCANCODE_UNKNOWN || static_cast<int>(sc) >= kMaxKeys) return {};
        return {Source::Key, static_cast<int32_t>(sc)};
    }
    if (kind == "mouse") {
        if (numeric(rest, n)) return {Source::Mouse, n};
        int i = indexOf(kMouseNames, kMouseCount, rest);
        return i > 0 ? InputBinding{Source::Mouse, i} : InputBinding{};
    }
    if (kind == "button") {
        if (numeric(rest, n)) return {Source::GamepadButton, n};
        int i = indexOf(kButtonNames, kButtonCount, rest);
        return i >= 0 ? InputBinding{Source::GamepadButton, i} : InputBinding{};
    }
    if (kind == "axis" && rest.size() > 1 && (rest[0] == '+' || rest[0] == '-')) {
        Source src = rest[0] == '+' ? Source::AxisPositive : Source::AxisNegative;
        rest = rest.substr(1);
        if (numeric(rest, n)) return {src, n};
        int i = indexOf(kAxisNames, kAxisCount, rest);
        return i >= 0 ? InputBinding{src, i} : InputBinding{};
    }
    return {};
}

// ---------------------------------------------------------------------------
// Definition and rebinding
// ---------------------------------------------------------------------------

InputActions::Control& InputActions::control(std::string_view name, Kind kind) {
    auto it = controls_.find(name);
    if (it == controls_.end()) {
        it = controls_.emplace(std::string(name), Control{}).first;
        it->second.kind = kind;
    } else if (it->second.kind != kind) {
        DRIFT_LOG_WARN("InputActions: '%.*s' redefined as a different kind; old bindings dropped",
                       static_cast<int>(name.size()), name.data());
        it->second = Control{};
        it->second.kind = kind;
    }
    return it->second;
}

const InputActions::Control* InputActions::find(std::string_view name) const {
    auto it = controls_.find(name);
    return it == controls_.end() ? nullptr : &it->second;
}

void InputActions::bindAction(std::string_view action, InputBinding b) {
    Control& c = control(action, Kind::Button);
    if (b.valid()) c.slots[0].push_back(b);
    if (source_) evaluate(c, *source_);
}

void InputActions::bindAxis(std::string_view axis, InputBinding negative, InputBinding positive) {
    Control& c = control(axis, Kind::Axis1D);
    if (negative.valid()) c.slots[AxisSlot::Negative].push_back(negative);
    if (positive.valid()) c.slots[AxisSlot::Positive].push_back(positive);
    if (source_) evaluate(c, *source_);
}

void InputActions::bindAxis2D(std::string_view axis, InputBinding left, InputBinding right,
                              InputBinding up, InputBinding down) {
    Control& c = control(axis, Kind::Axis2D);
    const InputBinding in[4] = {left, right, up, down};
    for (int i = 0; i < 4; ++i) if (in[i].valid()) c.slots[i].push_back(in[i]);
    if (source_) evaluate(c, *source_);
}

void InputActions::bindStick(std::string_view axis, GamepadAxis x, GamepadAxis y) {
    bindAxis2D(axis, InputBinding::axis(x, false), InputBinding::axis(x, true),
               InputBinding::axis(y, false), InputBinding::axis(y, true));
}

void InputActions::setBindings(std::string_view name, int slot, std::vector<InputBinding> list) {
    auto it = controls_.find(name);
    if (it == controls_.end() || slot < 0 || slot >= it->second.slotCount()) return;
    list.erase(std::remove_if(list.begin(), list.end(), [](const InputBinding& b) { return !b.valid(); }),
               list.end());
    it->second.slots[slot] = std::move(list);
    if (source_) evaluate(it->second, *source_);
}

const std::vector<InputBinding>& InputActions::bindings(std::string_view name, int slot) const {
    const Control* c = find(name);
    if (!c || slot < 0 || slot >= c->slotCount()) return kEmpty;
    return c->slots[slot];
}

void InputActions::rebind(std::string_view name, int slot, size_t index, InputBinding binding) {
    auto it = controls_.find(name);
    if (it == controls_.end() || slot < 0 || slot >= it->second.slotCount() || !binding.valid()) return;
    auto& list = it->second.slots[slot];
    if (index < list.size()) list[index] = binding;
    else list.push_back(binding);
    if (source_) evaluate(it->second, *source_);
}

void InputActions::remove(std::string_view name) {
    auto it = controls_.find(name);
    if (it != controls_.end()) controls_.erase(it);
}

bool InputActions::has(std::string_view name) const { return find(name) != nullptr; }

std::vector<std::string> InputActions::names() const {
    std::vector<std::string> out;
    out.reserve(controls_.size());
    for (auto& [k, v] : controls_) out.push_back(k);
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<InputBinding> InputActions::captureNext(const InputResource& input) const {
    for (int k = 1; k < kMaxKeys; ++k) {
        if (input.keyPressed(static_cast<Key>(k))) return InputBinding{InputBinding::Source::Key, k};
    }
    for (int b = 1; b < kMouseCount; ++b) {
        if (input.mouseButtonPressed(static_cast<MouseButton>(b))) return InputBinding{InputBinding::Source::Mouse, b};
    }
    for (int p = 0; p < kMaxGamepads; ++p) {
        if (gamepad >= 0 && p != gamepad) continue;
        if (!input.gamepadConnected(p)) continue;
        for (int b = 0; b < kButtonCount; ++b) {
            if (input.gamepadButtonPressed(p, b)) return InputBinding{InputBinding::Source::GamepadButton, b};
        }
        for (int a = 0; a < kAxisCount; ++a) {
            float v = input.gamepadAxis(p, a);
            if (v > 0.5f) return InputBinding::axis(static_cast<GamepadAxis>(a), true);
            if (v < -0.5f) return InputBinding::axis(static_cast<GamepadAxis>(a), false);
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

float InputActions::raw(const InputResource& input, const InputBinding& b) const {
    switch (b.source) {
        case InputBinding::Source::None: return 0.f;
        case InputBinding::Source::Key:
            return input.keyHeld(static_cast<Key>(b.code)) ? 1.f : 0.f;
        case InputBinding::Source::Mouse:
            return input.mouseButtonHeld(static_cast<MouseButton>(b.code)) ? 1.f : 0.f;
        case InputBinding::Source::GamepadButton:
        case InputBinding::Source::AxisPositive:
        case InputBinding::Source::AxisNegative: {
            float best = 0.f;
            for (int p = 0; p < kMaxGamepads; ++p) {
                if (gamepad >= 0 && p != gamepad) continue;
                if (!input.gamepadConnected(p)) continue;
                float v;
                if (b.source == InputBinding::Source::GamepadButton) {
                    v = input.gamepadButtonHeld(p, b.code) ? 1.f : 0.f;
                } else {
                    float a = input.gamepadAxis(p, b.code);
                    v = b.source == InputBinding::Source::AxisPositive ? a : -a;
                }
                best = std::max(best, v);
            }
            return std::clamp(best, 0.f, 1.f);
        }
    }
    return 0.f;
}

float InputActions::slotValue(const InputResource& input, const std::vector<InputBinding>& slot,
                              bool applyDeadZone) const {
    float best = 0.f;
    for (const auto& b : slot) {
        float v = raw(input, b);
        bool analog = b.source == InputBinding::Source::AxisPositive ||
                      b.source == InputBinding::Source::AxisNegative;
        if (analog && applyDeadZone) {
            v = v < deadZone ? 0.f : (v - deadZone) / std::max(1e-6f, 1.f - deadZone);
        }
        best = std::max(best, v);
    }
    return best;
}

void InputActions::evaluate(Control& c, const InputResource& input) const {
    switch (c.kind) {
        case Kind::Button:
            c.value = slotValue(input, c.slots[0], true);
            c.vec = {};
            c.held = c.value >= pressThreshold;
            break;
        case Kind::Axis1D:
            c.value = std::clamp(slotValue(input, c.slots[AxisSlot::Positive], true) -
                                 slotValue(input, c.slots[AxisSlot::Negative], true), -1.f, 1.f);
            c.vec = {};
            c.held = std::fabs(c.value) >= pressThreshold;
            break;
        case Kind::Axis2D: {
            // Raw vector first, then a radial dead zone, so diagonals on a
            // stick are not cut off the way per-axis dead zones would.
            Vec2 v{slotValue(input, c.slots[AxisSlot::Right], false) - slotValue(input, c.slots[AxisSlot::Left], false),
                   slotValue(input, c.slots[AxisSlot::Down], false) - slotValue(input, c.slots[AxisSlot::Up], false)};
            float len = v.length();
            if (len < deadZone || len == 0.f) {
                v = {};
                len = 0.f;
            } else {
                float scaled = std::min(1.f, (len - deadZone) / std::max(1e-6f, 1.f - deadZone));
                v = v * (scaled / len);
                len = scaled;
            }
            c.vec = v;
            c.value = len;
            c.held = len >= pressThreshold;
            break;
        }
    }
}

void InputActions::beginFrame() {
    for (auto& [name, c] : controls_) c.wasHeld = c.held;
}

void InputActions::refresh(const InputResource& input) {
    for (auto& [name, c] : controls_) evaluate(c, input);
}

void InputActions::processEvent(const SDL_Event&) {
    if (source_) refresh(*source_);
}

bool InputActions::pressed(std::string_view name) const {
    const Control* c = find(name);
    return c && c->held && !c->wasHeld;
}

bool InputActions::held(std::string_view name) const {
    const Control* c = find(name);
    return c && c->held;
}

bool InputActions::released(std::string_view name) const {
    const Control* c = find(name);
    return c && !c->held && c->wasHeld;
}

float InputActions::value(std::string_view name) const {
    const Control* c = find(name);
    return c ? c->value : 0.f;
}

float InputActions::axis(std::string_view name) const {
    const Control* c = find(name);
    if (!c) return 0.f;
    return c->kind == Kind::Axis2D ? c->vec.x : c->value;
}

Vec2 InputActions::axis2D(std::string_view name) const {
    const Control* c = find(name);
    if (!c) return {};
    if (c->kind == Kind::Axis2D) return c->vec;
    return {c->value, 0.f};
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

nlohmann::json InputActions::toJson() const {
    nlohmann::json j = nlohmann::json::object();
    for (auto& [name, c] : controls_) {
        nlohmann::json slots = nlohmann::json::array();
        for (int s = 0; s < c.slotCount(); ++s) {
            nlohmann::json list = nlohmann::json::array();
            for (const auto& b : c.slots[s]) list.push_back(b.toString());
            slots.push_back(std::move(list));
        }
        j[name] = std::move(slots);
    }
    return j;
}

int InputActions::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) return 0;
    int updated = 0;
    for (auto it = j.begin(); it != j.end(); ++it) {
        auto cit = controls_.find(it.key());
        if (cit == controls_.end()) continue;
        Control& c = cit->second;
        const auto& slots = it.value();
        if (!slots.is_array() || static_cast<int>(slots.size()) != c.slotCount()) continue;
        std::vector<InputBinding> parsed[4];
        bool ok = true;
        for (int s = 0; s < c.slotCount() && ok; ++s) {
            if (!slots[s].is_array()) { ok = false; break; }
            for (const auto& e : slots[s]) {
                if (!e.is_string()) continue;
                InputBinding b = InputBinding::parse(e.get<std::string>());
                if (b.valid()) parsed[s].push_back(b);
            }
        }
        if (!ok) continue;
        for (int s = 0; s < c.slotCount(); ++s) c.slots[s] = std::move(parsed[s]);
        if (source_) evaluate(c, *source_);
        ++updated;
    }
    return updated;
}

} // namespace drift
