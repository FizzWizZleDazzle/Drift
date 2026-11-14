#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/DebugViews.hpp>
#include <drift/resources/RendererResource.hpp>

#include <algorithm>

namespace drift {

inline void renderer_begin_frame(ResMut<RendererResource> r) { r->beginFrame(); }
inline void renderer_end_frame(ResMut<RendererResource> r) { r->endFrame(); }

class RendererPlugin : public Plugin {
public:
    void build(App& app) override {
        auto* renderer = app.addResource<RendererResource>(app);
        auto* assets = app.getResource<AssetServer>();
        assets->setTextureLoader([renderer](const char* path) {
            return renderer->loadTexture(path);
        });
        assets->setReloader("texture", [renderer](uint32_t id, const std::string& path) {
            return renderer->reloadTexture(TextureHandle{id}, path.c_str());
        });
        // Debug views: plugins add theirs; this one outlines what entities show.
        auto* views = app.initResource<DebugViews>();
        views->add("bounds", "Outlines what each entity draws and marks its position",
                   [](App& a, RendererResource& r) {
            const Color line{255, 235, 80, 255}, pivot{255, 80, 220, 255};
            a.getResource<DebugViews>()->forEachOutline(a, [&](const DebugOutline& o) {
                uint8_t layer = o.layer == ScreenLayer ? ScreenLayer : OverlayLayer;
                float x0 = o.corners[0].x, y0 = o.corners[0].y, x1 = x0, y1 = y0;
                for (const Vec2& c : o.corners) {
                    x0 = std::min(x0, c.x); y0 = std::min(y0, c.y);
                    x1 = std::max(x1, c.x); y1 = std::max(y1, c.y);
                }
                if (!r.visible(Rect{x0, y0, x1 - x0, y1 - y0}, layer)) return;
                r.setLayer(layer);
                r.drawPolyline(o.corners, 4, line, 1.5f, true, 1e6f);
                r.drawLine({o.pivot.x - 6.f, o.pivot.y}, {o.pivot.x + 6.f, o.pivot.y}, pivot, 2.f, 1e6f);
                r.drawLine({o.pivot.x, o.pivot.y - 6.f}, {o.pivot.x, o.pivot.y + 6.f}, pivot, 2.f, 1e6f);
            });
        });
        app.addSystem("debug_views", Phase::Render, [](App& a) {
            auto* r = a.getResource<RendererResource>();
            if (r) a.getResource<DebugViews>()->draw(a, *r);
        });
        app.addSystem<renderer_begin_frame>("renderer_begin", Phase::PreUpdate);
        app.addSystem<renderer_end_frame>("renderer_end", Phase::RenderFlush);
    }
    DRIFT_PLUGIN(RendererPlugin)
};

} // namespace drift
