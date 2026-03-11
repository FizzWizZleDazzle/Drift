// =============================================================================
// Drift 2D Game Engine - UI Implementation
// =============================================================================
// Implements the public drift/ui.h API on top of Dear ImGui.
// Uses the SDL3 + SDL_GPU3 ImGui backends for input handling and rendering.
//
// Integration notes:
//   - drift_ui_init must be called AFTER the renderer has been initialised
//     (i.e. after drift_renderer_init) so that the GPU device and window are
//     available.
//   - drift_ui_render requires the active render pass / command buffer from the
//     renderer.  See the TODO in that function for the integration hook.
//   - The game-facing widget functions (button, label, slider, etc.) use
//     ImGui's immediate-mode API internally, translating screen-space DriftRect
//     positions into ImGui cursor positions within an invisible full-screen
//     overlay window.
// =============================================================================

#include <drift/ui.h>
#include <drift/drift.h>
#include <drift/renderer.h>
#include <drift/physics.h>

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

#include <cstdio>
#include <cstring>
#include <vector>

// -----------------------------------------------------------------------------
// External linkage to core/renderer internals
// -----------------------------------------------------------------------------
extern "C" {
    SDL_Window*    drift_internal_get_window(void);
    SDL_GPUDevice* drift_internal_get_gpu_device(void);
}

// =============================================================================
// Internal state
// =============================================================================
namespace {

static ImGuiContext*    g_imgui_ctx      = nullptr;
static bool             g_initialised    = false;
static DriftUITheme     g_theme;
static DriftAnchor      g_current_anchor = DRIFT_ANCHOR_TOP_LEFT;

// Unique window id counter for ImGui windows created by the widget helpers.
static int              g_widget_window_id = 0;

// Font stack.
static std::vector<DriftFont> g_font_stack;

#ifdef DRIFT_DEV
static bool             g_dev_visible = false;
#endif

// -----------------------------------------------------------------------------
// Helper: apply the current DriftUITheme to ImGui style colours.
// -----------------------------------------------------------------------------
static void apply_theme(const DriftUITheme& t) {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = t.rounding;
    style.FrameRounding     = t.rounding;
    style.GrabRounding      = t.rounding;
    style.WindowPadding     = ImVec2(t.padding, t.padding);
    style.FramePadding      = ImVec2(t.padding * 0.5f, t.padding * 0.25f);

    ImVec4* colors = style.Colors;

    auto to_imvec4 = [](DriftColorF c) -> ImVec4 {
        return ImVec4(c.r, c.g, c.b, c.a);
    };

    colors[ImGuiCol_WindowBg]       = to_imvec4(t.background);
    colors[ImGuiCol_PopupBg]        = to_imvec4(t.background);
    colors[ImGuiCol_TitleBg]        = to_imvec4(t.secondary);
    colors[ImGuiCol_TitleBgActive]  = to_imvec4(t.primary);
    colors[ImGuiCol_Text]           = to_imvec4(t.text);
    colors[ImGuiCol_Button]         = to_imvec4(t.button);
    colors[ImGuiCol_ButtonHovered]  = to_imvec4(t.button_hover);
    colors[ImGuiCol_ButtonActive]   = to_imvec4(t.button_active);
    colors[ImGuiCol_FrameBg]        = to_imvec4(t.button);
    colors[ImGuiCol_FrameBgHovered] = to_imvec4(t.button_hover);
    colors[ImGuiCol_FrameBgActive]  = to_imvec4(t.button_active);
    colors[ImGuiCol_SliderGrab]     = to_imvec4(t.primary);
    colors[ImGuiCol_SliderGrabActive] = to_imvec4(t.primary);
    colors[ImGuiCol_Header]         = to_imvec4(t.button);
    colors[ImGuiCol_HeaderHovered]  = to_imvec4(t.button_hover);
    colors[ImGuiCol_HeaderActive]   = to_imvec4(t.button_active);
    colors[ImGuiCol_Border]         = to_imvec4(t.secondary);
    colors[ImGuiCol_ScrollbarBg]    = ImVec4(0.0f, 0.0f, 0.0f, 0.3f);
    colors[ImGuiCol_ScrollbarGrab]  = to_imvec4(t.button);
    colors[ImGuiCol_ScrollbarGrabHovered] = to_imvec4(t.button_hover);
    colors[ImGuiCol_ScrollbarGrabActive]  = to_imvec4(t.button_active);
    colors[ImGuiCol_CheckMark]      = to_imvec4(t.primary);
    colors[ImGuiCol_PlotHistogram]  = to_imvec4(t.primary);
}

// -----------------------------------------------------------------------------
// Helper: begin an invisible, full-screen overlay window for positioned widgets.
// Each call to a widget function that needs positioned drawing should call this,
// do its work, then call end_widget_window().
// -----------------------------------------------------------------------------
static void begin_widget_window(const char* label_hint) {
    char buf[128];
    snprintf(buf, sizeof(buf), "##drift_widget_%s_%d", label_hint, g_widget_window_id++);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar   |
        ImGuiWindowFlags_NoResize     |
        ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoScrollbar  |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse   |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav        |
        ImGuiWindowFlags_NoInputs;

    // Remove NoInputs so the widgets inside can still receive clicks.
    flags &= ~ImGuiWindowFlags_NoInputs;

    ImGui::Begin(buf, nullptr, flags);
}

static void end_widget_window() {
    ImGui::End();
}

} // anonymous namespace

// =============================================================================
// PUBLIC API IMPLEMENTATION
// =============================================================================

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

DriftResult drift_ui_init(void) {
    if (g_initialised) {
        SDL_Log("drift_ui_init: already initialised");
        return DRIFT_OK;
    }

    SDL_Window*    window = drift_internal_get_window();
    SDL_GPUDevice* device = drift_internal_get_gpu_device();

    if (!window || !device) {
        SDL_Log("drift_ui_init: window or GPU device not available");
        return DRIFT_ERROR_INIT_FAILED;
    }

    // Create ImGui context.
    IMGUI_CHECKVERSION();
    g_imgui_ctx = ImGui::CreateContext();
    if (!g_imgui_ctx) {
        SDL_Log("drift_ui_init: failed to create ImGui context");
        return DRIFT_ERROR_INIT_FAILED;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Initialise SDL3 platform backend.
    if (!ImGui_ImplSDL3_InitForOther(window)) {
        SDL_Log("drift_ui_init: ImGui_ImplSDL3_InitForOther failed");
        ImGui::DestroyContext(g_imgui_ctx);
        g_imgui_ctx = nullptr;
        return DRIFT_ERROR_INIT_FAILED;
    }

    // Initialise SDL GPU renderer backend.
    ImGui_ImplSDLGPU3_InitInfo gpu_init_info = {};
    gpu_init_info.Device              = device;
    gpu_init_info.ColorTargetFormat   = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    gpu_init_info.MSAASamples         = SDL_GPU_SAMPLECOUNT_1;

    if (!ImGui_ImplSDLGPU3_Init(&gpu_init_info)) {
        SDL_Log("drift_ui_init: ImGui_ImplSDLGPU3_Init failed");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(g_imgui_ctx);
        g_imgui_ctx = nullptr;
        return DRIFT_ERROR_INIT_FAILED;
    }

    // Apply default theme.
    g_theme = drift_ui_theme_default();
    apply_theme(g_theme);

    g_font_stack.clear();
    g_current_anchor = DRIFT_ANCHOR_TOP_LEFT;

    g_initialised = true;
    SDL_Log("drift_ui_init: UI system initialised");
    return DRIFT_OK;
}

void drift_ui_shutdown(void) {
    if (!g_initialised) return;

    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();

    if (g_imgui_ctx) {
        ImGui::DestroyContext(g_imgui_ctx);
        g_imgui_ctx = nullptr;
    }

    g_font_stack.clear();
    g_initialised = false;

    SDL_Log("drift_ui_shutdown: UI system shut down");
}

// -----------------------------------------------------------------------------
// Frame
// -----------------------------------------------------------------------------

void drift_ui_begin_frame(void) {
    if (!g_initialised) return;

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Reset the per-frame widget window counter.
    g_widget_window_id = 0;
}

void drift_ui_end_frame(void) {
    if (!g_initialised) return;

    ImGui::EndFrame();
    ImGui::Render();
}

void drift_ui_render(void) {
    if (!g_initialised) return;

    ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data) return;

    // -------------------------------------------------------------------------
    // TODO: Render pass integration.
    //
    // ImGui_ImplSDLGPU3_RenderDrawData requires an active SDL_GPUCommandBuffer
    // and SDL_GPURenderPass.  The renderer module (renderer.cpp) currently owns
    // the command buffer and render pass lifecycle.  To properly integrate:
    //
    //   1. Expose the current command buffer and/or render pass from the
    //      renderer via internal accessor functions, e.g.:
    //        extern SDL_GPUCommandBuffer* drift_internal_get_cmd_buf(void);
    //      and call ImGui_ImplSDLGPU3_RenderDrawData here.
    //
    //   2. Alternatively, call drift_ui_render from within
    //      drift_renderer_end_frame after the sprite batch has been flushed
    //      but before the render pass is ended.
    //
    // For now this function is a no-op beyond acquiring the draw data.
    // The integration point is intentionally left open so the renderer can
    // invoke the ImGui draw at the correct moment in the frame.
    // -------------------------------------------------------------------------

    // Placeholder: when the renderer exposes its command buffer, uncomment:
    // SDL_GPUCommandBuffer* cmd = drift_internal_get_cmd_buf();
    // ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd);
}

// -----------------------------------------------------------------------------
// Event processing
// -----------------------------------------------------------------------------

void drift_ui_process_event(const void* sdl_event) {
    if (!g_initialised || !sdl_event) return;

    ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(sdl_event));
}

// -----------------------------------------------------------------------------
// Theming
// -----------------------------------------------------------------------------

void drift_ui_set_game_theme(const DriftUITheme* theme) {
    if (!theme) return;

    g_theme = *theme;

    if (g_initialised) {
        apply_theme(g_theme);
    }
}

void drift_ui_push_font(DriftFont font) {
    g_font_stack.push_back(font);

    // NOTE: To actually push an ImGui font, the DriftFont's atlas would need
    // to be registered with ImGui's font atlas.  This is non-trivial because
    // ImGui manages its own font atlas texture.  For now we record the intent
    // so that the font stack concept is established for future integration.
    //
    // A proper implementation would:
    //   1. At font load time, add the TTF to ImGui's font atlas via
    //      ImFontAtlas::AddFontFromMemoryTTF.
    //   2. Store the ImFont* in FontData.
    //   3. Call ImGui::PushFont(imfont) here.
}

void drift_ui_pop_font(void) {
    if (g_font_stack.empty()) return;
    g_font_stack.pop_back();

    // See note in drift_ui_push_font -- would call ImGui::PopFont() here
    // once ImFont integration is complete.
}

// -----------------------------------------------------------------------------
// Anchor / positioning helpers
// -----------------------------------------------------------------------------

void drift_ui_set_anchor(DriftAnchor anchor) {
    g_current_anchor = anchor;
}

// -----------------------------------------------------------------------------
// Widgets
// -----------------------------------------------------------------------------

bool drift_ui_button(const char* label, DriftRect rect) {
    if (!g_initialised || !label) return false;

    begin_widget_window(label);

    ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));

    bool pressed = ImGui::Button(label, ImVec2(rect.w, rect.h));

    end_widget_window();
    return pressed;
}

void drift_ui_label(const char* text, DriftVec2 pos) {
    if (!g_initialised || !text) return;

    begin_widget_window("label");

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y));
    ImGui::TextUnformatted(text);

    end_widget_window();
}

void drift_ui_image(DriftTexture texture, DriftRect rect, DriftRect src_rect) {
    if (!g_initialised) return;

    begin_widget_window("image");

    ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));

    // ImTextureID is ImU64 in the SDL_GPU backend.
    ImTextureID tex_id = static_cast<ImTextureID>(texture.id);

    // Compute UV coordinates from the src_rect.
    // We need the full texture size to normalise.  For now, if src_rect is
    // zero-sized we assume full texture (0,0)->(1,1).
    ImVec2 uv0(0.0f, 0.0f);
    ImVec2 uv1(1.0f, 1.0f);

    if (src_rect.w > 0.0f && src_rect.h > 0.0f) {
        // Without knowing the full texture dimensions here, we treat src_rect
        // values as normalised UV if they are in [0,1] range, otherwise assume
        // pixel coordinates requiring the texture size.  For a proper
        // implementation, query drift_texture_get_size.
        int32_t tex_w = 0, tex_h = 0;
        drift_texture_get_size(texture, &tex_w, &tex_h);

        if (tex_w > 0 && tex_h > 0) {
            float fw = static_cast<float>(tex_w);
            float fh = static_cast<float>(tex_h);
            uv0 = ImVec2(src_rect.x / fw, src_rect.y / fh);
            uv1 = ImVec2((src_rect.x + src_rect.w) / fw,
                         (src_rect.y + src_rect.h) / fh);
        }
    }

    ImGui::Image(tex_id, ImVec2(rect.w, rect.h), uv0, uv1);

    end_widget_window();
}

bool drift_ui_slider(const char* label, float* value, float min_val, float max_val) {
    if (!g_initialised || !label || !value) return false;

    begin_widget_window(label);

    ImGui::SetCursorScreenPos(ImVec2(g_theme.padding, g_theme.padding));
    ImGui::PushItemWidth(200.0f);
    bool changed = ImGui::SliderFloat(label, value, min_val, max_val);
    ImGui::PopItemWidth();

    end_widget_window();
    return changed;
}

void drift_ui_progress_bar(float fraction, DriftRect rect) {
    if (!g_initialised) return;

    begin_widget_window("progress");

    ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));

    // Clamp fraction to [0, 1].
    float f = fraction;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;

    ImGui::ProgressBar(f, ImVec2(rect.w, rect.h));

    end_widget_window();
}

// -----------------------------------------------------------------------------
// Panels
// -----------------------------------------------------------------------------

void drift_ui_panel_begin(DriftRect rect, DriftPanelFlags flags) {
    if (!g_initialised) return;

    static int panel_counter = 0;
    char name[64];
    snprintf(name, sizeof(name), "##drift_panel_%d", panel_counter++);

    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h), ImGuiCond_Always);

    ImGuiWindowFlags win_flags =
        ImGuiWindowFlags_NoTitleBar   |
        ImGuiWindowFlags_NoResize     |
        ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoCollapse   |
        ImGuiWindowFlags_NoSavedSettings;

    if (flags & DRIFT_PANEL_NO_BG) {
        win_flags |= ImGuiWindowFlags_NoBackground;
    }

    if (!(flags & DRIFT_PANEL_SCROLLABLE)) {
        win_flags |= ImGuiWindowFlags_NoScrollbar;
        win_flags |= ImGuiWindowFlags_NoScrollWithMouse;
    }

    bool draw_border = (flags & DRIFT_PANEL_BORDER) != 0;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, draw_border ? 1.0f : 0.0f);

    ImGui::Begin(name, nullptr, win_flags);
}

void drift_ui_panel_end(void) {
    if (!g_initialised) return;

    ImGui::End();
    ImGui::PopStyleVar(); // WindowBorderSize pushed in panel_begin
}

// -----------------------------------------------------------------------------
// 9-slice
// -----------------------------------------------------------------------------

void drift_ui_9slice(DriftTexture texture, DriftRect rect, DriftRect borders) {
    if (!g_initialised) return;

    // borders: x = left, y = top, w = right, h = bottom (in pixels)
    int32_t tex_w = 0, tex_h = 0;
    drift_texture_get_size(texture, &tex_w, &tex_h);

    if (tex_w <= 0 || tex_h <= 0) return;

    float fw = static_cast<float>(tex_w);
    float fh = static_cast<float>(tex_h);

    ImTextureID tex_id = static_cast<ImTextureID>(texture.id);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImU32 white = IM_COL32(255, 255, 255, 255);

    float bl = borders.x;  // border left (pixels)
    float bt = borders.y;  // border top  (pixels)
    float br = borders.w;  // border right (pixels)
    float bb = borders.h;  // border bottom (pixels)

    // UV boundaries for the 9 slices.
    float ul = bl / fw;
    float ut = bt / fh;
    float ur = (fw - br) / fw;
    float ub = (fh - bb) / fh;

    // Destination boundaries.
    float dx0 = rect.x;
    float dy0 = rect.y;
    float dx1 = rect.x + bl;
    float dy1 = rect.y + bt;
    float dx2 = rect.x + rect.w - br;
    float dy2 = rect.y + rect.h - bb;
    float dx3 = rect.x + rect.w;
    float dy3 = rect.y + rect.h;

    // Draw 9 quads: top-left, top-center, top-right,
    //               mid-left, mid-center, mid-right,
    //               bot-left, bot-center, bot-right.

    // Row 0 (top)
    dl->AddImage(tex_id, ImVec2(dx0, dy0), ImVec2(dx1, dy1),
                 ImVec2(0.0f, 0.0f), ImVec2(ul, ut), white);
    dl->AddImage(tex_id, ImVec2(dx1, dy0), ImVec2(dx2, dy1),
                 ImVec2(ul, 0.0f), ImVec2(ur, ut), white);
    dl->AddImage(tex_id, ImVec2(dx2, dy0), ImVec2(dx3, dy1),
                 ImVec2(ur, 0.0f), ImVec2(1.0f, ut), white);

    // Row 1 (middle)
    dl->AddImage(tex_id, ImVec2(dx0, dy1), ImVec2(dx1, dy2),
                 ImVec2(0.0f, ut), ImVec2(ul, ub), white);
    dl->AddImage(tex_id, ImVec2(dx1, dy1), ImVec2(dx2, dy2),
                 ImVec2(ul, ut), ImVec2(ur, ub), white);
    dl->AddImage(tex_id, ImVec2(dx2, dy1), ImVec2(dx3, dy2),
                 ImVec2(ur, ut), ImVec2(1.0f, ub), white);

    // Row 2 (bottom)
    dl->AddImage(tex_id, ImVec2(dx0, dy2), ImVec2(dx1, dy3),
                 ImVec2(0.0f, ub), ImVec2(ul, 1.0f), white);
    dl->AddImage(tex_id, ImVec2(dx1, dy2), ImVec2(dx2, dy3),
                 ImVec2(ul, ub), ImVec2(ur, 1.0f), white);
    dl->AddImage(tex_id, ImVec2(dx2, dy2), ImVec2(dx3, dy3),
                 ImVec2(ur, ub), ImVec2(1.0f, 1.0f), white);
}

// -----------------------------------------------------------------------------
// Dev overlay
// -----------------------------------------------------------------------------

#ifdef DRIFT_DEV

void drift_ui_dev_toggle(void) {
    g_dev_visible = !g_dev_visible;
}

bool drift_ui_dev_is_visible(void) {
    return g_dev_visible;
}

void drift_ui_dev_render(void) {
    if (!g_initialised || !g_dev_visible) return;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 180), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.75f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("Drift Dev Overlay", &g_dev_visible, flags)) {
        ImGui::Text("FPS: %.1f (%.2f ms)", drift_get_fps(),
                     drift_get_fps() > 0.0f ? 1000.0f / drift_get_fps() : 0.0f);

        ImGui::Separator();

        ImGui::Text("Draw Calls:   %d", drift_renderer_get_draw_calls());
        ImGui::Text("Sprites:      %d", drift_renderer_get_sprite_count());
        ImGui::Text("Physics Bodies: %d", drift_physics_get_body_count());
        ImGui::Text("Frame:        %llu",
                     static_cast<unsigned long long>(drift_get_frame_count()));
        ImGui::Text("Time:         %.2f s", drift_get_time());

        ImGui::Separator();

        if (ImGui::Button("Close")) {
            g_dev_visible = false;
        }
    }
    ImGui::End();
}

#endif // DRIFT_DEV
