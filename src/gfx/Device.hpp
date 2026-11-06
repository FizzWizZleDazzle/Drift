#pragma once

// =============================================================================
// Graphics device seam.
//
// The renderer talks only to gfx::Device. Each backend (GL, WebGPU, ...) lives
// in its own directory under src/gfx/, implements Device, and registers a
// BackendInfo. CMake generates the registration list from the enabled
// backends, so nothing outside a backend's directory names it.
// =============================================================================

#include <drift/Types.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;
struct ImDrawData;

namespace drift {
struct Config;
}

namespace drift::gfx {

using TextureId = uint32_t;   // 0 = none / the screen when used as a pass target
using ShaderId  = uint32_t;   // 0 = the backend's default sprite shader

constexpr int kMaxTextureSlots = 4;
constexpr int kMaterialParamCount = 8;

enum class Blend : uint8_t {
    Alpha,          // straight alpha
    Additive,
    Multiply,
    Premultiplied,
    Screen,
    Opaque,
};

enum class Filter : uint8_t { Nearest, Linear };
enum class Wrap : uint8_t { Clamp, Repeat, Mirror };

struct TextureDesc {
    int32_t width  = 0;
    int32_t height = 0;
    Filter  filter = Filter::Nearest;
    Wrap    wrap   = Wrap::Clamp;
    bool    renderTarget = false;
};

// Vertex layout shared by every backend and every shader.
// location 0: position (world space), 1: uv, 2: color (unorm8x4).
struct Vertex {
    float   x, y;
    float   u, v;
    uint8_t r, g, b, a;
};

// Uniform block 0 (FrameUniforms). std140 / WGSL uniform compatible.
struct FrameUniforms {
    float viewProj[16];   // column-major mat4
    float time[4];        // x = seconds since start, y = delta, z = target width, w = target height
};

// Uniform block 1 (MaterialParams).
struct MaterialParams {
    float v[kMaterialParamCount][4] = {};
};

// A shader program. Backends read the field they understand:
// GL uses glslVertex/glslFragment (empty vertex = built-in vertex stage),
// WebGPU uses wgsl (a complete module with vs_main and fs_main).
struct ShaderSource {
    std::string glslVertex;
    std::string glslFragment;
    std::string wgsl;
};

struct DrawCall {
    ShaderId  shader = 0;
    Blend     blend  = Blend::Alpha;
    TextureId textures[kMaxTextureSlots] = {};
    uint32_t  firstIndex = 0;
    uint32_t  indexCount = 0;
    int32_t   paramsIndex = -1;     // index into the pass params array; -1 = all zero
    bool      scissor = false;
    int32_t   scissorRect[4] = {};  // x, y, w, h in target pixels, origin top-left
};

struct PassDesc {
    TextureId     target = 0;       // 0 = the window
    bool          clear  = true;
    ColorF        clearColor = {0.f, 0.f, 0.f, 1.f};
    int32_t       viewport[4] = {}; // x, y, w, h in target pixels, origin top-left; w == 0 = full target
    FrameUniforms frame{};
};

class Device {
public:
    virtual ~Device() = default;

    virtual const char* backendName() const = 0;

    // Textures. Pixels are RGBA8, row 0 = top. nullptr pixels = uninitialised.
    virtual TextureId createTexture(const TextureDesc& desc, const void* rgba) = 0;
    virtual void updateTexture(TextureId tex, int32_t x, int32_t y,
                               int32_t w, int32_t h, const void* rgba) = 0;
    virtual void setTextureSampling(TextureId tex, Filter filter, Wrap wrap) = 0;
    virtual void destroyTexture(TextureId tex) = 0;
    // Reads back a texture (or the window when tex == 0) as RGBA8, row 0 = top.
    virtual bool readPixels(TextureId tex, int32_t* w, int32_t* h, std::vector<uint8_t>& out) = 0;

    // Shaders. Returns 0 and fills *error on failure.
    virtual ShaderId createShader(const ShaderSource& src, std::string* error) = 0;
    virtual void destroyShader(ShaderId shader) = 0;

    // Frame. beginFrame returns false when nothing can be drawn this frame
    // (minimised window, lost surface); the caller skips to the next frame.
    virtual bool beginFrame() = 0;
    virtual void drawableSize(int32_t* w, int32_t* h) const = 0;
    virtual void submitPass(const PassDesc& pass,
                            const Vertex* vertices, uint32_t vertexCount,
                            const uint32_t* indices, uint32_t indexCount,
                            const DrawCall* calls, uint32_t callCount,
                            const MaterialParams* params, uint32_t paramCount) = 0;
    virtual void endFrame() = 0;

    // Dear ImGui renderer for this backend. imguiRender draws on top of the
    // window contents of the current frame.
    virtual bool imguiInit() = 0;
    virtual void imguiShutdown() = 0;
    virtual void imguiNewFrame() = 0;
    virtual void imguiRender(ImDrawData* data) = 0;
    // ImTextureID for a texture, so ImGui can draw it.
    virtual uint64_t imguiTexture(TextureId tex) const = 0;
};

struct BackendInfo {
    const char* name;
    // Called before the window exists (GL sets context attributes here).
    void (*prepareWindow)(const Config& cfg);
    // Extra SDL_WindowFlags the backend needs.
    uint64_t (*windowFlags)();
    std::unique_ptr<Device> (*create)(SDL_Window* window, const Config& cfg);
};

void registerBackend(const BackendInfo& info);
const std::vector<BackendInfo>& backends();

// Registers every backend compiled into this build (generated by CMake).
void registerBuiltinBackends();

} // namespace drift::gfx
