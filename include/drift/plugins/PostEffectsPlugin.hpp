#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/effects/PostEffects.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>

namespace drift {

inline void post_effects_tick(Res<Time> time, ResMut<PostEffects> effects) {
    effects->tick(time->delta);
}

// Adds the PostEffects resource: bloom, color grading, blur, pixelate,
// grayscale, sepia, vignette, CRT and screen fades, each enabled with one
// add() call. Effects cost nothing until added.
class PostEffectsPlugin : public Plugin {
public:
    void build(App& app) override {
        auto* renderer = app.getResource<RendererResource>();
        if (!renderer) return;
        app.addResource<PostEffects>(*renderer);
        app.addSystem<post_effects_tick>("post_effects_tick", Phase::PostUpdate);
    }
    DRIFT_PLUGIN(PostEffectsPlugin)
};

} // namespace drift
