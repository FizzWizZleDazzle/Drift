#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/resources/TilemapResource.hpp>
#include <drift/resources/RendererResource.hpp>

namespace drift {

class TilemapPlugin : public Plugin {
public:
    void build(App& app) override {
        app.addResource<TilemapResource>(*app.getResource<RendererResource>());
    }
    DRIFT_PLUGIN(TilemapPlugin)
};

} // namespace drift
