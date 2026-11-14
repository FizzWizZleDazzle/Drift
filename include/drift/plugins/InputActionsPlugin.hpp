#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/gameplay/InputActions.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/Log.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace drift {

inline void input_actions_begin_frame(ResMut<InputActions> actions) { actions->beginFrame(); }

// Adds the InputActions resource. Action state follows InputResource as
// events arrive, so every Update system sees this frame's input. Add it
// after InputPlugin.
//
// The [input] section of drift.toml binds actions, one key per action with
// a list of bindings in their text form:
//   [input]
//   jump = ["key:Space", "button:South"]
// Saving drift.toml rebinds the actions it names.
class InputActionsPlugin : public Plugin {
public:
    void build(App& app) override {
        auto* actions = app.initResource<InputActions>();
        auto* input = app.getResource<InputResource>();
        if (!input) {
            DRIFT_LOG_WARN("InputActionsPlugin: InputResource missing; add InputPlugin first");
            return;
        }
        actions->setSource(input);
        // Registered after InputPlugin's handler, so InputResource has
        // already applied each event when actions re-read it.
        app.addEventHandler(actions);
        app.addSystem<input_actions_begin_frame>("input_actions_begin_frame", Phase::PreUpdate);

        Settings& settings = app.settingsStore();
        bindFromSettings(*actions, settings, {});
        settings.onChange("input", [actions, &settings](const std::vector<std::string>& keys) {
            bindFromSettings(*actions, settings, keys);
        });
    }

    // Binds the actions of [input]; `only` limits it to those names (all
    // when empty). Each named action's bindings are replaced.
    static void bindFromSettings(InputActions& actions, const Settings& settings,
                                 const std::vector<std::string>& only) {
        std::string text = settings.valueJson("input");
        if (text.empty()) return;
        nlohmann::json section = nlohmann::json::parse(text, nullptr, false);
        if (!section.is_object()) return;
        for (const auto& [name, list] : section.items()) {
            if (!only.empty() && std::find(only.begin(), only.end(), name) == only.end()) continue;
            if (actions.has(name)) actions.remove(name);
            nlohmann::json bindings = list.is_array() ? list : nlohmann::json::array({list});
            for (const auto& b : bindings) {
                InputBinding binding = b.is_string() ? InputBinding::parse(b.get<std::string>()) : InputBinding{};
                if (!binding.valid()) {
                    settings.report("input." + name, "input." + name + ": " + b.dump() +
                                                         " is not a binding; write one such as \"key:Space\"");
                    continue;
                }
                actions.bindAction(name, binding);
            }
        }
        for (const auto& name : only) {
            if (!section.contains(name) && actions.has(name)) actions.remove(name);
        }
    }
    DRIFT_PLUGIN(InputActionsPlugin)
};

} // namespace drift
