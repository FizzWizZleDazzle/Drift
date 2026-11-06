#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/resources/UIResource.hpp>

namespace drift {

inline void ui_begin_frame(ResMut<UIResource> ui) { ui->beginFrame(); }
inline void ui_end_frame(ResMut<UIResource> ui) { ui->endFrame(); }

class UIPlugin : public Plugin {
public:
    void build(App& app) override {
        auto* ui = app.addResource<UIResource>(app);
        app.addEventHandler(ui);
        app.addSystem<ui_begin_frame>("ui_begin", Phase::PreUpdate);
#ifdef DRIFT_DEV
        app.addSystem("dev_overlay", Phase::Render, [](App& a) {
            if (auto* r = a.getResource<UIResource>()) r->devRender();
        });
#endif
        app.addSystem<ui_end_frame>("ui_end", Phase::Render);
    }
    DRIFT_PLUGIN(UIPlugin)
};

} // namespace drift
