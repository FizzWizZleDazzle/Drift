#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/resources/FontResource.hpp>
#include <drift/resources/RendererResource.hpp>

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
