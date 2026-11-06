// =============================================================================
// WebGPU backend: wgpu-native on desktop, emdawnwebgpu in the browser.
//
// Conventions match the GL backend: textures are RGBA8 with row 0 = top, and
// a pass rendered into a texture comes out the same way up as one rendered to
// the window. WebGPU's framebuffer origin is already top-left with clip-space
// y up, so no projection flip is needed for either.
//
// Each submitPass records and submits its own command buffer. Vertex, index
// and uniform data go through queue writes, which the queue orders before the
// next submit, so one set of buffers serves every pass of a frame.
// =============================================================================

#include "gfx/Device.hpp"
#include "gfx/wgpu/WebGPUSurface.hpp"
#include "drift_shaders.gen.hpp"

#include <drift/Config.hpp>
#include <drift/Log.hpp>

#include <SDL3/SDL.h>

#include <webgpu/webgpu.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/em_asm.h>
#else
#include <webgpu/wgpu.h>
#endif

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace drift::gfx::webgpu {

namespace {

static_assert(sizeof(Vertex) == 20, "Vertex layout must match the pipeline vertex buffer layout");
static_assert(sizeof(FrameUniforms) == 80, "FrameUniforms must match the WGSL prelude");
static_assert(sizeof(MaterialParams) == 128, "MaterialParams must match the WGSL prelude");
static_assert(sizeof(ImDrawVert) == sizeof(Vertex), "ImGui vertices are drawn with the sprite layout");

constexpr WGPUTextureFormat kTextureFormat = WGPUTextureFormat_RGBA8Unorm;

WGPUStringView sv(const char* s) { return {s, WGPU_STRLEN}; }

std::string toString(WGPUStringView v) {
    if (!v.data) return {};
    return v.length == WGPU_STRLEN ? std::string(v.data) : std::string(v.data, v.length);
}

uint32_t alignUp(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }

// Draws a viewport-sized triangle in a flat color, for clears that cover
// only part of a target (render pass load ops always clear the whole target).
constexpr const char* kClearWGSL = R"(
struct V {
    @location(0) pos: vec2<f32>,
    @location(1) color: vec4<f32>,
};
struct O {
    @builtin(position) pos: vec4<f32>,
    @location(0) color: vec4<f32>,
};
@vertex fn vs_main(v: V) -> O {
    var o: O;
    o.pos = vec4<f32>(v.pos, 0.0, 1.0);
    o.color = v.color;
    return o;
}
@fragment fn fs_main(i: O) -> @location(0) vec4<f32> {
    return i.color;
}
)";

struct ClearVertex {
    float x, y;
    float r, g, b, a;
};

struct Texture {
    WGPUTexture     texture = nullptr;
    WGPUTextureView view = nullptr;
    int32_t width = 0;
    int32_t height = 0;
    bool    renderTarget = false;
    Filter  filter = Filter::Nearest;
    Wrap    wrap = Wrap::Clamp;
};

struct Shader {
    WGPUShaderModule module = nullptr;
};

struct TextureSetHash {
    size_t operator()(const std::array<TextureId, kMaxTextureSlots>& k) const {
        uint64_t h = 1469598103934665603ull;
        for (TextureId t : k) h = (h ^ t) * 1099511628211ull;
        return static_cast<size_t>(h);
    }
};

// Callback state for a single asynchronous request.
template <typename T>
struct Pending {
    bool        done = false;
    T           value = nullptr;
    std::string message;
};

class WebGPUDevice final : public Device {
public:
    WebGPUDevice(SDL_Window* window, const Config& cfg) : window_(window), vsync_(cfg.vsync) {}

    ~WebGPUDevice() override {
        releaseFrameTexture();
        clearBindGroupCache();
        for (auto& [k, p] : pipelines_) wgpuRenderPipelineRelease(p);
        for (auto& [id, t] : textures_) releaseTexture(t);
        for (auto& [id, s] : shaders_) wgpuShaderModuleRelease(s.module);
        if (defaultShader_.module) wgpuShaderModuleRelease(defaultShader_.module);
        for (auto& [k, p] : clearPipelines_) wgpuRenderPipelineRelease(p);
        if (clearModule_) wgpuShaderModuleRelease(clearModule_);
        releaseTexture(dummy_);
        for (WGPUSampler s : samplers_) if (s) wgpuSamplerRelease(s);
        if (group0_) wgpuBindGroupRelease(group0_);
        for (WGPUBuffer b : {vertexBuf_, indexBuf_, frameBuf_, paramsBuf_, clearBuf_}) {
            if (b) wgpuBufferRelease(b);
        }
        if (pipelineLayout_) wgpuPipelineLayoutRelease(pipelineLayout_);
        if (clearLayout_) wgpuPipelineLayoutRelease(clearLayout_);
        if (layout0_) wgpuBindGroupLayoutRelease(layout0_);
        if (layout1_) wgpuBindGroupLayoutRelease(layout1_);
        if (surface_.surface && configured_) wgpuSurfaceUnconfigure(surface_.surface);
        destroyWindowSurface(surface_);
        if (queue_) wgpuQueueRelease(queue_);
        if (device_) wgpuDeviceRelease(device_);
        if (adapter_) wgpuAdapterRelease(adapter_);
        if (instance_) wgpuInstanceRelease(instance_);
    }

    bool init() {
#ifdef __EMSCRIPTEN__
        if (!EM_ASM_INT({ return navigator.gpu ? 1 : 0; })) {
            DRIFT_LOG_WARN("webgpu: this browser does not support WebGPU");
            return false;
        }
#endif
        WGPUInstanceDescriptor idesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
#ifdef __EMSCRIPTEN__
        // Blocking waits on futures (with ASYNCIFY) need this instance feature.
        WGPUInstanceFeatureName timedWait = WGPUInstanceFeatureName_TimedWaitAny;
        idesc.requiredFeatureCount = 1;
        idesc.requiredFeatures = &timedWait;
#endif
        instance_ = wgpuCreateInstance(&idesc);
        if (!instance_) {
            DRIFT_LOG_ERROR("webgpu: no WebGPU instance (is WebGPU available?)");
            return false;
        }

#ifndef __EMSCRIPTEN__
        // On the desktop the surface comes first so the adapter can present to it.
        // In the browser it comes last: creating it claims the canvas, which
        // would stop the GL backend from taking over if WebGPU fails below.
        surface_ = createWindowSurface(instance_, window_);
        if (!surface_.surface) return false;
#endif

        if (!requestAdapter()) return false;
        if (!requestDevice()) return false;
        queue_ = wgpuDeviceGetQueue(device_);

#ifdef __EMSCRIPTEN__
        surface_ = createWindowSurface(instance_, window_);
        if (!surface_.surface) return false;
#endif

        WGPULimits limits = WGPU_LIMITS_INIT;
        if (wgpuDeviceGetLimits(device_, &limits) == WGPUStatus_Success && limits.minUniformBufferOffsetAlignment > 0) {
            uniformAlign_ = limits.minUniformBufferOffsetAlignment;
        }

        if (!chooseSurfaceConfig()) return false;
        int32_t w = 0, h = 0;
        drawableSize(&w, &h);
        if (w > 0 && h > 0) configureSurface(w, h);

        createLayouts();
        createStaticResources();

        std::string error;
        defaultShader_.module = compileModule(std::string(shaders::prelude_wgsl) + "\n" + shaders::sprite_wgsl, &error);
        if (!defaultShader_.module) {
            DRIFT_LOG_ERROR("webgpu: default shader failed: %s", error.c_str());
            return false;
        }
        clearModule_ = compileModule(kClearWGSL, &error);
        if (!clearModule_) {
            DRIFT_LOG_ERROR("webgpu: clear shader failed: %s", error.c_str());
            return false;
        }
        return true;
    }

    const char* backendName() const override { return "webgpu"; }

    // ---- Textures ----------------------------------------------------------

    TextureId createTexture(const TextureDesc& desc, const void* rgba) override {
        if (desc.width <= 0 || desc.height <= 0) return 0;
        Texture t = makeTexture(desc.width, desc.height, desc.renderTarget);
        if (!t.texture) return 0;
        t.filter = desc.filter;
        t.wrap = desc.wrap;
        if (rgba) writePixels(t, 0, 0, desc.width, desc.height, rgba);
        TextureId id = nextId_++;
        textures_[id] = t;
        viewToTexture_[t.view] = id;
        return id;
    }

    void updateTexture(TextureId id, int32_t x, int32_t y, int32_t w, int32_t h,
                       const void* rgba) override {
        auto it = textures_.find(id);
        if (it == textures_.end() || !rgba || w <= 0 || h <= 0) return;
        const Texture& t = it->second;
        if (x < 0 || y < 0 || x + w > t.width || y + h > t.height) return;
        writePixels(t, x, y, w, h, rgba);
    }

    void setTextureSampling(TextureId id, Filter filter, Wrap wrap) override {
        auto it = textures_.find(id);
        if (it == textures_.end()) return;
        if (it->second.filter == filter && it->second.wrap == wrap) return;
        it->second.filter = filter;
        it->second.wrap = wrap;
        forgetBindGroupsUsing(id);
    }

    void destroyTexture(TextureId id) override {
        auto it = textures_.find(id);
        if (it == textures_.end()) return;
        forgetBindGroupsUsing(id);
        viewToTexture_.erase(it->second.view);
        releaseTexture(it->second);
        textures_.erase(it);
    }

    bool readPixels(TextureId id, int32_t* w, int32_t* h, std::vector<uint8_t>& out) override {
        WGPUTexture src = nullptr;
        int32_t tw = 0, th = 0;
        bool bgra = false;
        if (id == 0) {
            if (!frameTexture_ || !surfaceCopySrc_) return false;
            src = frameTexture_;
            tw = static_cast<int32_t>(surfaceW_);
            th = static_cast<int32_t>(surfaceH_);
            bgra = surfaceFormat_ == WGPUTextureFormat_BGRA8Unorm;
        } else {
            auto it = textures_.find(id);
            if (it == textures_.end() || !it->second.renderTarget) return false;
            src = it->second.texture;
            tw = it->second.width;
            th = it->second.height;
        }

        uint32_t rowBytes = static_cast<uint32_t>(tw) * 4;
        uint32_t stride = alignUp(rowBytes, 256);
        uint64_t size = static_cast<uint64_t>(stride) * th;

        WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
        bd.label = sv("drift readback");
        bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        bd.size = size;
        WGPUBuffer buf = wgpuDeviceCreateBuffer(device_, &bd);
        if (!buf) return false;

        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device_, nullptr);
        WGPUTexelCopyTextureInfo from = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        from.texture = src;
        WGPUTexelCopyBufferInfo to = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        to.buffer = buf;
        to.layout.bytesPerRow = stride;
        to.layout.rowsPerImage = static_cast<uint32_t>(th);
        WGPUExtent3D extent = {static_cast<uint32_t>(tw), static_cast<uint32_t>(th), 1};
        wgpuCommandEncoderCopyTextureToBuffer(enc, &from, &to, &extent);
        submit(enc);

        struct MapState { bool done = false; bool ok = false; };
        MapState state;
        WGPUBufferMapCallbackInfo cb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        cb.mode = kCallbackMode;
        cb.callback = [](WGPUMapAsyncStatus status, WGPUStringView message, void* ud1, void*) {
            auto* s = static_cast<MapState*>(ud1);
            s->ok = status == WGPUMapAsyncStatus_Success;
            if (!s->ok) DRIFT_LOG_ERROR("webgpu: readback failed: %s", toString(message).c_str());
            s->done = true;
        };
        cb.userdata1 = &state;
        WGPUFuture f = wgpuBufferMapAsync(buf, WGPUMapMode_Read, 0, static_cast<size_t>(size), cb);
        wait(f, state.done);

        bool ok = state.ok;
        if (ok) {
            const auto* data = static_cast<const uint8_t*>(
                wgpuBufferGetConstMappedRange(buf, 0, static_cast<size_t>(size)));
            ok = data != nullptr;
            if (ok) {
                out.resize(static_cast<size_t>(rowBytes) * th);
                for (int32_t y = 0; y < th; ++y) {
                    std::memcpy(out.data() + static_cast<size_t>(y) * rowBytes,
                                data + static_cast<size_t>(y) * stride, rowBytes);
                }
                if (bgra) {
                    for (size_t i = 0; i < out.size(); i += 4) std::swap(out[i], out[i + 2]);
                }
            }
            wgpuBufferUnmap(buf);
        }
        wgpuBufferRelease(buf);
        if (!ok) return false;
        *w = tw;
        *h = th;
        return true;
    }

    // ---- Shaders -----------------------------------------------------------

    ShaderId createShader(const ShaderSource& src, std::string* error) override {
        if (src.wgsl.empty()) {
            if (error) *error = "shader has no WGSL source";
            return 0;
        }
        WGPUShaderModule module = compileModule(src.wgsl, error);
        if (!module) return 0;

        // Building one pipeline checks the module against the prelude's
        // bind group layout, so mismatches surface here, not at draw time.
        wgpuDevicePushErrorScope(device_, WGPUErrorFilter_Validation);
        WGPURenderPipeline probe = buildPipeline(module, Blend::Alpha, kTextureFormat);
        std::string message;
        if (popErrorScope(&message)) {
            if (error) *error = message;
            if (probe) wgpuRenderPipelineRelease(probe);
            wgpuShaderModuleRelease(module);
            return 0;
        }

        ShaderId id = nextId_++;
        shaders_[id] = {module};
        pipelines_[pipelineKey(id, Blend::Alpha, kTextureFormat)] = probe;
        return id;
    }

    void destroyShader(ShaderId id) override {
        auto it = shaders_.find(id);
        if (it == shaders_.end()) return;
        for (auto p = pipelines_.begin(); p != pipelines_.end();) {
            if (static_cast<ShaderId>(p->first & 0xffffffffu) == id) {
                wgpuRenderPipelineRelease(p->second);
                p = pipelines_.erase(p);
            } else {
                ++p;
            }
        }
        wgpuShaderModuleRelease(it->second.module);
        shaders_.erase(it);
    }

    // ---- Frame -------------------------------------------------------------

    bool beginFrame() override {
        releaseFrameTexture();
        int32_t w = 0, h = 0;
        drawableSize(&w, &h);
        if (w <= 0 || h <= 0) return false;
        if (!configured_ || static_cast<uint32_t>(w) != surfaceW_ || static_cast<uint32_t>(h) != surfaceH_ ||
            reconfigure_) {
            configureSurface(w, h);
        }

        WGPUSurfaceTexture st = WGPU_SURFACE_TEXTURE_INIT;
        wgpuSurfaceGetCurrentTexture(surface_.surface, &st);
        switch (st.status) {
        case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
            break;
        case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
            reconfigure_ = true;   // draw this frame, reconfigure on the next
            break;
        case WGPUSurfaceGetCurrentTextureStatus_Outdated:
        case WGPUSurfaceGetCurrentTextureStatus_Lost:
            if (st.texture) wgpuTextureRelease(st.texture);
            configureSurface(w, h);
            return false;
        default:   // timeout, occluded, error
            if (st.texture) wgpuTextureRelease(st.texture);
            return false;
        }
        frameTexture_ = st.texture;
        frameView_ = wgpuTextureCreateView(frameTexture_, nullptr);
        return frameView_ != nullptr;
    }

    void drawableSize(int32_t* w, int32_t* h) const override {
        int iw = 0, ih = 0;
        SDL_GetWindowSizeInPixels(window_, &iw, &ih);
        *w = iw;
        *h = ih;
    }

    void submitPass(const PassDesc& pass,
                    const Vertex* vertices, uint32_t vertexCount,
                    const uint32_t* indices, uint32_t indexCount,
                    const DrawCall* calls, uint32_t callCount,
                    const MaterialParams* params, uint32_t paramCount) override {
        WGPUTextureView targetView = nullptr;
        WGPUTextureFormat format = kTextureFormat;
        int32_t tw = 0, th = 0;
        if (pass.target != 0) {
            auto it = textures_.find(pass.target);
            if (it == textures_.end() || !it->second.renderTarget) return;
            targetView = it->second.view;
            tw = it->second.width;
            th = it->second.height;
        } else {
            if (!frameView_) return;
            targetView = frameView_;
            format = surfaceFormat_;
            tw = static_cast<int32_t>(surfaceW_);
            th = static_cast<int32_t>(surfaceH_);
        }

        int32_t vx = 0, vy = 0, vw = tw, vh = th;
        if (pass.viewport[2] > 0 && pass.viewport[3] > 0) {
            vx = pass.viewport[0];
            vy = pass.viewport[1];
            vw = pass.viewport[2];
            vh = pass.viewport[3];
        }
        bool wholeTarget = vx <= 0 && vy <= 0 && vx + vw >= tw && vy + vh >= th;

        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = targetView;
        color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color.loadOp = pass.clear && wholeTarget ? WGPULoadOp_Clear : WGPULoadOp_Load;
        color.storeOp = WGPUStoreOp_Store;
        color.clearValue = {pass.clearColor.r, pass.clearColor.g, pass.clearColor.b, pass.clearColor.a};
        WGPURenderPassDescriptor rp = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &color;

        bool draws = callCount > 0 && indexCount > 0 && vertexCount > 0;
        if (draws) uploadPassData(pass, vertices, vertexCount, indices, indexCount, params, paramCount);

        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device_, nullptr);
        WGPURenderPassEncoder rpe = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        wgpuRenderPassEncoderSetViewport(rpe, static_cast<float>(vx), static_cast<float>(vy),
                                         static_cast<float>(vw), static_cast<float>(vh), 0.f, 1.f);

        int32_t full[4] = {0, 0, tw, th};
        if (pass.clear && !wholeTarget) {
            int32_t r[4];
            if (clampRect(vx, vy, vw, vh, tw, th, r)) {
                ClearVertex cv[3] = {
                    {-1.f, -1.f, pass.clearColor.r, pass.clearColor.g, pass.clearColor.b, pass.clearColor.a},
                    { 3.f, -1.f, pass.clearColor.r, pass.clearColor.g, pass.clearColor.b, pass.clearColor.a},
                    {-1.f,  3.f, pass.clearColor.r, pass.clearColor.g, pass.clearColor.b, pass.clearColor.a},
                };
                wgpuQueueWriteBuffer(queue_, clearBuf_, 0, cv, sizeof(cv));
                wgpuRenderPassEncoderSetPipeline(rpe, clearPipeline(format));
                wgpuRenderPassEncoderSetVertexBuffer(rpe, 0, clearBuf_, 0, sizeof(cv));
                wgpuRenderPassEncoderSetScissorRect(rpe, r[0], r[1], r[2], r[3]);
                wgpuRenderPassEncoderDraw(rpe, 3, 1, 0, 0);
                setScissor(rpe, full);
            }
        }

        if (draws) {
            wgpuRenderPassEncoderSetVertexBuffer(rpe, 0, vertexBuf_, 0, uint64_t(vertexCount) * sizeof(Vertex));
            wgpuRenderPassEncoderSetIndexBuffer(rpe, indexBuf_, WGPUIndexFormat_Uint32, 0,
                                                uint64_t(indexCount) * sizeof(uint32_t));
            WGPURenderPipeline curPipeline = nullptr;
            WGPUBindGroup curTextures = nullptr;
            bool scissorOn = false;

            for (uint32_t i = 0; i < callCount; ++i) {
                const DrawCall& dc = calls[i];
                if (dc.indexCount == 0) continue;

                ShaderId sid = dc.shader;
                WGPUShaderModule module = defaultShader_.module;
                if (sid != 0) {
                    auto it = shaders_.find(sid);
                    if (it != shaders_.end()) module = it->second.module;
                    else sid = 0;
                }
                WGPURenderPipeline pipe = pipeline(sid, module, dc.blend, format);
                if (!pipe) continue;
                if (pipe != curPipeline) {
                    wgpuRenderPassEncoderSetPipeline(rpe, pipe);
                    curPipeline = pipe;
                }

                uint32_t slot = (dc.paramsIndex >= 0 && static_cast<uint32_t>(dc.paramsIndex) < paramCount)
                    ? static_cast<uint32_t>(dc.paramsIndex) : paramCount;
                uint32_t offset = slot * paramStride_;
                wgpuRenderPassEncoderSetBindGroup(rpe, 0, group0_, 1, &offset);

                WGPUBindGroup tg = textureGroup(dc.textures);
                if (tg != curTextures) {
                    wgpuRenderPassEncoderSetBindGroup(rpe, 1, tg, 0, nullptr);
                    curTextures = tg;
                }

                if (dc.scissor) {
                    int32_t r[4];
                    if (!clampRect(dc.scissorRect[0], dc.scissorRect[1], dc.scissorRect[2],
                                   dc.scissorRect[3], tw, th, r)) {
                        continue;   // empty scissor: nothing to draw
                    }
                    setScissor(rpe, r);
                    scissorOn = true;
                } else if (scissorOn) {
                    setScissor(rpe, full);
                    scissorOn = false;
                }

                wgpuRenderPassEncoderDrawIndexed(rpe, dc.indexCount, 1, dc.firstIndex, 0, 0);
            }
        }

        wgpuRenderPassEncoderEnd(rpe);
        wgpuRenderPassEncoderRelease(rpe);
        submit(enc);
    }

    void endFrame() override {
        if (frameTexture_) {
#ifndef __EMSCRIPTEN__
            wgpuSurfacePresent(surface_.surface);   // the browser presents on its own
#endif
        }
        releaseFrameTexture();
#ifndef __EMSCRIPTEN__
        wgpuDevicePoll(device_, false, nullptr);
#endif
    }

    // ---- ImGui -------------------------------------------------------------
    //
    // ImGui's vertex layout (pos, uv, rgba8) is the engine's Vertex layout, so
    // draw lists go through submitPass with the default sprite shader.

    bool imguiInit() override {
        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = "drift_webgpu";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        return true;
    }

    void imguiShutdown() override {
        ImGuiIO& io = ImGui::GetIO();
        if (fontTexture_) {
            destroyTexture(fontTexture_);
            fontTexture_ = 0;
            io.Fonts->SetTexID(0);
        }
        io.BackendRendererName = nullptr;
        io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
    }

    void imguiNewFrame() override {
        if (fontTexture_) return;
        ImGuiIO& io = ImGui::GetIO();
        unsigned char* pixels = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        TextureDesc d;
        d.width = w;
        d.height = h;
        d.filter = Filter::Linear;
        d.wrap = Wrap::Clamp;
        fontTexture_ = createTexture(d, pixels);
        io.Fonts->SetTexID(static_cast<ImTextureID>(imguiTexture(fontTexture_)));
    }

    void imguiRender(ImDrawData* data) override {
        if (!frameView_ || !data || data->CmdListsCount == 0) return;
        int32_t fbW = static_cast<int32_t>(data->DisplaySize.x * data->FramebufferScale.x);
        int32_t fbH = static_cast<int32_t>(data->DisplaySize.y * data->FramebufferScale.y);
        if (fbW <= 0 || fbH <= 0) return;

        imguiVerts_.clear();
        imguiIndices_.clear();
        imguiCalls_.clear();
        const ImVec2 pos = data->DisplayPos;
        const ImVec2 scale = data->FramebufferScale;

        for (int n = 0; n < data->CmdListsCount; ++n) {
            const ImDrawList* list = data->CmdLists[n];
            uint32_t base = static_cast<uint32_t>(imguiVerts_.size());
            imguiVerts_.resize(base + static_cast<size_t>(list->VtxBuffer.Size));
            std::memcpy(imguiVerts_.data() + base, list->VtxBuffer.Data,
                        static_cast<size_t>(list->VtxBuffer.Size) * sizeof(Vertex));

            for (const ImDrawCmd& cmd : list->CmdBuffer) {
                if (cmd.UserCallback) {
                    if (cmd.UserCallback != ImDrawCallback_ResetRenderState) cmd.UserCallback(list, &cmd);
                    continue;
                }
                float x0 = (cmd.ClipRect.x - pos.x) * scale.x;
                float y0 = (cmd.ClipRect.y - pos.y) * scale.y;
                float x1 = (cmd.ClipRect.z - pos.x) * scale.x;
                float y1 = (cmd.ClipRect.w - pos.y) * scale.y;
                if (x1 <= x0 || y1 <= y0) continue;

                auto tex = viewToTexture_.find(reinterpret_cast<WGPUTextureView>(
                    static_cast<uintptr_t>(cmd.GetTexID())));
                if (tex == viewToTexture_.end()) continue;

                DrawCall dc;
                dc.blend = Blend::Alpha;
                dc.textures[0] = tex->second;
                dc.firstIndex = static_cast<uint32_t>(imguiIndices_.size());
                dc.indexCount = cmd.ElemCount;
                dc.scissor = true;
                dc.scissorRect[0] = static_cast<int32_t>(x0);
                dc.scissorRect[1] = static_cast<int32_t>(y0);
                dc.scissorRect[2] = static_cast<int32_t>(x1) - dc.scissorRect[0];
                dc.scissorRect[3] = static_cast<int32_t>(y1) - dc.scissorRect[1];
                uint32_t offset = base + cmd.VtxOffset;
                for (unsigned int e = 0; e < cmd.ElemCount; ++e) {
                    imguiIndices_.push_back(offset + list->IdxBuffer[static_cast<int>(cmd.IdxOffset + e)]);
                }
                imguiCalls_.push_back(dc);
            }
        }
        if (imguiCalls_.empty()) return;

        float l = pos.x, r = pos.x + data->DisplaySize.x;
        float t = pos.y, b = pos.y + data->DisplaySize.y;
        PassDesc pd;
        pd.target = 0;
        pd.clear = false;
        pd.viewport[2] = fbW;
        pd.viewport[3] = fbH;
        std::memset(pd.frame.viewProj, 0, sizeof(pd.frame.viewProj));
        pd.frame.viewProj[0] = 2.f / (r - l);
        pd.frame.viewProj[5] = 2.f / (t - b);
        pd.frame.viewProj[10] = 1.f;
        pd.frame.viewProj[12] = (r + l) / (l - r);
        pd.frame.viewProj[13] = (t + b) / (b - t);
        pd.frame.viewProj[15] = 1.f;
        pd.frame.time[2] = static_cast<float>(fbW);
        pd.frame.time[3] = static_cast<float>(fbH);
        submitPass(pd, imguiVerts_.data(), static_cast<uint32_t>(imguiVerts_.size()),
                   imguiIndices_.data(), static_cast<uint32_t>(imguiIndices_.size()),
                   imguiCalls_.data(), static_cast<uint32_t>(imguiCalls_.size()), nullptr, 0);
    }

    // The texture view, as imgui_impl_wgpu uses for ImTextureID.
    uint64_t imguiTexture(TextureId id) const override {
        auto it = textures_.find(id);
        return it != textures_.end() ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(it->second.view)) : 0;
    }

private:
#ifdef __EMSCRIPTEN__
    static constexpr WGPUCallbackMode kCallbackMode = WGPUCallbackMode_WaitAnyOnly;
#else
    static constexpr WGPUCallbackMode kCallbackMode = WGPUCallbackMode_AllowProcessEvents;
#endif

    // Blocks until `done` is set by the callback attached to `future`.
    void wait(WGPUFuture future, const bool& done) {
#ifdef __EMSCRIPTEN__
        if (done) return;
        WGPUFutureWaitInfo info = {future, false};
        wgpuInstanceWaitAny(instance_, 1, &info, UINT64_MAX);
        // The wait yielded to the browser, which presented the canvas and
        // expired its texture; the rest of this frame cannot draw to it.
        releaseFrameTexture();
#else
        (void)future;
        uint64_t start = SDL_GetTicks();
        while (!done) {
            if (device_) wgpuDevicePoll(device_, true, nullptr);
            wgpuInstanceProcessEvents(instance_);
            if (!done && SDL_GetTicks() - start > 10000) {
                DRIFT_LOG_ERROR("webgpu: timed out waiting for the GPU");
                return;
            }
        }
#endif
    }

    bool requestAdapter() {
        WGPURequestAdapterOptions opts = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        opts.powerPreference = WGPUPowerPreference_HighPerformance;
        opts.compatibleSurface = surface_.surface;
        Pending<WGPUAdapter> p;
        WGPURequestAdapterCallbackInfo cb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
        cb.mode = kCallbackMode;
        cb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                         void* ud1, void*) {
            auto* s = static_cast<Pending<WGPUAdapter>*>(ud1);
            if (status == WGPURequestAdapterStatus_Success) s->value = adapter;
            else s->message = toString(message);
            s->done = true;
        };
        cb.userdata1 = &p;
        wait(wgpuInstanceRequestAdapter(instance_, &opts, cb), p.done);
        adapter_ = p.value;
        if (!adapter_) {
            DRIFT_LOG_ERROR("webgpu: no suitable adapter%s%s", p.message.empty() ? "" : ": ", p.message.c_str());
            return false;
        }
        WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
        if (wgpuAdapterGetInfo(adapter_, &info) == WGPUStatus_Success) {
            DRIFT_LOG_INFO("webgpu: adapter '%s' (%s)", toString(info.device).c_str(),
                           backendTypeName(info.backendType));
            wgpuAdapterInfoFreeMembers(info);
        }
        return true;
    }

    bool requestDevice() {
        WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
        dd.label = sv("drift");
        dd.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dd.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason reason,
                                                WGPUStringView message, void*, void*) {
            if (reason != WGPUDeviceLostReason_Destroyed && reason != WGPUDeviceLostReason_CallbackCancelled) {
                DRIFT_LOG_ERROR("webgpu: device lost: %s", toString(message).c_str());
            }
        };
        dd.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType, WGPUStringView message,
                                                     void*, void*) {
            DRIFT_LOG_ERROR("webgpu: %s", toString(message).c_str());
        };
        Pending<WGPUDevice> p;
        WGPURequestDeviceCallbackInfo cb = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
        cb.mode = kCallbackMode;
        cb.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                         void* ud1, void*) {
            auto* s = static_cast<Pending<WGPUDevice>*>(ud1);
            if (status == WGPURequestDeviceStatus_Success) s->value = device;
            else s->message = toString(message);
            s->done = true;
        };
        cb.userdata1 = &p;
        wait(wgpuAdapterRequestDevice(adapter_, &dd, cb), p.done);
        device_ = p.value;
        if (!device_) {
            DRIFT_LOG_ERROR("webgpu: device request failed: %s", p.message.c_str());
            return false;
        }
        return true;
    }

    static const char* backendTypeName(WGPUBackendType t) {
        switch (t) {
        case WGPUBackendType_Vulkan: return "Vulkan";
        case WGPUBackendType_Metal: return "Metal";
        case WGPUBackendType_D3D12: return "D3D12";
        case WGPUBackendType_D3D11: return "D3D11";
        case WGPUBackendType_OpenGL: return "OpenGL";
        case WGPUBackendType_OpenGLES: return "OpenGL ES";
        case WGPUBackendType_WebGPU: return "browser WebGPU";
        default: return "unknown backend";
        }
    }

    bool chooseSurfaceConfig() {
        WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
        if (wgpuSurfaceGetCapabilities(surface_.surface, adapter_, &caps) != WGPUStatus_Success ||
            caps.formatCount == 0) {
            DRIFT_LOG_ERROR("webgpu: surface is not supported by the adapter");
            return false;
        }
        // A linear (non-sRGB) 8-bit format, like the GL default framebuffer.
        // The first one listed is the platform's preferred format.
        surfaceFormat_ = caps.formats[0];
        for (size_t i = 0; i < caps.formatCount; ++i) {
            if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm || caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
                surfaceFormat_ = caps.formats[i];
                break;
            }
        }

        auto hasMode = [&](WGPUPresentMode m) {
            for (size_t i = 0; i < caps.presentModeCount; ++i) if (caps.presentModes[i] == m) return true;
            return false;
        };
        presentMode_ = WGPUPresentMode_Fifo;   // always supported; vsync
        if (!vsync_) {
            if (hasMode(WGPUPresentMode_Immediate)) presentMode_ = WGPUPresentMode_Immediate;
            else if (hasMode(WGPUPresentMode_Mailbox)) presentMode_ = WGPUPresentMode_Mailbox;
        }

        alphaMode_ = WGPUCompositeAlphaMode_Auto;
        for (size_t i = 0; i < caps.alphaModeCount; ++i) {
            if (caps.alphaModes[i] == WGPUCompositeAlphaMode_Opaque) alphaMode_ = WGPUCompositeAlphaMode_Opaque;
        }
#ifdef __EMSCRIPTEN__
        surfaceCopySrc_ = true;   // canvases accept any usage; the port does not report them
#else
        surfaceCopySrc_ = (caps.usages & WGPUTextureUsage_CopySrc) != 0;
#endif
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        return true;
    }

    void configureSurface(int32_t w, int32_t h) {
        WGPUSurfaceConfiguration sc = WGPU_SURFACE_CONFIGURATION_INIT;
        sc.device = device_;
        sc.format = surfaceFormat_;
        sc.usage = WGPUTextureUsage_RenderAttachment | (surfaceCopySrc_ ? WGPUTextureUsage_CopySrc : 0);
        sc.width = static_cast<uint32_t>(w);
        sc.height = static_cast<uint32_t>(h);
        sc.alphaMode = alphaMode_;
        sc.presentMode = presentMode_;
        wgpuSurfaceConfigure(surface_.surface, &sc);
        surfaceW_ = sc.width;
        surfaceH_ = sc.height;
        configured_ = true;
        reconfigure_ = false;
    }

    void releaseFrameTexture() {
        if (frameView_) wgpuTextureViewRelease(frameView_);
        if (frameTexture_) wgpuTextureRelease(frameTexture_);
        frameView_ = nullptr;
        frameTexture_ = nullptr;
    }

    void createLayouts() {
        WGPUBindGroupLayoutEntry e0[2] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
        for (int i = 0; i < 2; ++i) {
            e0[i].binding = static_cast<uint32_t>(i);
            e0[i].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            e0[i].buffer.type = WGPUBufferBindingType_Uniform;
        }
        e0[0].buffer.minBindingSize = sizeof(FrameUniforms);
        e0[1].buffer.hasDynamicOffset = true;
        e0[1].buffer.minBindingSize = sizeof(MaterialParams);
        WGPUBindGroupLayoutDescriptor d0 = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        d0.entryCount = 2;
        d0.entries = e0;
        layout0_ = wgpuDeviceCreateBindGroupLayout(device_, &d0);

        WGPUBindGroupLayoutEntry e1[kMaxTextureSlots * 2];
        for (int s = 0; s < kMaxTextureSlots; ++s) {
            WGPUBindGroupLayoutEntry& t = e1[s * 2];
            WGPUBindGroupLayoutEntry& m = e1[s * 2 + 1];
            t = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            m = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            t.binding = static_cast<uint32_t>(s * 2);
            t.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            t.texture.sampleType = WGPUTextureSampleType_Float;
            t.texture.viewDimension = WGPUTextureViewDimension_2D;
            m.binding = static_cast<uint32_t>(s * 2 + 1);
            m.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            m.sampler.type = WGPUSamplerBindingType_Filtering;
        }
        WGPUBindGroupLayoutDescriptor d1 = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        d1.entryCount = kMaxTextureSlots * 2;
        d1.entries = e1;
        layout1_ = wgpuDeviceCreateBindGroupLayout(device_, &d1);

        WGPUBindGroupLayout layouts[2] = {layout0_, layout1_};
        WGPUPipelineLayoutDescriptor pd = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        pd.bindGroupLayoutCount = 2;
        pd.bindGroupLayouts = layouts;
        pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pd);

        WGPUPipelineLayoutDescriptor cd = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        clearLayout_ = wgpuDeviceCreatePipelineLayout(device_, &cd);
    }

    void createStaticResources() {
        paramStride_ = alignUp(sizeof(MaterialParams), uniformAlign_);
        frameBuf_ = makeBuffer(sizeof(FrameUniforms), WGPUBufferUsage_Uniform, "drift frame uniforms");
        clearBuf_ = makeBuffer(sizeof(ClearVertex) * 3, WGPUBufferUsage_Vertex, "drift clear");
        ensureParamsCapacity(16);

        // Empty texture slots read opaque black, as an unbound GL texture does.
        dummy_ = makeTexture(1, 1, false);
        const uint8_t black[4] = {0, 0, 0, 255};
        writePixels(dummy_, 0, 0, 1, 1, black);
    }

    WGPUBuffer makeBuffer(uint64_t size, WGPUBufferUsage usage, const char* label) {
        WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
        bd.label = sv(label);
        bd.usage = usage | WGPUBufferUsage_CopyDst;
        bd.size = size;
        return wgpuDeviceCreateBuffer(device_, &bd);
    }

    void ensureBuffer(WGPUBuffer& buf, uint64_t& capacity, uint64_t size, WGPUBufferUsage usage, const char* label) {
        if (buf && capacity >= size) return;
        uint64_t cap = std::max<uint64_t>(capacity, 4096);
        while (cap < size) cap *= 2;
        if (buf) wgpuBufferRelease(buf);
        buf = makeBuffer(cap, usage, label);
        capacity = cap;
    }

    void ensureParamsCapacity(uint32_t slots) {
        if (paramsBuf_ && paramSlots_ >= slots) return;
        uint32_t n = std::max<uint32_t>(paramSlots_, 16);
        while (n < slots) n *= 2;
        if (paramsBuf_) wgpuBufferRelease(paramsBuf_);
        paramsBuf_ = makeBuffer(uint64_t(n) * paramStride_, WGPUBufferUsage_Uniform, "drift material params");
        paramSlots_ = n;

        if (group0_) wgpuBindGroupRelease(group0_);
        WGPUBindGroupEntry e[2] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
        e[0].binding = 0;
        e[0].buffer = frameBuf_;
        e[0].size = sizeof(FrameUniforms);
        e[1].binding = 1;
        e[1].buffer = paramsBuf_;
        e[1].size = sizeof(MaterialParams);
        WGPUBindGroupDescriptor d = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        d.layout = layout0_;
        d.entryCount = 2;
        d.entries = e;
        group0_ = wgpuDeviceCreateBindGroup(device_, &d);
    }

    void uploadPassData(const PassDesc& pass, const Vertex* vertices, uint32_t vertexCount,
                        const uint32_t* indices, uint32_t indexCount,
                        const MaterialParams* params, uint32_t paramCount) {
        uint64_t vbytes = uint64_t(vertexCount) * sizeof(Vertex);
        uint64_t ibytes = uint64_t(indexCount) * sizeof(uint32_t);
        ensureBuffer(vertexBuf_, vertexCap_, vbytes, WGPUBufferUsage_Vertex, "drift vertices");
        ensureBuffer(indexBuf_, indexCap_, ibytes, WGPUBufferUsage_Index, "drift indices");
        wgpuQueueWriteBuffer(queue_, vertexBuf_, 0, vertices, static_cast<size_t>(vbytes));
        wgpuQueueWriteBuffer(queue_, indexBuf_, 0, indices, static_cast<size_t>(ibytes));
        wgpuQueueWriteBuffer(queue_, frameBuf_, 0, &pass.frame, sizeof(FrameUniforms));

        // Slot paramCount holds zeros for calls without params.
        ensureParamsCapacity(paramCount + 1);
        paramScratch_.assign(static_cast<size_t>(paramStride_) * (paramCount + 1), 0);
        for (uint32_t i = 0; i < paramCount; ++i) {
            std::memcpy(paramScratch_.data() + static_cast<size_t>(i) * paramStride_, &params[i],
                        sizeof(MaterialParams));
        }
        wgpuQueueWriteBuffer(queue_, paramsBuf_, 0, paramScratch_.data(), paramScratch_.size());
    }

    void submit(WGPUCommandEncoder enc) {
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
        wgpuQueueSubmit(queue_, 1, &cmd);
        wgpuCommandBufferRelease(cmd);
        wgpuCommandEncoderRelease(enc);
    }

    static bool clampRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t tw, int32_t th, int32_t out[4]) {
        int32_t x0 = std::max(x, 0), y0 = std::max(y, 0);
        int32_t x1 = std::min(x + w, tw), y1 = std::min(y + h, th);
        if (x1 <= x0 || y1 <= y0) return false;
        out[0] = x0;
        out[1] = y0;
        out[2] = x1 - x0;
        out[3] = y1 - y0;
        return true;
    }

    static void setScissor(WGPURenderPassEncoder rpe, const int32_t r[4]) {
        wgpuRenderPassEncoderSetScissorRect(rpe, static_cast<uint32_t>(r[0]), static_cast<uint32_t>(r[1]),
                                            static_cast<uint32_t>(r[2]), static_cast<uint32_t>(r[3]));
    }

    // ---- Textures ----

    Texture makeTexture(int32_t w, int32_t h, bool renderTarget) {
        Texture t;
        WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        if (renderTarget) td.usage |= WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        td.size = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        td.format = kTextureFormat;
        t.texture = wgpuDeviceCreateTexture(device_, &td);
        if (!t.texture) return t;
        t.view = wgpuTextureCreateView(t.texture, nullptr);
        t.width = w;
        t.height = h;
        t.renderTarget = renderTarget;
        return t;
    }

    void writePixels(const Texture& t, int32_t x, int32_t y, int32_t w, int32_t h, const void* rgba) {
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = t.texture;
        dst.origin = {static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0};
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = static_cast<uint32_t>(w) * 4;
        layout.rowsPerImage = static_cast<uint32_t>(h);
        WGPUExtent3D size = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        wgpuQueueWriteTexture(queue_, &dst, rgba, static_cast<size_t>(w) * h * 4, &layout, &size);
    }

    void releaseTexture(Texture& t) {
        if (t.view) wgpuTextureViewRelease(t.view);
        if (t.texture) wgpuTextureRelease(t.texture);
        t = {};
    }

    WGPUSampler sampler(Filter filter, Wrap wrap) {
        size_t idx = static_cast<size_t>(filter) * 3 + static_cast<size_t>(wrap);
        if (samplers_[idx]) return samplers_[idx];
        WGPUAddressMode mode = wrap == Wrap::Repeat ? WGPUAddressMode_Repeat
                             : wrap == Wrap::Mirror ? WGPUAddressMode_MirrorRepeat
                             : WGPUAddressMode_ClampToEdge;
        WGPUFilterMode f = filter == Filter::Linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
        WGPUSamplerDescriptor sd = WGPU_SAMPLER_DESCRIPTOR_INIT;
        sd.addressModeU = sd.addressModeV = sd.addressModeW = mode;
        sd.magFilter = sd.minFilter = f;
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        samplers_[idx] = wgpuDeviceCreateSampler(device_, &sd);
        return samplers_[idx];
    }

    WGPUBindGroup textureGroup(const TextureId (&ids)[kMaxTextureSlots]) {
        std::array<TextureId, kMaxTextureSlots> key;
        for (int s = 0; s < kMaxTextureSlots; ++s) {
            key[s] = textures_.count(ids[s]) ? ids[s] : 0;
        }
        auto it = bindGroups_.find(key);
        if (it != bindGroups_.end()) return it->second;

        WGPUBindGroupEntry e[kMaxTextureSlots * 2];
        for (int s = 0; s < kMaxTextureSlots; ++s) {
            const Texture& t = key[s] ? textures_[key[s]] : dummy_;
            e[s * 2] = WGPU_BIND_GROUP_ENTRY_INIT;
            e[s * 2].binding = static_cast<uint32_t>(s * 2);
            e[s * 2].textureView = t.view;
            e[s * 2 + 1] = WGPU_BIND_GROUP_ENTRY_INIT;
            e[s * 2 + 1].binding = static_cast<uint32_t>(s * 2 + 1);
            e[s * 2 + 1].sampler = sampler(t.filter, t.wrap);
        }
        WGPUBindGroupDescriptor d = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        d.layout = layout1_;
        d.entryCount = kMaxTextureSlots * 2;
        d.entries = e;
        WGPUBindGroup g = wgpuDeviceCreateBindGroup(device_, &d);
        bindGroups_[key] = g;
        return g;
    }

    void forgetBindGroupsUsing(TextureId id) {
        for (auto it = bindGroups_.begin(); it != bindGroups_.end();) {
            if (std::find(it->first.begin(), it->first.end(), id) != it->first.end()) {
                wgpuBindGroupRelease(it->second);
                it = bindGroups_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void clearBindGroupCache() {
        for (auto& [k, g] : bindGroups_) wgpuBindGroupRelease(g);
        bindGroups_.clear();
    }

    // ---- Shaders and pipelines ----

    WGPUShaderModule compileModule(const std::string& wgsl, std::string* error) {
        WGPUShaderSourceWGSL src = {};
        src.chain.sType = WGPUSType_ShaderSourceWGSL;
        src.code = {wgsl.c_str(), wgsl.size()};
        WGPUShaderModuleDescriptor md = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        md.nextInChain = &src.chain;
        wgpuDevicePushErrorScope(device_, WGPUErrorFilter_Validation);
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(device_, &md);
        std::string message;
        if (popErrorScope(&message)) {
            if (error) *error = message;
            if (module) wgpuShaderModuleRelease(module);
            return nullptr;
        }
        return module;
    }

    // Returns true when the scope caught an error.
    bool popErrorScope(std::string* message) {
        struct ScopeState { bool done = false; bool failed = false; std::string message; };
        ScopeState state;
        WGPUPopErrorScopeCallbackInfo cb = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
        cb.mode = kCallbackMode;
        cb.callback = [](WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView msg,
                         void* ud1, void*) {
            auto* s = static_cast<ScopeState*>(ud1);
            s->failed = status != WGPUPopErrorScopeStatus_Success || type != WGPUErrorType_NoError;
            s->message = toString(msg);
            s->done = true;
        };
        cb.userdata1 = &state;
        wait(wgpuDevicePopErrorScope(device_, cb), state.done);
        if (message) *message = state.message;
        return state.failed;
    }

    static uint64_t pipelineKey(ShaderId shader, Blend blend, WGPUTextureFormat format) {
        return uint64_t(shader) | (uint64_t(blend) << 32) | (uint64_t(format) << 40);
    }

    WGPURenderPipeline pipeline(ShaderId shader, WGPUShaderModule module, Blend blend, WGPUTextureFormat format) {
        uint64_t key = pipelineKey(shader, blend, format);
        auto it = pipelines_.find(key);
        if (it != pipelines_.end()) return it->second;
        WGPURenderPipeline p = buildPipeline(module, blend, format);
        pipelines_[key] = p;
        return p;
    }

    WGPURenderPipeline buildPipeline(WGPUShaderModule module, Blend blend, WGPUTextureFormat format) {
        WGPUVertexAttribute attrs[3] = {WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT,
                                        WGPU_VERTEX_ATTRIBUTE_INIT};
        attrs[0].format = WGPUVertexFormat_Float32x2;
        attrs[0].offset = offsetof(Vertex, x);
        attrs[0].shaderLocation = 0;
        attrs[1].format = WGPUVertexFormat_Float32x2;
        attrs[1].offset = offsetof(Vertex, u);
        attrs[1].shaderLocation = 1;
        attrs[2].format = WGPUVertexFormat_Unorm8x4;
        attrs[2].offset = offsetof(Vertex, r);
        attrs[2].shaderLocation = 2;
        WGPUVertexBufferLayout vbl = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        vbl.stepMode = WGPUVertexStepMode_Vertex;
        vbl.arrayStride = sizeof(Vertex);
        vbl.attributeCount = 3;
        vbl.attributes = attrs;

        WGPUBlendState bs = blendState(blend);
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = format;
        target.blend = blend == Blend::Opaque ? nullptr : &bs;
        target.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT;
        fs.module = module;
        fs.entryPoint = sv("fs_main");
        fs.targetCount = 1;
        fs.targets = &target;

        WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pd.label = sv("drift sprite pipeline");
        pd.layout = pipelineLayout_;
        pd.vertex.module = module;
        pd.vertex.entryPoint = sv("vs_main");
        pd.vertex.bufferCount = 1;
        pd.vertex.buffers = &vbl;
        pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pd.primitive.cullMode = WGPUCullMode_None;
        pd.fragment = &fs;
        return wgpuDeviceCreateRenderPipeline(device_, &pd);
    }

    WGPURenderPipeline clearPipeline(WGPUTextureFormat format) {
        auto it = clearPipelines_.find(format);
        if (it != clearPipelines_.end()) return it->second;

        WGPUVertexAttribute attrs[2] = {WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT};
        attrs[0].format = WGPUVertexFormat_Float32x2;
        attrs[0].offset = offsetof(ClearVertex, x);
        attrs[0].shaderLocation = 0;
        attrs[1].format = WGPUVertexFormat_Float32x4;
        attrs[1].offset = offsetof(ClearVertex, r);
        attrs[1].shaderLocation = 1;
        WGPUVertexBufferLayout vbl = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        vbl.stepMode = WGPUVertexStepMode_Vertex;
        vbl.arrayStride = sizeof(ClearVertex);
        vbl.attributeCount = 2;
        vbl.attributes = attrs;

        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = format;
        target.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT;
        fs.module = clearModule_;
        fs.entryPoint = sv("fs_main");
        fs.targetCount = 1;
        fs.targets = &target;

        WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pd.label = sv("drift clear pipeline");
        pd.layout = clearLayout_;
        pd.vertex.module = clearModule_;
        pd.vertex.entryPoint = sv("vs_main");
        pd.vertex.bufferCount = 1;
        pd.vertex.buffers = &vbl;
        pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pd.fragment = &fs;
        WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline(device_, &pd);
        clearPipelines_[format] = p;
        return p;
    }

    // Same factors as the GL backend's applyBlend.
    static WGPUBlendState blendState(Blend b) {
        auto comp = [](WGPUBlendFactor src, WGPUBlendFactor dst) {
            return WGPUBlendComponent{WGPUBlendOperation_Add, src, dst};
        };
        switch (b) {
        case Blend::Additive:
            return {comp(WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_One), comp(WGPUBlendFactor_One, WGPUBlendFactor_One)};
        case Blend::Multiply:
            return {comp(WGPUBlendFactor_Dst, WGPUBlendFactor_Zero), comp(WGPUBlendFactor_Zero, WGPUBlendFactor_One)};
        case Blend::Premultiplied:
            return {comp(WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha),
                    comp(WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha)};
        case Blend::Screen:
            return {comp(WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrc),
                    comp(WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha)};
        case Blend::Alpha:
        case Blend::Opaque:
        default:
            return {comp(WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha),
                    comp(WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha)};
        }
    }

    SDL_Window*   window_;
    bool          vsync_;
    WGPUInstance  instance_ = nullptr;
    WindowSurface surface_;
    WGPUAdapter   adapter_ = nullptr;
    WGPUDevice    device_ = nullptr;
    WGPUQueue     queue_ = nullptr;

    WGPUTextureFormat      surfaceFormat_ = WGPUTextureFormat_BGRA8Unorm;
    WGPUPresentMode        presentMode_ = WGPUPresentMode_Fifo;
    WGPUCompositeAlphaMode alphaMode_ = WGPUCompositeAlphaMode_Auto;
    bool     surfaceCopySrc_ = false;
    bool     configured_ = false;
    bool     reconfigure_ = false;
    uint32_t surfaceW_ = 0, surfaceH_ = 0;
    WGPUTexture     frameTexture_ = nullptr;
    WGPUTextureView frameView_ = nullptr;

    WGPUBindGroupLayout layout0_ = nullptr, layout1_ = nullptr;
    WGPUPipelineLayout  pipelineLayout_ = nullptr, clearLayout_ = nullptr;
    WGPUBindGroup       group0_ = nullptr;
    WGPUBuffer vertexBuf_ = nullptr, indexBuf_ = nullptr, frameBuf_ = nullptr;
    WGPUBuffer paramsBuf_ = nullptr, clearBuf_ = nullptr;
    uint64_t   vertexCap_ = 0, indexCap_ = 0;
    uint32_t   paramSlots_ = 0;
    uint32_t   uniformAlign_ = 256;
    uint32_t   paramStride_ = 256;
    std::vector<uint8_t> paramScratch_;

    Shader           defaultShader_;
    WGPUShaderModule clearModule_ = nullptr;
    std::unordered_map<uint64_t, WGPURenderPipeline> pipelines_;
    std::unordered_map<uint32_t, WGPURenderPipeline> clearPipelines_;
    std::array<WGPUSampler, 6> samplers_ = {};
    std::unordered_map<std::array<TextureId, kMaxTextureSlots>, WGPUBindGroup, TextureSetHash> bindGroups_;

    uint32_t nextId_ = 1;
    Texture  dummy_;
    std::unordered_map<TextureId, Texture> textures_;
    std::unordered_map<ShaderId, Shader> shaders_;
    std::unordered_map<WGPUTextureView, TextureId> viewToTexture_;

    TextureId fontTexture_ = 0;
    std::vector<Vertex>   imguiVerts_;
    std::vector<uint32_t> imguiIndices_;
    std::vector<DrawCall> imguiCalls_;
};

} // namespace

} // namespace drift::gfx::webgpu

namespace drift::gfx {

namespace {

void webgpuPrepareWindow(const Config&) {}

uint64_t webgpuWindowFlags() {
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    return SDL_WINDOW_METAL;
#else
    return 0;
#endif
}

#ifndef __EMSCRIPTEN__
void webgpuLog(WGPULogLevel level, WGPUStringView message, void*) {
    std::string m = webgpu::toString(message);
    if (level <= WGPULogLevel_Error) DRIFT_LOG_ERROR("wgpu: %s", m.c_str());
    else DRIFT_LOG_WARN("wgpu: %s", m.c_str());
}
#endif

std::unique_ptr<Device> webgpuCreate(SDL_Window* window, const Config& cfg) {
#ifndef __EMSCRIPTEN__
    wgpuSetLogCallback(webgpuLog, nullptr);
    wgpuSetLogLevel(WGPULogLevel_Warn);
#endif
    auto dev = std::make_unique<webgpu::WebGPUDevice>(window, cfg);
    if (!dev->init()) return nullptr;
    return dev;
}

} // namespace

void registerBackend_webgpu() {
    registerBackend({"webgpu", webgpuPrepareWindow, webgpuWindowFlags, webgpuCreate});
}

} // namespace drift::gfx
