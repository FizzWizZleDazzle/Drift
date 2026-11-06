#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/gameplay/InputActions.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/Log.hpp>

namespace drift {

inline void input_actions_begin_frame(ResMut<InputActions> actions) { actions->beginFrame(); }

// Adds the InputActions resource. Action state follows InputResource as
// events arrive, so every Update system sees this frame's input. Add it
// after InputPlugin.
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
    }
    DRIFT_PLUGIN(InputActionsPlugin)
};

} // namespace drift
