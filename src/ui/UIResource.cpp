#include <drift/resources/UIResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/App.hpp>
#include <drift/Log.hpp>

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

#include <cstdio>
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
    std::vector<FontHandle> fontStack;

#ifdef DRIFT_DEV
    bool devVisible = false;
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
    SDL_GPUDevice* device = static_cast<SDL_GPUDevice*>(app.gpuDevice());

    if (!window || !device) {
        DRIFT_LOG_ERROR("UIResource: window or GPU device not available");
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

    if (!ImGui_ImplSDL3_InitForOther(window)) {
        DRIFT_LOG_ERROR("UIResource: ImGui_ImplSDL3_InitForOther failed");
        ImGui::DestroyContext(impl_->imguiCtx);
        impl_->imguiCtx = nullptr;
        return;
    }

    ImGui_ImplSDLGPU3_InitInfo gpuInitInfo = {};
    gpuInitInfo.Device            = device;
    gpuInitInfo.ColorTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    gpuInitInfo.MSAASamples       = SDL_GPU_SAMPLECOUNT_1;

    if (!ImGui_ImplSDLGPU3_Init(&gpuInitInfo)) {
        DRIFT_LOG_ERROR("UIResource: ImGui_ImplSDLGPU3_Init failed");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(impl_->imguiCtx);
        impl_->imguiCtx = nullptr;
        return;
    }

    applyTheme(impl_->theme);
    impl_->initialised = true;
    DRIFT_LOG_INFO("UIResource: initialised");
}

UIResource::~UIResource() {
    if (impl_->initialised) {
        ImGui_ImplSDLGPU3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        if (impl_->imguiCtx) {
            ImGui::DestroyContext(impl_->imguiCtx);
        }
    }
    delete impl_;
}

void UIResource::beginFrame() {
    if (!impl_->initialised) return;

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    impl_->widgetWindowId = 0;
}

void UIResource::endFrame() {
    if (!impl_->initialised) return;
    ImGui::EndFrame();
    ImGui::Render();
}

void UIResource::render() {
    if (!impl_->initialised) return;

    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData) return;

    // NOTE: ImGui_ImplSDLGPU3_RenderDrawData requires an active
    // SDL_GPUCommandBuffer and SDL_GPURenderPass. The RendererResource
    // owns the render pass lifecycle. For now this is a placeholder —
    // the renderer calls into ImGui at the correct moment in the frame.
    // TODO: Expose command buffer from renderer for proper integration.
}

void UIResource::processEvent(const SDL_Event& event) {
    if (!impl_->initialised) return;
    ImGui_ImplSDL3_ProcessEvent(&event);
}

// Theming
void UIResource::setGameTheme(const UITheme& theme) {
    impl_->theme = theme;
    if (impl_->initialised) {
        applyTheme(impl_->theme);
    }
}

void UIResource::pushFont(FontHandle font) {
    impl_->fontStack.push_back(font);
    // NOTE: Proper implementation would call ImGui::PushFont with the
    // ImFont* associated with this FontHandle.
}

void UIResource::popFont() {
    if (impl_->fontStack.empty()) return;
    impl_->fontStack.pop_back();
    // NOTE: Would call ImGui::PopFont() here once ImFont integration is done.
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

    ImTextureID texId = static_cast<ImTextureID>(static_cast<uintptr_t>(texture.id));

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

bool UIResource::slider(const char* label, float* value, float minVal, float maxVal) {
    if (!impl_->initialised || !label || !value) return false;
    impl_->beginWidgetWindow(label);
    ImGui::SetCursorScreenPos(ImVec2(impl_->theme.padding, impl_->theme.padding));
    ImGui::PushItemWidth(200.f);
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

    static int panelCounter = 0;
    char name[64];
    std::snprintf(name, sizeof(name), "##drift_panel_%d", panelCounter++);

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

    ImTextureID texId = static_cast<ImTextureID>(static_cast<uintptr_t>(texture.id));
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

void UIResource::devRender() {
    if (!impl_->initialised || !impl_->devVisible) return;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 180), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.75f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("Drift Dev Overlay", &impl_->devVisible, flags)) {
        float dt = impl_->app.deltaTime();
        float fps = (dt > 0.f) ? (1.f / dt) : 0.f;
        ImGui::Text("FPS: %.1f (%.2f ms)", fps, dt * 1000.f);

        ImGui::Separator();
        ImGui::Text("Frame: %llu",
                     static_cast<unsigned long long>(impl_->app.frameCount()));
        ImGui::Text("Time:  %.2f s", impl_->app.time());

        ImGui::Separator();
        if (ImGui::Button("Close")) {
            impl_->devVisible = false;
        }
    }
    ImGui::End();
}

bool UIResource::devIsVisible() const {
    return impl_->devVisible;
}
#endif

} // namespace drift
