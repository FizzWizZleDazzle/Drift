#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/resources/RendererResource.hpp>

namespace drift {

inline void renderer_begin_frame(ResMut<RendererResource> r) { r->beginFrame(); }
inline void renderer_end_frame(ResMut<RendererResource> r) { r->endFrame(); r->present(); }

class RendererPlugin : public Plugin {
public:
    void build(App& app) override {
        auto* renderer = app.addResource<RendererResource>(app);
        auto* assets = app.getResource<AssetServer>();
        assets->setTextureLoader([renderer](const char* path) {
            return renderer->loadTexture(path);
        });
        app.addSystem<renderer_begin_frame>("renderer_begin", Phase::PreUpdate);
        app.addSystem<renderer_end_frame>("renderer_end", Phase::RenderFlush);
    }
    DRIFT_PLUGIN(RendererPlugin)
};

} // namespace drift
