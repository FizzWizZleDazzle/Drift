#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/resources/TilemapResource.hpp>
#include <drift/resources/RendererResource.hpp>

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
