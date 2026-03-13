#pragma once

#include <drift/Resource.hpp>
#include <drift/App.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>

union SDL_Event;

namespace drift {

class RendererResource;

class UIResource : public Resource, public EventHandler {
public:
    explicit UIResource(App& app);
    ~UIResource() override;

    const char* name() const override { return "UIResource"; }

    void beginFrame();
    void endFrame();
    void render();
    void processEvent(const SDL_Event& event) override;

    // Theming
    void setGameTheme(const UITheme& theme);
    void pushFont(FontHandle font);
    void popFont();

    // Widgets
    bool button(const char* label, Rect rect);
    void label(const char* text, Vec2 pos);
    void image(TextureHandle texture, Rect rect, Rect srcRect);
    bool slider(const char* label, float* value, float minVal, float maxVal);
    void progressBar(float fraction, Rect rect);

    // Panels
    void panelBegin(Rect rect, PanelFlags flags = PanelFlags::None);
    void panelEnd();

    // 9-slice
    void nineSlice(TextureHandle texture, Rect rect, Rect borders);

    // Positioning
    void setAnchor(Anchor anchor);

#ifdef DRIFT_DEV
    void devToggle();
    void devRender();
    bool devIsVisible() const;
#endif

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
