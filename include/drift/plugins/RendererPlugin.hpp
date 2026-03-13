#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/resources/RendererResource.hpp>

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
        app.addSystem<renderer_end_frame>("renderer_end", Phase::RenderFlush);
#endif
    }
    DRIFT_PLUGIN(RendererPlugin)
};

} // namespace drift
