#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/RenderSnapshot.hpp>
#include <drift/resources/SpriteResource.hpp>
#include <drift/resources/RendererResource.hpp>

#include <vector>

namespace drift {

class SpritePlugin : public Plugin {
public:
    void build(App& app) override {
        app.addResource<SpriteResource>(*app.getResource<RendererResource>());

        // Extract system: copy the cameras and place the renderer's cameras
        // from them (so every Render system sees this frame's cameras),
        // then copy the sprites some camera may show.
        app.addSystem("sprite_extract", Phase::Extract, [this](App& a) {
            auto* snapshot = a.getResource<RenderSnapshot>();
            auto* renderer = a.getResource<RendererResource>();
            snapshot->extract(
                a.world(), a.world().componentRegistry(),
                [&](const std::vector<CameraSnapshot>& cams) { syncCameras(*renderer, cams); },
                [&](const Transform2D& t, const Sprite& s) { return renderer->visible(spriteDraw(t, s)); });
            snapshot->swap();
        });

        // Auto-render system: draw the sprites of the snapshot.
        app.addSystem("sprite_render", Phase::Render, [](App& a) {
            auto* snapshot = a.getResource<RenderSnapshot>();
            auto* renderer = a.getResource<RendererResource>();
            for (const auto& entry : snapshot->sprites()) {
                renderer->draw(spriteDraw(entry.transform, entry.sprite));
            }
        });
    }
    DRIFT_PLUGIN(SpritePlugin)

private:
    static SpriteDraw spriteDraw(const Transform2D& t, const Sprite& s) {
        SpriteDraw d;
        d.texture = s.texture;
        d.position = t.position;
        d.srcRect = s.srcRect;
        d.scale = t.scale;
        d.rotation = t.rotation;
        d.origin = s.origin;
        d.tint = s.tint;
        d.flip = s.flip;
        d.zOrder = s.zOrder;
        d.blend = s.blend;
        d.material = s.material;
        d.layer = s.layer;
        return d;
    }

    // The first active camera that renders to the screen drives the
    // renderer's default camera (used by screenToWorld); every other active
    // camera gets its own renderer camera.
    void syncCameras(RendererResource& renderer, const std::vector<CameraSnapshot>& cams) {
        if (cams.empty()) return;
        size_t primary = 0;
        for (size_t i = 0; i < cams.size(); ++i) {
            if (!cams[i].camera.target.valid()) { primary = i; break; }
        }
        size_t used = 0;
        for (size_t i = 0; i < cams.size(); ++i) {
            const Camera& c = cams[i].camera;
            CameraDesc d;
            d.position = cams[i].position;
            d.zoom = c.zoom;
            d.rotation = c.rotation;
            d.viewport = c.viewport;
            d.order = c.order;
            d.layerMask = c.layerMask;
            d.target = c.target;
            d.clear = c.clear;
            d.clearColor = c.clearColor;
            if (i == primary) {
                renderer.setCamera(renderer.getDefaultCamera(), d);
                continue;
            }
            if (extra_.size() <= used) extra_.push_back(renderer.createCamera(d));
            renderer.setCamera(extra_[used++], d);
        }
        for (size_t j = used; j < extra_.size(); ++j) {
            CameraDesc d = renderer.camera(extra_[j]);
            d.enabled = false;
            renderer.setCamera(extra_[j], d);
        }
    }

    std::vector<CameraHandle> extra_;
};

} // namespace drift
