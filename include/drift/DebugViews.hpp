#pragma once

// Named debug views: drawing that shows what the engine sees (colliders,
// entity bounds, cameras, lights, tile grids), switched on and off by name
// while the game runs. Plugins add the views for what they own; games and
// dev tools switch them. Views draw on OverlayLayer, after lighting and
// post effects, so nothing darkens or tints them.
//
// The resource also gathers the outlines of what entities show on screen
// (sprites, shapes, text), which the "bounds" view draws and pick() tests.

#include <drift/Resource.hpp>
#include <drift/Types.hpp>

#include <functional>
#include <string>
#include <vector>

namespace drift {

class App;
class RendererResource;

// The outline of something an entity shows: four corners in world units
// (screen units on ScreenLayer), the entity's position, its layer and z.
struct DebugOutline {
    EntityId entity = InvalidEntityId;
    Vec2 corners[4];
    Vec2 pivot;
    uint8_t layer = 0;
    float z = 0.f;
};

class DebugViews : public Resource {
public:
    DRIFT_RESOURCE(DebugViews)

    using Draw = std::function<void(App&, RendererResource&)>;
    using OutlineSink = std::function<void(const DebugOutline&)>;
    using OutlineSource = std::function<void(App&, const OutlineSink&)>;

    // Adds drawing to the view `name`, made on first use. Several plugins
    // may add to one view. The draw runs in the Render phase while the view
    // is on, with the renderer on OverlayLayer.
    void add(const std::string& name, const std::string& description, Draw draw);
    // Switches a view; false for a name no plugin added.
    bool set(const std::string& name, bool on);
    bool enabled(const std::string& name) const;
    // Every view's name and description, in the order they were added.
    std::vector<std::pair<std::string, std::string>> list() const;

    // Adds a source of entity outlines (a plugin's visible components).
    void addOutlines(OutlineSource source);
    void forEachOutline(App& app, const OutlineSink& sink) const;
    // The entity whose outline holds `screenPos` (logical screen units) and
    // is drawn on top, or InvalidEntityId. World layers are tested through
    // the default camera.
    EntityId pick(App& app, Vec2 screenPos) const;

    // Runs the drawing of every view that is on (the debug_views system).
    void draw(App& app, RendererResource& renderer);

private:
    struct View {
        std::string name;
        std::string description;
        std::vector<Draw> draws;
        bool on = false;
    };
    std::vector<View> views_;
    std::vector<OutlineSource> outlines_;
};

// The corners of a w x h box with its pivot `origin` (in box units) placed
// at `pos`, scaled and turned like a sprite.
void debugBox(Vec2 pos, float rotation, Vec2 scale, Vec2 size, Vec2 origin, Vec2 out[4]);

} // namespace drift
