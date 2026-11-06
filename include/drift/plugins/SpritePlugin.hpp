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

        // Extract system: query Sprite+Transform2D+Camera entities into snapshot
        app.addSystem("sprite_extract", Phase::Extract, [](App& a) {
            auto* snapshot = a.getResource<RenderSnapshot>();
            snapshot->extract(a.world(), a.world().componentRegistry());
            snapshot->swap();
        });

        // Auto-render system: sync cameras, then draw sprites from snapshot
        app.addSystem("sprite_render", Phase::Render, [this](App& a) {
            auto* snapshot = a.getResource<RenderSnapshot>();
            auto* renderer = a.getResource<RendererResource>();
            syncCameras(*renderer, snapshot->cameras());

            for (const auto& entry : snapshot->sprites()) {
                const Sprite& s = entry.sprite;
                if (!s.visible) continue;
                SpriteDraw d;
                d.texture = s.texture;
                d.position = entry.transform.position;
                d.srcRect = s.srcRect;
                d.scale = entry.transform.scale;
                d.rotation = entry.transform.rotation;
                d.origin = s.origin;
                d.tint = s.tint;
                d.flip = s.flip;
                d.zOrder = s.zOrder;
                d.blend = s.blend;
                d.material = s.material;
                d.layer = s.layer;
                renderer->draw(d);
            }
        });
    }
    DRIFT_PLUGIN(SpritePlugin)

private:
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
