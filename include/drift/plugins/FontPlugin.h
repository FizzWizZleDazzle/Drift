#pragma once

#include <drift/Plugin.h>
#include <drift/App.h>
#include <drift/resources/FontResource.h>
#include <drift/resources/RendererResource.h>

namespace drift {

class FontPlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        app.addResource<FontResource>(*app.getResource<RendererResource>());
#endif
    }
    DRIFT_PLUGIN(FontPlugin)
};

} // namespace drift
