#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/resources/InputResource.hpp>

namespace drift {

#ifndef SWIG
inline void input_begin_frame(ResMut<InputResource> input, float) { input->beginFrame(); }
#endif

class InputPlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        auto* input = app.addResource<InputResource>();
        app.addEventHandler(input);
        app.addSystem<input_begin_frame>("input_begin_frame", Phase::PreUpdate);
#endif
    }
    DRIFT_PLUGIN(InputPlugin)
};

} // namespace drift
