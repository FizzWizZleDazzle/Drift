#pragma once

#include <drift/Plugin.hpp>
#include <drift/Resource.hpp>
#include <drift/Reflect.hpp>
#include <drift/Types.hpp>
#include <drift/components/Light2D.hpp>

namespace drift {

// Scene-wide lighting options, the [lighting] section of drift.toml. Edit
// them from any system.
struct LightingSettings : public Resource {
    DRIFT_RESOURCE(LightingSettings)

    bool     enabled = true;
    ColorF   ambient = {0.08f, 0.08f, 0.12f, 1.f};   // light everywhere, before any light adds to it
    uint32_t layerMask = AllWorldLayers;             // cameras whose layerMask shares a bit are lit
    float    resolutionScale = 1.f;                  // light map size relative to the camera viewport

    static void describe(reflect::TypeBuilder<LightingSettings>& t) {
        t.field("enabled", &LightingSettings::enabled)
         .field("ambient", &LightingSettings::ambient)
         .field("layer_mask", &LightingSettings::layerMask)
         .field("resolution_scale", &LightingSettings::resolutionScale);
    }
};

// 2D lighting: point and spot lights, ambient light, and occluders that
// cast hard shadows. Every lit camera gets a light map that is multiplied
// over its view, so split screen and render-target cameras each get their
// own. Not part of DefaultPlugins; add it when a game wants lighting.
class LightingPlugin : public Plugin {
public:
    void build(App& app) override;
    void finish(App& app) override;
    DRIFT_PLUGIN(LightingPlugin)
};

} // namespace drift
