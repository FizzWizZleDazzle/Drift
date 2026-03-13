#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/resources/SpriteResource.hpp>
#include <drift/resources/RendererResource.hpp>

namespace drift {

class SpritePlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        app.addResource<SpriteResource>(*app.getResource<RendererResource>());
#endif
    }
    DRIFT_PLUGIN(SpritePlugin)
};

} // namespace drift
