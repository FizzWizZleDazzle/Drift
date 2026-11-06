#include "gfx/wgpu/WebGPUSurface.hpp"

#include <drift/Log.hpp>

#include <SDL3/SDL.h>

#include <cstring>

namespace drift::gfx::webgpu {

WindowSurface createWindowSurface(WGPUInstance instance, SDL_Window* window) {
    WindowSurface out;
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    WGPUSurfaceDescriptor desc = WGPU_SURFACE_DESCRIPTOR_INIT;

#if defined(__EMSCRIPTEN__)
    // SDL stores the canvas as a CSS selector ("#canvas" by default).
    const char* selector = SDL_GetStringProperty(props, SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING, "#canvas");
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector src = {};
    src.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    src.selector = {selector, WGPU_STRLEN};
    desc.nextInChain = &src.chain;
    out.surface = wgpuInstanceCreateSurface(instance, &desc);

#elif defined(__APPLE__)
    (void)props;
    SDL_MetalView view = SDL_Metal_CreateView(window);
    if (!view) {
        DRIFT_LOG_ERROR("webgpu: SDL_Metal_CreateView failed: %s", SDL_GetError());
        return out;
    }
    WGPUSurfaceSourceMetalLayer src = {};
    src.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    src.layer = SDL_Metal_GetLayer(view);
    desc.nextInChain = &src.chain;
    out.surface = wgpuInstanceCreateSurface(instance, &desc);
    if (out.surface) out.metalView = view;
    else SDL_Metal_DestroyView(view);

#elif defined(_WIN32)
    WGPUSurfaceSourceWindowsHWND src = {};
    src.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    src.hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    src.hinstance = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
    if (!src.hwnd) {
        DRIFT_LOG_ERROR("webgpu: window has no HWND");
        return out;
    }
    desc.nextInChain = &src.chain;
    out.surface = wgpuInstanceCreateSurface(instance, &desc);

#else
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver && std::strcmp(driver, "x11") == 0) {
        WGPUSurfaceSourceXlibWindow src = {};
        src.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        src.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        src.window = static_cast<uint64_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        if (!src.display || !src.window) {
            DRIFT_LOG_ERROR("webgpu: X11 window handles unavailable");
            return out;
        }
        desc.nextInChain = &src.chain;
        out.surface = wgpuInstanceCreateSurface(instance, &desc);
    } else if (driver && std::strcmp(driver, "wayland") == 0) {
        WGPUSurfaceSourceWaylandSurface src = {};
        src.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
        src.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        src.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        if (!src.display || !src.surface) {
            DRIFT_LOG_ERROR("webgpu: Wayland window handles unavailable");
            return out;
        }
        desc.nextInChain = &src.chain;
        out.surface = wgpuInstanceCreateSurface(instance, &desc);
    } else {
        DRIFT_LOG_ERROR("webgpu: unsupported SDL video driver '%s'", driver ? driver : "(none)");
        return out;
    }
#endif

    if (!out.surface) DRIFT_LOG_ERROR("webgpu: surface creation failed");
    return out;
}

void destroyWindowSurface(WindowSurface& s) {
    if (s.surface) wgpuSurfaceRelease(s.surface);
#if defined(__APPLE__)
    if (s.metalView) SDL_Metal_DestroyView(static_cast<SDL_MetalView>(s.metalView));
#endif
    s = {};
}

} // namespace drift::gfx::webgpu
