#pragma once

#include <drift/Plugin.h>
#include <drift/App.h>
#include <drift/resources/UIResource.h>

namespace drift {

#ifndef SWIG
inline void ui_begin_frame(ResMut<UIResource> ui, float) { ui->beginFrame(); }
inline void ui_end_frame(ResMut<UIResource> ui, float) { ui->endFrame(); ui->render(); }
#endif

class UIPlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        auto* ui = app.addResource<UIResource>(app);
        app.addEventHandler(ui);
        app.addSystem<ui_begin_frame>("ui_begin", Phase::PreUpdate);
        app.addSystem<ui_end_frame>("ui_end", Phase::Render);
#endif
    }
    DRIFT_PLUGIN(UIPlugin)
};

} // namespace drift
