#pragma once

#include <drift/Plugin.h>
#include <drift/App.h>
#include <drift/resources/RendererResource.h>

namespace drift {

#ifndef SWIG
inline void renderer_begin_frame(ResMut<RendererResource> r, float) { r->beginFrame(); }
inline void renderer_end_frame(ResMut<RendererResource> r, float) { r->endFrame(); r->present(); }
#endif

class RendererPlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        app.addResource<RendererResource>(app);
        app.addSystem<renderer_begin_frame>("renderer_begin", Phase::PreUpdate);
        app.addSystem<renderer_end_frame>("renderer_end", Phase::Render);
#endif
    }
    DRIFT_PLUGIN(RendererPlugin)
};

} // namespace drift
