#pragma once

#include <drift/Plugin.h>
#include <drift/App.h>
#include <drift/resources/TilemapResource.h>
#include <drift/resources/RendererResource.h>

namespace drift {

class TilemapPlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        app.addResource<TilemapResource>(*app.getResource<RendererResource>());
#endif
    }
    DRIFT_PLUGIN(TilemapPlugin)
};

} // namespace drift
