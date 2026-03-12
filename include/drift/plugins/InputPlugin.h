#pragma once

#include <drift/Plugin.h>
#include <drift/App.h>
#include <drift/resources/InputResource.h>

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
