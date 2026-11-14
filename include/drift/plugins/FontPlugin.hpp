#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/World.hpp>
#include <drift/components/GlobalTransform.hpp>
#include <drift/components/Text.hpp>
#include <drift/resources/FontResource.hpp>
#include <drift/resources/RendererResource.hpp>

#include <string>

namespace drift {

inline void font_begin_frame(ResMut<FontResource> fonts) { fonts->beginFrame(); }

// Draws every visible Text entity at its world transform (GlobalTransform2D
// when it is part of a hierarchy, Transform2D otherwise).
inline void drawTextEntities(App& app) {
    auto* fonts = app.getResource<FontResource>();
    World& world = app.world();
    const auto& registry = world.componentRegistry();
    ComponentId transformId = registry.get<Transform2D>();
    ComponentId textId = registry.getByName("Text");
    ComponentId globalId = registry.getByName("GlobalTransform2D");
    if (!fonts || !transformId || !textId) return;

    std::string expr = "#" + std::to_string(transformId) + ", #" + std::to_string(textId);
    if (globalId) expr += ", ?#" + std::to_string(globalId);

    QueryIter it = world.queryIter(expr.c_str());
    while (world.queryNext(&it)) {
        auto* transforms = static_cast<Transform2D*>(world.queryField(&it, 0, sizeof(Transform2D)));
        auto* texts = static_cast<Text*>(world.queryField(&it, 1, sizeof(Text)));
        auto* globals = globalId ? static_cast<GlobalTransform2D*>(
            world.queryFieldOptional(&it, 2, sizeof(GlobalTransform2D))) : nullptr;
        for (int32_t i = 0; i < it.count; ++i) {
            const Text& text = texts[i];
            if (!text.visible || text.text[0] == '\0') continue;
            Transform2D t = transforms[i];
            if (globals) t = {globals[i].position, globals[i].rotation, globals[i].scale};
            fonts->drawText(text.font, text.str(), t, text.style());
        }
    }
    world.queryFini(&it);
}

class FontPlugin : public Plugin {
public:
    void build(App& app) override {
        auto* font = app.addResource<FontResource>(*app.getResource<RendererResource>());
        auto* assets = app.getResource<AssetServer>();
        assets->setFontLoader([font](const char* path, int sizePx) {
            return font->loadFont(path, static_cast<float>(sizePx));
        });
        assets->setReloader("font", [font](uint32_t id, const std::string& path) {
            return font->reloadFont(FontHandle{id}, path.c_str());
        });
        app.world().registerComponent<Text>("Text");
        app.addSystem<font_begin_frame>("font_begin", Phase::PreUpdate);
        app.addSystem("text_render", Phase::Render, drawTextEntities);
    }
    DRIFT_PLUGIN(FontPlugin)
};

} // namespace drift
