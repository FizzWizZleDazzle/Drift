#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/RenderSnapshot.h>
#include <drift/resources/SpriteResource.hpp>
#include <drift/resources/RendererResource.hpp>

namespace drift {

class SpritePlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        app.addResource<SpriteResource>(*app.getResource<RendererResource>());

        // Extract system: query Sprite+Transform2D+Camera entities into snapshot
        app.addSystem("sprite_extract", Phase::Extract, [](App& a) {
            auto* snapshot = a.getResource<RenderSnapshot>();
            snapshot->extract(a.world());
            snapshot->swap();
        });

        // Auto-render system: apply camera, then draw sprites from snapshot
        app.addSystem("sprite_render", Phase::Render, [](App& a) {
            auto* snapshot = a.getResource<RenderSnapshot>();
            auto* renderer = a.getResource<RendererResource>();

            // Apply camera from snapshot
            const auto& cam = snapshot->camera();
            if (cam.found) {
                auto defaultCam = renderer->getDefaultCamera();
                renderer->setCameraPosition(defaultCam, cam.position);
                renderer->setCameraZoom(defaultCam, cam.zoom);
                renderer->setCameraRotation(defaultCam, cam.rotation);
            }

            // Draw all sprites
            for (const auto& entry : snapshot->sprites()) {
                if (!entry.sprite.visible) continue;
                renderer->drawSprite(
                    entry.sprite.texture,
                    entry.transform.position,
                    entry.sprite.srcRect,
                    entry.transform.scale,
                    entry.transform.rotation,
                    entry.sprite.origin,
                    entry.sprite.tint,
                    entry.sprite.flip,
                    entry.sprite.zOrder);
            }
        });
#endif
    }
    DRIFT_PLUGIN(SpritePlugin)
};

} // namespace drift
