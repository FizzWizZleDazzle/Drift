#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/resources/InputResource.hpp>

namespace drift {

inline void input_begin_frame(ResMut<InputResource> input) { input->beginFrame(); }

class InputPlugin : public Plugin {
public:
    void build(App& app) override {
        auto* input = app.addResource<InputResource>();
        app.addEventHandler(input);

        // Wire up logical resolution scaling for mouse coordinates
        const auto& cfg = app.config();
        float lw = static_cast<float>(cfg.width > 0 ? cfg.width : 1280);
        float lh = static_cast<float>(cfg.height > 0 ? cfg.height : 720);
        input->setLogicalSize(lw, lh);
        input->setWindow(app.sdlWindow());

        app.addSystem<input_begin_frame>("input_begin_frame", Phase::PreUpdate);
    }
    DRIFT_PLUGIN(InputPlugin)
};

} // namespace drift
