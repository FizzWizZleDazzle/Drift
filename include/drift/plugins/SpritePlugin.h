#pragma once

#include <drift/Plugin.h>
#include <drift/App.h>
#include <drift/resources/SpriteResource.h>
#include <drift/resources/RendererResource.h>

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
