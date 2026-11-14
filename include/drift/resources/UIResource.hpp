#pragma once

#include <drift/Resource.hpp>
#include <drift/App.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>

#include <functional>

union SDL_Event;

namespace drift {

class RendererResource;

class UIResource : public Resource, public EventHandler {
public:
    explicit UIResource(App& app);
    ~UIResource() override;

    DRIFT_RESOURCE(UIResource)
    bool mainThreadOnly() const override { return true; }

    void beginFrame();
    void endFrame();
    void processEvent(const SDL_Event& event) override;

    // Theming
    void setGameTheme(const UITheme& theme);

    // Widgets
    bool button(const char* label, Rect rect);
    void label(const char* text, Vec2 pos);
    void image(TextureHandle texture, Rect rect, Rect srcRect);
    // A slider at rect's position, rect.w wide (200 when 0), its label to
    // the right. Returns true when the value changes.
    bool slider(const char* label, float* value, float minVal, float maxVal, Rect rect);
    void progressBar(float fraction, Rect rect);

    // Panels
    void panelBegin(Rect rect, PanelFlags flags = PanelFlags::None);
    void panelEnd();

    // 9-slice
    void nineSlice(TextureHandle texture, Rect rect, Rect borders);

    // Positioning
    void setAnchor(Anchor anchor);

#ifdef DRIFT_DEV
    // Dev overlay: a menu of panels, shown and hidden with the dev key (F1
    // by default). Each panel is an ImGui window; `draw` fills it with ImGui
    // calls and runs as an exclusive system, so it may change the world.
    using DevPanel = std::function<void(App&)>;
    void addDevPanel(const char* name, DevPanel draw, bool open = false);
    void setDevToggleKey(Key key);
    void devToggle();
    void devRender();
    bool devIsVisible() const;
    // Components kept outside the ECS, such as a scripting host's: the
    // inspector shows them read-only beside the engine's. The function
    // answers an entity's components as a JSON object by name.
    using ForeignComponents = std::function<std::string(EntityId)>;
    void setForeignComponents(ForeignComponents components);
    const ForeignComponents& foreignComponents() const;
#endif

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
