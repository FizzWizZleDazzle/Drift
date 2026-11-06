#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/RendererResource.hpp>

namespace drift {

inline void input_begin_frame(ResMut<InputResource> input) { input->beginFrame(); }

class InputPlugin : public Plugin {
public:
    void build(App& app) override {
        auto* input = app.addResource<InputResource>();
        app.addEventHandler(input);

        app.addSystem<input_begin_frame>("input_begin_frame", Phase::PreUpdate);
    }

    // Mouse positions follow the renderer's scale mode (letterbox bars,
    // integer scaling) when a renderer is present.
    void finish(App& app) override {
        auto* input = app.getResource<InputResource>();
        auto* renderer = app.getResource<RendererResource>();
        if (input && renderer) {
            input->setCoordinateMapper([renderer](Vec2 p) { return renderer->windowToLogical(p); });
        }
    }
    DRIFT_PLUGIN(InputPlugin)
};

} // namespace drift
