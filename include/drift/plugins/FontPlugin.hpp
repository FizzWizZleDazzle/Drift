#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/DebugViews.hpp>
#include <drift/Query.hpp>
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
        // Text outlines for the "bounds" debug view and picking.
        app.initResource<DebugViews>()->addOutlines([](App& a, const DebugViews::OutlineSink& out) {
            auto* fonts = a.getResource<FontResource>();
            World& w = a.world();
            Query<Text, Transform2D, Optional<GlobalTransform2D>> q(w, w.componentRegistry());
            q.iterWithEntity([&](EntityId e, const Text& text, const Transform2D& t, const GlobalTransform2D* g) {
                if (!text.visible || text.text[0] == '\0') return;
                TextStyle style = text.style();
                Vec2 size = fonts->measureText(text.font, text.str(), style);
                float ax = text.align == TextAlign::Center ? 0.5f : text.align == TextAlign::Right ? 1.f : 0.f;
                float ay = text.valign == TextVAlign::Middle ? 0.5f : text.valign == TextVAlign::Bottom ? 1.f : 0.f;
                DebugOutline o;
                o.entity = e;
                o.pivot = g ? g->position : t.position;
                o.layer = text.layer;
                o.z = text.zOrder;
                debugBox(o.pivot, g ? g->rotation : t.rotation, g ? g->scale : t.scale, size,
                         {ax * size.x, ay * size.y}, o.corners);
                out(o);
            });
        });
    }
    DRIFT_PLUGIN(FontPlugin)
};

} // namespace drift
