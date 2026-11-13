#include <drift/resources/UIResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>
#include <drift/App.hpp>
#include <drift/Log.hpp>

#ifdef DRIFT_DEV
#include "ui/DevPanels.hpp"
#endif

#include "gfx/Device.hpp"

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"

#include <cstdio>
#include <string>
#include <vector>

namespace drift {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void applyTheme(const UITheme& t) {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = t.rounding;
    style.FrameRounding  = t.rounding;
    style.GrabRounding   = t.rounding;
    style.WindowPadding  = ImVec2(t.padding, t.padding);
    style.FramePadding   = ImVec2(t.padding * 0.5f, t.padding * 0.25f);

    ImVec4* colors = style.Colors;

    auto toImVec4 = [](ColorF c) -> ImVec4 {
        return ImVec4(c.r, c.g, c.b, c.a);
    };

    colors[ImGuiCol_WindowBg]             = toImVec4(t.background);
    colors[ImGuiCol_PopupBg]              = toImVec4(t.background);
    colors[ImGuiCol_TitleBg]              = toImVec4(t.secondary);
    colors[ImGuiCol_TitleBgActive]        = toImVec4(t.primary);
    colors[ImGuiCol_Text]                 = toImVec4(t.text);
    colors[ImGuiCol_Button]               = toImVec4(t.button);
    colors[ImGuiCol_ButtonHovered]        = toImVec4(t.buttonHover);
    colors[ImGuiCol_ButtonActive]         = toImVec4(t.buttonActive);
    colors[ImGuiCol_FrameBg]              = toImVec4(t.button);
    colors[ImGuiCol_FrameBgHovered]       = toImVec4(t.buttonHover);
    colors[ImGuiCol_FrameBgActive]        = toImVec4(t.buttonActive);
    colors[ImGuiCol_SliderGrab]           = toImVec4(t.primary);
    colors[ImGuiCol_SliderGrabActive]     = toImVec4(t.primary);
    colors[ImGuiCol_Header]               = toImVec4(t.button);
    colors[ImGuiCol_HeaderHovered]        = toImVec4(t.buttonHover);
    colors[ImGuiCol_HeaderActive]         = toImVec4(t.buttonActive);
    colors[ImGuiCol_Border]               = toImVec4(t.secondary);
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.f, 0.f, 0.f, 0.3f);
    colors[ImGuiCol_ScrollbarGrab]        = toImVec4(t.button);
    colors[ImGuiCol_ScrollbarGrabHovered] = toImVec4(t.buttonHover);
    colors[ImGuiCol_ScrollbarGrabActive]  = toImVec4(t.buttonActive);
    colors[ImGuiCol_CheckMark]            = toImVec4(t.primary);
    colors[ImGuiCol_PlotHistogram]        = toImVec4(t.primary);
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct UIResource::Impl {
    App& app;
    ImGuiContext* imguiCtx = nullptr;
    bool initialised = false;
    UITheme theme;
    Anchor currentAnchor = Anchor::TopLeft;
    int widgetWindowId = 0;
    int panelCounter = 0;
    ImVec2 displayOffset = ImVec2(0.f, 0.f);
    ImVec2 framebufferSize = ImVec2(0.f, 0.f);

#ifdef DRIFT_DEV
    bool devVisible = false;
    Key devKey = Key::F1;
    struct Panel {
        std::string name;
        DevPanel draw;
        bool open = false;
        int slot = -1;   // screen position, assigned when first opened
    };
    std::vector<Panel> panels;
    int nextSlot = 0;
#endif

    Impl(App& a) : app(a) {}

    void beginWidgetWindow(const char* labelHint) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "##drift_widget_%s_%d", labelHint, widgetWindowId++);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y));

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNav;

        ImGui::Begin(buf, nullptr, flags);
    }

    void endWidgetWindow() {
        ImGui::End();
    }
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

UIResource::UIResource(App& app)
    : impl_(new Impl(app))
{
    SDL_Window* window = static_cast<SDL_Window*>(app.sdlWindow());
    auto* renderer = app.getResource<RendererResource>();
    gfx::Device* device = renderer ? renderer->device() : nullptr;

    if (!window || !device) {
        DRIFT_LOG_ERROR("UIResource: window or graphics device not available");
        return;
    }

    IMGUI_CHECKVERSION();
    impl_->imguiCtx = ImGui::CreateContext();
    if (!impl_->imguiCtx) {
        DRIFT_LOG_ERROR("UIResource: failed to create ImGui context");
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;   // games do not write imgui.ini next to the executable

    if (!ImGui_ImplSDL3_InitForOther(window)) {
        DRIFT_LOG_ERROR("UIResource: ImGui_ImplSDL3_InitForOther failed");
        ImGui::DestroyContext(impl_->imguiCtx);
        impl_->imguiCtx = nullptr;
        return;
    }

    if (!device->imguiInit()) {
        DRIFT_LOG_ERROR("UIResource: ImGui renderer init failed");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(impl_->imguiCtx);
        impl_->imguiCtx = nullptr;
        return;
    }

    applyTheme(impl_->theme);
    impl_->initialised = true;

#ifdef DRIFT_DEV
    addDevPanel("Inspector", makeInspectorPanel(), true);
    addDevPanel("Performance", makePerformancePanel(), false);
#endif

    // ImGui draws on top of the presented frame.
    renderer->setOverlayCallback([device]() {
        if (ImDrawData* drawData = ImGui::GetDrawData()) {
            device->imguiRender(drawData);
        }
    });

    DRIFT_LOG_INFO("UIResource: initialised");
}

UIResource::~UIResource() {
    if (impl_->initialised) {
        auto* renderer = impl_->app.getResource<RendererResource>();
        if (renderer) {
            renderer->setOverlayCallback(nullptr);
            if (renderer->device()) renderer->device()->imguiShutdown();
        }
        ImGui_ImplSDL3_Shutdown();
        if (impl_->imguiCtx) {
            ImGui::DestroyContext(impl_->imguiCtx);
        }
    }
    delete impl_;
}

// ImGui works in logical coordinates: widget rects match the game's logical
// resolution and land inside the renderer's output rect.
void UIResource::beginFrame() {
    if (!impl_->initialised) return;

    auto* renderer = impl_->app.getResource<RendererResource>();
    renderer->device()->imguiNewFrame();
    ImGui_ImplSDL3_NewFrame();

    Rect out = renderer->outputRect();
    float lw = renderer->logicalWidth();
    float lh = renderer->logicalHeight();
    if (out.w > 0.f && out.h > 0.f && lw > 0.f && lh > 0.f) {
        ImGuiIO& io = ImGui::GetIO();
        float sx = out.w / lw, sy = out.h / lh;
        int dw = 0, dh = 0;
        SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(impl_->app.sdlWindow()), &dw, &dh);
        io.DisplaySize = ImVec2(lw, lh);
        io.DisplayFramebufferScale = ImVec2(sx, sy);
        impl_->displayOffset = ImVec2(-out.x / sx, -out.y / sy);
        impl_->framebufferSize = ImVec2(static_cast<float>(dw) / sx, static_cast<float>(dh) / sy);
    }

    ImGui::NewFrame();
    impl_->widgetWindowId = 0;
    impl_->panelCounter = 0;
}

void UIResource::endFrame() {
    if (!impl_->initialised) return;
    ImGui::Render();
    if (ImDrawData* dd = ImGui::GetDrawData()) {
        // Shift the logical origin to the output rect and cover the window.
        dd->DisplayPos = impl_->displayOffset;
        dd->DisplaySize = impl_->framebufferSize;
    }
}

void UIResource::processEvent(const SDL_Event& event) {
    if (!impl_->initialised) return;
    auto* renderer = impl_->app.getResource<RendererResource>();
    SDL_Event e = event;
    auto map = [&](float& x, float& y) {
        Vec2 p = renderer->windowToLogical({x, y});
        x = p.x;
        y = p.y;
    };
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: map(e.motion.x, e.motion.y); break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: map(e.button.x, e.button.y); break;
    case SDL_EVENT_MOUSE_WHEEL: map(e.wheel.mouse_x, e.wheel.mouse_y); break;
    default: break;
    }
#ifdef DRIFT_DEV
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
        static_cast<int>(e.key.scancode) == static_cast<int>(impl_->devKey)) {
        devToggle();
    }
#endif
    ImGui_ImplSDL3_ProcessEvent(&e);
}

// Theming
void UIResource::setGameTheme(const UITheme& theme) {
    impl_->theme = theme;
    if (impl_->initialised) {
        applyTheme(impl_->theme);
    }
}

// Widgets
bool UIResource::button(const char* label, Rect rect) {
    if (!impl_->initialised || !label) return false;
    impl_->beginWidgetWindow(label);
    ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));
    bool pressed = ImGui::Button(label, ImVec2(rect.w, rect.h));
    impl_->endWidgetWindow();
    return pressed;
}

void UIResource::label(const char* text, Vec2 pos) {
    if (!impl_->initialised || !text) return;
    impl_->beginWidgetWindow("label");
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y));
    ImGui::TextUnformatted(text);
    impl_->endWidgetWindow();
}

void UIResource::image(TextureHandle texture, Rect rect, Rect srcRect) {
    if (!impl_->initialised) return;
    impl_->beginWidgetWindow("image");
    ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));

    ImTextureID texId = static_cast<ImTextureID>(
        impl_->app.getResource<RendererResource>()->imguiTextureId(texture));

    ImVec2 uv0(0.f, 0.f);
    ImVec2 uv1(1.f, 1.f);

    if (srcRect.w > 0.f && srcRect.h > 0.f) {
        auto* renderer = impl_->app.getResource<RendererResource>();
        if (renderer) {
            int32_t texW = 0, texH = 0;
            renderer->getTextureSize(texture, &texW, &texH);
            if (texW > 0 && texH > 0) {
                float fw = static_cast<float>(texW);
                float fh = static_cast<float>(texH);
                uv0 = ImVec2(srcRect.x / fw, srcRect.y / fh);
                uv1 = ImVec2((srcRect.x + srcRect.w) / fw,
                             (srcRect.y + srcRect.h) / fh);
            }
        }
    }

    ImGui::Image(texId, ImVec2(rect.w, rect.h), uv0, uv1);
    impl_->endWidgetWindow();
}

bool UIResource::slider(const char* label, float* value, float minVal, float maxVal, Rect rect) {
    if (!impl_->initialised || !label || !value) return false;
    impl_->beginWidgetWindow(label);
    ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));
    ImGui::PushItemWidth(rect.w > 0.f ? rect.w : 200.f);
    bool changed = ImGui::SliderFloat(label, value, minVal, maxVal);
    ImGui::PopItemWidth();
    impl_->endWidgetWindow();
    return changed;
}

void UIResource::progressBar(float fraction, Rect rect) {
    if (!impl_->initialised) return;
    impl_->beginWidgetWindow("progress");
    ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));
    float f = fraction;
    if (f < 0.f) f = 0.f;
    if (f > 1.f) f = 1.f;
    ImGui::ProgressBar(f, ImVec2(rect.w, rect.h));
    impl_->endWidgetWindow();
}

// Panels
void UIResource::panelBegin(Rect rect, PanelFlags flags) {
    if (!impl_->initialised) return;

    char name[64];
    std::snprintf(name, sizeof(name), "##drift_panel_%d", impl_->panelCounter++);

    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h), ImGuiCond_Always);

    ImGuiWindowFlags winFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    if (flags & PanelFlags::NoBg) {
        winFlags |= ImGuiWindowFlags_NoBackground;
    }
    if (!(flags & PanelFlags::Scrollable)) {
        winFlags |= ImGuiWindowFlags_NoScrollbar;
        winFlags |= ImGuiWindowFlags_NoScrollWithMouse;
    }

    bool drawBorder = flags & PanelFlags::Border;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, drawBorder ? 1.f : 0.f);
    ImGui::Begin(name, nullptr, winFlags);
}

void UIResource::panelEnd() {
    if (!impl_->initialised) return;
    ImGui::End();
    ImGui::PopStyleVar();
}

// 9-slice
void UIResource::nineSlice(TextureHandle texture, Rect rect, Rect borders) {
    if (!impl_->initialised) return;

    auto* renderer = impl_->app.getResource<RendererResource>();
    if (!renderer) return;

    int32_t texW = 0, texH = 0;
    renderer->getTextureSize(texture, &texW, &texH);
    if (texW <= 0 || texH <= 0) return;

    float fw = static_cast<float>(texW);
    float fh = static_cast<float>(texH);

    ImTextureID texId = static_cast<ImTextureID>(
        impl_->app.getResource<RendererResource>()->imguiTextureId(texture));
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImU32 white = IM_COL32(255, 255, 255, 255);

    float bl = borders.x, bt = borders.y, br = borders.w, bb = borders.h;

    float ul = bl / fw, ut = bt / fh;
    float ur = (fw - br) / fw, ub = (fh - bb) / fh;

    float dx0 = rect.x, dy0 = rect.y;
    float dx1 = rect.x + bl, dy1 = rect.y + bt;
    float dx2 = rect.x + rect.w - br, dy2 = rect.y + rect.h - bb;
    float dx3 = rect.x + rect.w, dy3 = rect.y + rect.h;

    // 9 quads
    dl->AddImage(texId, ImVec2(dx0, dy0), ImVec2(dx1, dy1), ImVec2(0.f, 0.f), ImVec2(ul, ut), white);
    dl->AddImage(texId, ImVec2(dx1, dy0), ImVec2(dx2, dy1), ImVec2(ul, 0.f), ImVec2(ur, ut), white);
    dl->AddImage(texId, ImVec2(dx2, dy0), ImVec2(dx3, dy1), ImVec2(ur, 0.f), ImVec2(1.f, ut), white);

    dl->AddImage(texId, ImVec2(dx0, dy1), ImVec2(dx1, dy2), ImVec2(0.f, ut), ImVec2(ul, ub), white);
    dl->AddImage(texId, ImVec2(dx1, dy1), ImVec2(dx2, dy2), ImVec2(ul, ut), ImVec2(ur, ub), white);
    dl->AddImage(texId, ImVec2(dx2, dy1), ImVec2(dx3, dy2), ImVec2(ur, ut), ImVec2(1.f, ub), white);

    dl->AddImage(texId, ImVec2(dx0, dy2), ImVec2(dx1, dy3), ImVec2(0.f, ub), ImVec2(ul, 1.f), white);
    dl->AddImage(texId, ImVec2(dx1, dy2), ImVec2(dx2, dy3), ImVec2(ul, ub), ImVec2(ur, 1.f), white);
    dl->AddImage(texId, ImVec2(dx2, dy2), ImVec2(dx3, dy3), ImVec2(ur, ub), ImVec2(1.f, 1.f), white);
}

// Positioning
void UIResource::setAnchor(Anchor anchor) {
    impl_->currentAnchor = anchor;
}

#ifdef DRIFT_DEV
void UIResource::devToggle() {
    impl_->devVisible = !impl_->devVisible;
}

void UIResource::addDevPanel(const char* name, DevPanel draw, bool open) {
    if (!name || !draw) return;
    for (auto& p : impl_->panels) {
        if (p.name == name) {
            p.draw = std::move(draw);
            return;
        }
    }
    impl_->panels.push_back({name, std::move(draw), open});
}

void UIResource::setDevToggleKey(Key key) {
    impl_->devKey = key;
}

void UIResource::devRender() {
    if (!impl_->initialised || !impl_->devVisible) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("Drift Dev", &impl_->devVisible, flags)) {
        auto* timeRes = impl_->app.getResource<Time>();
        float dt = timeRes ? timeRes->delta : 0.f;
        float fps = (dt > 0.f) ? (1.f / dt) : 0.f;
        ImGui::Text("%.1f FPS (%.2f ms)  frame %llu", fps, dt * 1000.f,
                    static_cast<unsigned long long>(timeRes ? timeRes->frame : 0));
        ImGui::Separator();
        for (auto& p : impl_->panels) {
            ImGui::Checkbox(p.name.c_str(), &p.open);
        }
    }
    ImGui::End();

    // Panels start along the right edge, out of the way of the scene: the
    // first one gets the full height, later ones fill columns of two
    // further left.
    for (auto& p : impl_->panels) {
        if (!p.open) continue;
        if (p.slot < 0) p.slot = impl_->nextSlot++;
        float fullH = io.DisplaySize.y - 20.f;
        int col = p.slot == 0 ? 0 : 1 + (p.slot - 1) / 2;
        int row = p.slot == 0 ? 0 : (p.slot - 1) % 2;
        float h = p.slot == 0 ? fullH : fullH * 0.5f - 4.f;
        float x = io.DisplaySize.x - 370.f * static_cast<float>(col + 1);
        float y = 10.f + static_cast<float>(row) * (fullH * 0.5f + 4.f);
        ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360.f, h), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.9f);
        if (ImGui::Begin(p.name.c_str(), &p.open, ImGuiWindowFlags_NoSavedSettings)) {
            p.draw(impl_->app);
        }
        ImGui::End();
    }
}

bool UIResource::devIsVisible() const {
    return impl_->devVisible;
}
#endif

} // namespace drift
