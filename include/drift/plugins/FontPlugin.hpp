#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/resources/FontResource.hpp>
#include <drift/resources/RendererResource.hpp>

namespace drift {

class FontPlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        auto* font = app.addResource<FontResource>(*app.getResource<RendererResource>());
        auto* assets = app.getResource<AssetServer>();
        assets->setFontLoader([font](const char* path, int sizePx) {
            return font->loadFont(path, static_cast<float>(sizePx));
        });
#endif
    }
    DRIFT_PLUGIN(FontPlugin)
};

} // namespace drift
