#pragma once

// Creates a WebGPU surface for an SDL3 window: X11 or Wayland on Linux, a
// HWND on Windows, a CAMetalLayer on macOS, the canvas on the web.

#include <webgpu/webgpu.h>

struct SDL_Window;

namespace drift::gfx::webgpu {

struct WindowSurface {
    WGPUSurface surface = nullptr;
    void*       metalView = nullptr;   // SDL_MetalView on macOS
};

// Returns an empty WindowSurface (surface == nullptr) on failure and logs why.
WindowSurface createWindowSurface(WGPUInstance instance, SDL_Window* window);
void destroyWindowSurface(WindowSurface& s);

} // namespace drift::gfx::webgpu
