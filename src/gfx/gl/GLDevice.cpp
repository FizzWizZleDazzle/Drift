// =============================================================================
// OpenGL backend: GL 3.3 core on desktop, WebGL2 (GLES 3.0) on the web.
// =============================================================================

#include "gfx/Device.hpp"
#include "gfx/gl/GLFunctions.hpp"
#include "drift_shaders.gen.hpp"

#include <drift/Config.hpp>
#include <drift/Log.hpp>

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace drift::gfx::gl {

bool Functions::load(const char** missing) {
#ifdef __EMSCRIPTEN__
#define DRIFT_GL_LOAD(ret, name, params) this->name = &::gl##name;
    DRIFT_GL_FUNCTIONS(DRIFT_GL_LOAD)
#undef DRIFT_GL_LOAD
    (void)missing;
    return true;
#else
#define DRIFT_GL_LOAD(ret, name, params)                                             \
    this->name = reinterpret_cast<ret (APIENTRY*) params>(SDL_GL_GetProcAddress("gl" #name)); \
    if (!this->name) { *missing = "gl" #name; return false; }
    DRIFT_GL_FUNCTIONS(DRIFT_GL_LOAD)
#undef DRIFT_GL_LOAD
    return true;
#endif
}

namespace {

#ifdef __EMSCRIPTEN__
constexpr const char* kVersionHeader = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
#else
constexpr const char* kVersionHeader = "#version 330 core\n";
#endif

// Replaces a leading #version directive (translated modules carry one) with
// the one this platform needs.
std::string withVersion(const std::string& src) {
    size_t start = src.find_first_not_of(" \t\r\n");
    std::string body = src;
    if (start != std::string::npos && src.compare(start, 8, "#version") == 0) {
        size_t end = src.find('\n', start);
        body = end == std::string::npos ? std::string() : src.substr(end + 1);
    }
    return std::string(kVersionHeader) + body;
}

struct Texture {
    GLuint  tex = 0;
    GLuint  fbo = 0;
    int32_t width = 0;
    int32_t height = 0;
};

struct Program {
    GLuint program = 0;
};

class GLDevice final : public Device {
public:
    GLDevice(SDL_Window* window, SDL_GLContext ctx, const Functions& fns)
        : window_(window), ctx_(ctx), gl_(fns) {}

    ~GLDevice() override {
        for (auto& [id, t] : textures_) releaseTexture(t);
        for (auto& [id, p] : programs_) gl_.DeleteProgram(p.program);
        if (defaultProgram_) gl_.DeleteProgram(defaultProgram_);
        if (vbo_) gl_.DeleteBuffers(1, &vbo_);
        if (ibo_) gl_.DeleteBuffers(1, &ibo_);
        if (ubo_[0]) gl_.DeleteBuffers(2, ubo_);
        if (vao_) gl_.DeleteVertexArrays(1, &vao_);
        if (ctx_) SDL_GL_DestroyContext(ctx_);
    }

    bool init(std::string* error) {
        gl_.GenVertexArrays(1, &vao_);
        gl_.BindVertexArray(vao_);
        gl_.GenBuffers(1, &vbo_);
        gl_.GenBuffers(1, &ibo_);
        gl_.BindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl_.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        gl_.EnableVertexAttribArray(0);
        gl_.EnableVertexAttribArray(1);
        gl_.EnableVertexAttribArray(2);
        gl_.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                reinterpret_cast<void*>(offsetof(Vertex, x)));
        gl_.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                reinterpret_cast<void*>(offsetof(Vertex, u)));
        gl_.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                                reinterpret_cast<void*>(offsetof(Vertex, r)));
        gl_.BindVertexArray(0);

        gl_.GenBuffers(2, ubo_);
        gl_.BindBuffer(GL_UNIFORM_BUFFER, ubo_[0]);
        gl_.BufferData(GL_UNIFORM_BUFFER, sizeof(FrameUniforms), nullptr, GL_DYNAMIC_DRAW);
        gl_.BindBuffer(GL_UNIFORM_BUFFER, ubo_[1]);
        gl_.BufferData(GL_UNIFORM_BUFFER, sizeof(MaterialParams), nullptr, GL_DYNAMIC_DRAW);
        gl_.BindBuffer(GL_UNIFORM_BUFFER, 0);

        GLint align = 256;
        gl_.GetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
        uboAlign_ = align > 0 ? align : 256;

        std::string fragSrc = std::string(shaders::prelude_frag_glsl) + shaders::sprite_frag_glsl;
        defaultProgram_ = buildProgram(shaders::prelude_vert_glsl, fragSrc, error);
        return defaultProgram_ != 0;
    }

    const char* backendName() const override { return "gl"; }

    // ---- Textures ----------------------------------------------------------

    TextureId createTexture(const TextureDesc& desc, const void* rgba) override {
        if (desc.width <= 0 || desc.height <= 0) return 0;
        Texture t;
        t.width = desc.width;
        t.height = desc.height;
        gl_.GenTextures(1, &t.tex);
        gl_.BindTexture(GL_TEXTURE_2D, t.tex);
        gl_.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl_.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, desc.width, desc.height, 0,
                       GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        applySampling(desc.filter, desc.wrap);

        if (desc.renderTarget) {
            gl_.GenFramebuffers(1, &t.fbo);
            gl_.BindFramebuffer(GL_FRAMEBUFFER, t.fbo);
            gl_.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
            GLenum status = gl_.CheckFramebufferStatus(GL_FRAMEBUFFER);
            gl_.BindFramebuffer(GL_FRAMEBUFFER, 0);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                DRIFT_LOG_ERROR("gl: render target %dx%d incomplete (0x%x)",
                                desc.width, desc.height, status);
                releaseTexture(t);
                return 0;
            }
        }
        gl_.BindTexture(GL_TEXTURE_2D, 0);

        TextureId id = nextId_++;
        textures_[id] = t;
        return id;
    }

    void updateTexture(TextureId id, int32_t x, int32_t y, int32_t w, int32_t h,
                       const void* rgba) override {
        auto it = textures_.find(id);
        if (it == textures_.end() || !rgba) return;
        gl_.BindTexture(GL_TEXTURE_2D, it->second.tex);
        gl_.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl_.TexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        gl_.BindTexture(GL_TEXTURE_2D, 0);
    }

    void setTextureSampling(TextureId id, Filter filter, Wrap wrap) override {
        auto it = textures_.find(id);
        if (it == textures_.end()) return;
        gl_.BindTexture(GL_TEXTURE_2D, it->second.tex);
        applySampling(filter, wrap);
        gl_.BindTexture(GL_TEXTURE_2D, 0);
    }

    void destroyTexture(TextureId id) override {
        auto it = textures_.find(id);
        if (it == textures_.end()) return;
        releaseTexture(it->second);
        textures_.erase(it);
    }

    bool readPixels(TextureId id, int32_t* w, int32_t* h, std::vector<uint8_t>& out) override {
        GLuint fbo = 0;
        int32_t tw = 0, th = 0;
        if (id == 0) {
            drawableSize(&tw, &th);
        } else {
            auto it = textures_.find(id);
            if (it == textures_.end() || !it->second.fbo) return false;
            fbo = it->second.fbo;
            tw = it->second.width;
            th = it->second.height;
        }
        out.resize(static_cast<size_t>(tw) * th * 4);
        gl_.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl_.PixelStorei(GL_PACK_ALIGNMENT, 1);
        gl_.ReadPixels(0, 0, tw, th, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
        gl_.BindFramebuffer(GL_FRAMEBUFFER, 0);
        // Render targets are stored top-down (see submitPass); the window is bottom-up.
        if (id == 0) {
            size_t row = static_cast<size_t>(tw) * 4;
            std::vector<uint8_t> tmp(row);
            for (int32_t y = 0; y < th / 2; ++y) {
                uint8_t* a = out.data() + y * row;
                uint8_t* b = out.data() + (th - 1 - y) * row;
                std::memcpy(tmp.data(), a, row);
                std::memcpy(a, b, row);
                std::memcpy(b, tmp.data(), row);
            }
        }
        *w = tw;
        *h = th;
        return true;
    }

    // ---- Shaders -----------------------------------------------------------

    ShaderId createShader(const ShaderSource& src, std::string* error) override {
        if (src.glslFragment.empty()) {
            if (error) *error = "shader has no GLSL fragment source";
            return 0;
        }
        const std::string vert = src.glslVertex.empty()
            ? std::string(shaders::prelude_vert_glsl) : src.glslVertex;
        GLuint prog = buildProgram(vert, src.glslFragment, error);
        if (!prog) return 0;
        ShaderId id = nextId_++;
        programs_[id] = {prog};
        return id;
    }

    void destroyShader(ShaderId id) override {
        auto it = programs_.find(id);
        if (it == programs_.end()) return;
        gl_.DeleteProgram(it->second.program);
        programs_.erase(it);
    }

    // ---- Frame -------------------------------------------------------------

    bool beginFrame() override {
        int32_t w = 0, h = 0;
        drawableSize(&w, &h);
        return w > 0 && h > 0;
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
        GLuint fbo = 0;
        int32_t tw = 0, th = 0;
        bool toTexture = pass.target != 0;
        if (toTexture) {
            auto it = textures_.find(pass.target);
            if (it == textures_.end() || !it->second.fbo) return;
            fbo = it->second.fbo;
            tw = it->second.width;
            th = it->second.height;
        } else {
            drawableSize(&tw, &th);
        }

        gl_.BindFramebuffer(GL_FRAMEBUFFER, fbo);

        // Viewport rect in top-left origin -> GL bottom-left origin. Render
        // targets are drawn upside down (see flipY below) so their memory
        // layout matches uploaded images: row 0 = top.
        int32_t vx = 0, vy = 0, vw = tw, vh = th;
        if (pass.viewport[2] > 0 && pass.viewport[3] > 0) {
            vx = pass.viewport[0];
            vy = pass.viewport[1];
            vw = pass.viewport[2];
            vh = pass.viewport[3];
        }
        auto toGLRect = [&](int32_t x, int32_t y, int32_t w, int32_t h, GLint* out) {
            out[0] = x;
            out[1] = toTexture ? y : th - (y + h);
            out[2] = w;
            out[3] = h;
        };
        GLint vp[4];
        toGLRect(vx, vy, vw, vh, vp);
        gl_.Viewport(vp[0], vp[1], vp[2], vp[3]);

        if (pass.clear) {
            gl_.Enable(GL_SCISSOR_TEST);
            gl_.Scissor(vp[0], vp[1], vp[2], vp[3]);
            gl_.ClearColor(pass.clearColor.r, pass.clearColor.g, pass.clearColor.b, pass.clearColor.a);
            gl_.Clear(GL_COLOR_BUFFER_BIT);
            gl_.Disable(GL_SCISSOR_TEST);
        }

        if (callCount == 0 || indexCount == 0) {
            gl_.BindFramebuffer(GL_FRAMEBUFFER, 0);
            return;
        }

        FrameUniforms frame = pass.frame;
        if (toTexture) {
            // Negate clip-space y so the top of the view lands in row 0.
            for (int c = 0; c < 4; ++c) frame.viewProj[c * 4 + 1] = -frame.viewProj[c * 4 + 1];
        }
        gl_.BindBuffer(GL_UNIFORM_BUFFER, ubo_[0]);
        gl_.BufferData(GL_UNIFORM_BUFFER, sizeof(FrameUniforms), &frame, GL_DYNAMIC_DRAW);
        gl_.BindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_[0]);

        // All material params for the pass go in one buffer; each draw binds
        // its slice. Slot paramCount holds zeros for calls without params.
        uint32_t stride = static_cast<uint32_t>(
            (sizeof(MaterialParams) + uboAlign_ - 1) / uboAlign_ * uboAlign_);
        paramScratch_.assign(static_cast<size_t>(stride) * (paramCount + 1), 0);
        for (uint32_t i = 0; i < paramCount; ++i) {
            std::memcpy(paramScratch_.data() + static_cast<size_t>(i) * stride, &params[i], sizeof(MaterialParams));
        }
        gl_.BindBuffer(GL_UNIFORM_BUFFER, ubo_[1]);
        gl_.BufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(paramScratch_.size()),
                       paramScratch_.data(), GL_DYNAMIC_DRAW);
        gl_.BindBuffer(GL_UNIFORM_BUFFER, 0);

        gl_.BindVertexArray(vao_);
        gl_.BindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl_.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexCount * sizeof(Vertex)),
                       vertices, GL_STREAM_DRAW);
        gl_.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        gl_.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indexCount * sizeof(uint32_t)),
                       indices, GL_STREAM_DRAW);

        gl_.Enable(GL_BLEND);
        GLuint curProgram = 0;
        Blend curBlend = Blend::Opaque;
        bool blendSet = false;
        bool scissorOn = false;

        for (uint32_t i = 0; i < callCount; ++i) {
            const DrawCall& dc = calls[i];
            if (dc.indexCount == 0) continue;

            GLuint prog = defaultProgram_;
            if (dc.shader != 0) {
                auto it = programs_.find(dc.shader);
                if (it != programs_.end()) prog = it->second.program;
            }
            if (prog != curProgram) {
                gl_.UseProgram(prog);
                curProgram = prog;
            }
            if (!blendSet || dc.blend != curBlend) {
                applyBlend(dc.blend);
                curBlend = dc.blend;
                blendSet = true;
            }

            for (int s = 0; s < kMaxTextureSlots; ++s) {
                gl_.ActiveTexture(GL_TEXTURE0 + s);
                auto it = textures_.find(dc.textures[s]);
                gl_.BindTexture(GL_TEXTURE_2D, it != textures_.end() ? it->second.tex : 0);
            }

            uint32_t slot = (dc.paramsIndex >= 0 && static_cast<uint32_t>(dc.paramsIndex) < paramCount)
                ? static_cast<uint32_t>(dc.paramsIndex) : paramCount;
            bindParamRange(slot * stride);

            if (dc.scissor) {
                if (!scissorOn) { gl_.Enable(GL_SCISSOR_TEST); scissorOn = true; }
                GLint r[4];
                toGLRect(dc.scissorRect[0], dc.scissorRect[1], dc.scissorRect[2], dc.scissorRect[3], r);
                gl_.Scissor(r[0], r[1], r[2], r[3]);
            } else if (scissorOn) {
                gl_.Disable(GL_SCISSOR_TEST);
                scissorOn = false;
            }

            gl_.DrawElements(GL_TRIANGLES, static_cast<GLsizei>(dc.indexCount), GL_UNSIGNED_INT,
                             reinterpret_cast<void*>(static_cast<uintptr_t>(dc.firstIndex) * sizeof(uint32_t)));
        }

        if (scissorOn) gl_.Disable(GL_SCISSOR_TEST);
        gl_.ActiveTexture(GL_TEXTURE0);
        gl_.BindVertexArray(0);
        gl_.UseProgram(0);
        gl_.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void endFrame() override {
        SDL_GL_SwapWindow(window_);
    }

    // ---- ImGui -------------------------------------------------------------

    bool imguiInit() override {
#ifdef __EMSCRIPTEN__
        return ImGui_ImplOpenGL3_Init("#version 300 es");
#else
        return ImGui_ImplOpenGL3_Init("#version 330 core");
#endif
    }
    void imguiShutdown() override { ImGui_ImplOpenGL3_Shutdown(); }
    void imguiNewFrame() override { ImGui_ImplOpenGL3_NewFrame(); }
    void imguiRender(ImDrawData* data) override {
        int32_t w = 0, h = 0;
        drawableSize(&w, &h);
        gl_.BindFramebuffer(GL_FRAMEBUFFER, 0);
        gl_.Viewport(0, 0, w, h);
        ImGui_ImplOpenGL3_RenderDrawData(data);
    }

    uint64_t imguiTexture(TextureId id) const override {
        auto it = textures_.find(id);
        return it != textures_.end() ? static_cast<uint64_t>(it->second.tex) : 0;
    }

private:
    void applySampling(Filter filter, Wrap wrap) {
        GLint f = filter == Filter::Linear ? GL_LINEAR : GL_NEAREST;
        GLint w = wrap == Wrap::Repeat ? GL_REPEAT
                : wrap == Wrap::Mirror ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE;
        gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
        gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, w);
        gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, w);
    }

    void applyBlend(Blend b) {
        if (b == Blend::Opaque) {
            gl_.Disable(GL_BLEND);
            return;
        }
        gl_.Enable(GL_BLEND);
        gl_.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        switch (b) {
        case Blend::Alpha:
            gl_.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case Blend::Additive:
            gl_.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
            break;
        case Blend::Multiply:
            gl_.BlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
            break;
        case Blend::Premultiplied:
            gl_.BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case Blend::Screen:
            gl_.BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case Blend::Opaque:
            break;
        }
    }

    void bindParamRange(uint32_t offset) {
        gl_.BindBufferRange(GL_UNIFORM_BUFFER, 1, ubo_[1], offset, sizeof(MaterialParams));
    }

    void releaseTexture(Texture& t) {
        if (t.fbo) gl_.DeleteFramebuffers(1, &t.fbo);
        if (t.tex) gl_.DeleteTextures(1, &t.tex);
        t = {};
    }

    GLuint compile(GLenum type, const std::string& src, std::string* error) {
        std::string full = withVersion(src);
        const char* p = full.c_str();
        GLuint sh = gl_.CreateShader(type);
        gl_.ShaderSource(sh, 1, &p, nullptr);
        gl_.CompileShader(sh);
        GLint ok = 0;
        gl_.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048] = {};
            gl_.GetShaderInfoLog(sh, sizeof(log), nullptr, log);
            if (error) *error = std::string(type == GL_VERTEX_SHADER ? "vertex: " : "fragment: ") + log;
            gl_.DeleteShader(sh);
            return 0;
        }
        return sh;
    }

    // Binds uniform blocks and samplers by the names the shader conventions
    // define: blocks named FrameUniforms* / MaterialParams* (naga appends a
    // suffix), samplers u_textureN (hand-written GLSL) or the combined
    // sampler names naga emits for group 1 bindings 0, 2, 4, 6.
    GLuint buildProgram(const std::string& vert, const std::string& frag, std::string* error) {
        GLuint vs = compile(GL_VERTEX_SHADER, vert, error);
        if (!vs) return 0;
        GLuint fs = compile(GL_FRAGMENT_SHADER, frag, error);
        if (!fs) { gl_.DeleteShader(vs); return 0; }

        GLuint prog = gl_.CreateProgram();
        gl_.AttachShader(prog, vs);
        gl_.AttachShader(prog, fs);
        gl_.LinkProgram(prog);
        gl_.DeleteShader(vs);
        gl_.DeleteShader(fs);
        GLint ok = 0;
        gl_.GetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048] = {};
            gl_.GetProgramInfoLog(prog, sizeof(log), nullptr, log);
            if (error) *error = std::string("link: ") + log;
            gl_.DeleteProgram(prog);
            return 0;
        }

        GLint blocks = 0;
        gl_.GetProgramiv(prog, GL_ACTIVE_UNIFORM_BLOCKS, &blocks);
        for (GLint b = 0; b < blocks; ++b) {
            char name[128] = {};
            gl_.GetActiveUniformBlockName(prog, static_cast<GLuint>(b), sizeof(name), nullptr, name);
            if (std::strncmp(name, "FrameUniforms", 13) == 0) {
                gl_.UniformBlockBinding(prog, static_cast<GLuint>(b), 0);
            } else if (std::strncmp(name, "MaterialParams", 14) == 0) {
                gl_.UniformBlockBinding(prog, static_cast<GLuint>(b), 1);
            }
        }

        gl_.UseProgram(prog);
        for (int s = 0; s < kMaxTextureSlots; ++s) {
            char name[64];
            std::snprintf(name, sizeof(name), "u_texture%d", s);
            GLint loc = gl_.GetUniformLocation(prog, name);
            if (loc < 0) {
                std::snprintf(name, sizeof(name), "_group_1_binding_%d_fs", s * 2);
                loc = gl_.GetUniformLocation(prog, name);
            }
            if (loc >= 0) gl_.Uniform1i(loc, s);
        }
        gl_.UseProgram(0);
        return prog;
    }

    SDL_Window*   window_;
    SDL_GLContext ctx_;
    Functions     gl_;

    GLuint vao_ = 0, vbo_ = 0, ibo_ = 0;
    GLuint ubo_[2] = {};
    GLint  uboAlign_ = 256;
    GLuint defaultProgram_ = 0;

    uint32_t nextId_ = 1;
    std::unordered_map<TextureId, Texture> textures_;
    std::unordered_map<ShaderId, Program> programs_;
    std::vector<uint8_t> paramScratch_;
};

} // namespace

} // namespace drift::gfx::gl

namespace drift::gfx {

namespace {

void glPrepareWindow(const Config&) {
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
}

uint64_t glWindowFlags() { return SDL_WINDOW_OPENGL; }

std::unique_ptr<Device> glCreate(SDL_Window* window, const Config& cfg) {
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx) {
        DRIFT_LOG_ERROR("gl: context creation failed: %s", SDL_GetError());
        return nullptr;
    }
    SDL_GL_MakeCurrent(window, ctx);
    SDL_GL_SetSwapInterval(cfg.vsync ? 1 : 0);

    gl::Functions fns;
    const char* missing = nullptr;
    if (!fns.load(&missing)) {
        DRIFT_LOG_ERROR("gl: missing function %s", missing ? missing : "?");
        SDL_GL_DestroyContext(ctx);
        return nullptr;
    }

    auto dev = std::make_unique<gl::GLDevice>(window, ctx, fns);
    std::string error;
    if (!dev->init(&error)) {
        DRIFT_LOG_ERROR("gl: default shader failed: %s", error.c_str());
        return nullptr;
    }
    return dev;
}

} // namespace

void registerBackend_gl() {
    registerBackend({"gl", glPrepareWindow, glWindowFlags, glCreate});
}

} // namespace drift::gfx
