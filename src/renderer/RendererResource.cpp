// =============================================================================
// Drift 2D Game Engine - RendererResource
// =============================================================================
// Queues sprites and shapes in world space, then at endFrame renders every
// camera into the scene target, runs view hooks and post effects, draws the
// screen layer, and presents the scene to the window. All GPU work goes
// through gfx::Device.
// =============================================================================

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <drift/resources/RendererResource.hpp>
#include <drift/App.hpp>
#include <drift/Log.hpp>

#include <SDL3/SDL.h>

#include "core/HandlePool.hpp"
#include "gfx/Device.hpp"
#include "drift_shaders.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace drift {

namespace {

constexpr float kPi = 3.14159265358979f;

struct TextureEntry {
    gfx::TextureId id = 0;
    int32_t width = 0;
    int32_t height = 0;
    bool renderTarget = false;
    TextureOptions options = {};
};

struct ShaderEntry {
    gfx::ShaderId id = 0;
};

struct MaterialEntry {
    ShaderHandle shader;
    TextureHandle textures[gfx::kMaxTextureSlots];   // slot 0 is the drawn texture
    gfx::MaterialParams params;
};

struct CameraEntry {
    CameraDesc desc;
    uint32_t created = 0;   // creation sequence, for stable ordering
};

// One queued draw: a run of vertices + local indices plus its sort state.
struct Item {
    float    z;
    uint8_t  layer;
    uint8_t  blend;
    uint32_t material;   // MaterialHandle id
    uint32_t texture;    // TextureHandle id
    uint32_t firstVertex, vertexCount;
    uint32_t firstIndex, indexCount;
    int32_t  clip;       // index into Queue::clips, -1 = none
    float    minX, minY, maxX, maxY;
};

struct Queue {
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t>    indices;   // local to each item
    std::vector<Item>        items;
    std::vector<Rect>        clips;

    void clear() {
        vertices.clear();
        indices.clear();
        items.clear();
        clips.clear();
    }
};

gfx::Blend toGfx(BlendMode b) {
    switch (b) {
    case BlendMode::Alpha:         return gfx::Blend::Alpha;
    case BlendMode::Additive:      return gfx::Blend::Additive;
    case BlendMode::Multiply:      return gfx::Blend::Multiply;
    case BlendMode::Premultiplied: return gfx::Blend::Premultiplied;
    case BlendMode::Screen:        return gfx::Blend::Screen;
    case BlendMode::Opaque:        return gfx::Blend::Opaque;
    }
    return gfx::Blend::Alpha;
}

gfx::Filter toGfx(TextureFilter f) { return f == TextureFilter::Linear ? gfx::Filter::Linear : gfx::Filter::Nearest; }

gfx::Wrap toGfx(TextureWrap w) {
    return w == TextureWrap::Repeat ? gfx::Wrap::Repeat
         : w == TextureWrap::Mirror ? gfx::Wrap::Mirror : gfx::Wrap::Clamp;
}

void toMat4(const Mat3& m, float out[16]) {
    std::memset(out, 0, sizeof(float) * 16);
    out[0]  = m.m[0]; out[1]  = m.m[1];
    out[4]  = m.m[3]; out[5]  = m.m[4];
    out[10] = 1.f;
    out[12] = m.m[6]; out[13] = m.m[7];
    out[15] = 1.f;
}

// World -> clip for a camera looking at a view of size (w, h) logical units.
Mat3 cameraViewProj(const CameraDesc& cam, float w, float h) {
    Mat3 view = Mat3::identity();
    view = Mat3::translate(-cam.position.x, -cam.position.y) * view;
    view = Mat3::rotate(-cam.rotation) * view;
    view = Mat3::scale(cam.zoom, cam.zoom) * view;
    view = Mat3::translate(w * 0.5f, h * 0.5f) * view;
    return Mat3::ortho(w, h) * view;
}

Rect cameraBounds(const CameraDesc& cam, float w, float h) {
    float hw = w * 0.5f / cam.zoom;
    float hh = h * 0.5f / cam.zoom;
    float c = std::fabs(std::cos(cam.rotation));
    float s = std::fabs(std::sin(cam.rotation));
    float ex = hw * c + hh * s;
    float ey = hw * s + hh * c;
    return {cam.position.x - ex, cam.position.y - ey, ex * 2.f, ey * 2.f};
}

int autoSegments(float radius) {
    int n = static_cast<int>(radius * 0.6f);
    return std::clamp(n, 12, 128);
}

float cross(Vec2 o, Vec2 a, Vec2 b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

bool pointInTriangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
    float d1 = cross(p, a, b);
    float d2 = cross(p, b, c);
    float d3 = cross(p, c, a);
    bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
}

} // namespace

// =============================================================================
// Impl
// =============================================================================

struct RendererResource::Impl {
    App& app;
    SDL_Window* window = nullptr;
    gfx::Device* device = nullptr;

    HandlePool<TextureTag, TextureEntry>   textures;
    HandlePool<ShaderTag, ShaderEntry>     shaders;
    HandlePool<MaterialTag, MaterialEntry> materials;
    HandlePool<CameraTag, CameraEntry>     cameras;
    uint32_t cameraSeq = 0;

    TextureHandle white;
    CameraHandle defaultCamera;

    // Logical resolution: configured size and the size in effect this frame
    // (they differ in Expand mode).
    float configW = 1280.f, configH = 720.f;
    float logicalW = 1280.f, logicalH = 720.f;
    ScaleMode scaleMode = ScaleMode::Letterbox;
    Rect output = {};           // window pixels the scene is shown in
    int32_t drawableW = 0, drawableH = 0;

    // Scene ping-pong targets.
    TextureHandle scene[2];
    int32_t sceneW = 0, sceneH = 0;
    TextureFilter sceneFilter = TextureFilter::Nearest;

    ColorF clearColor = {0.f, 0.f, 0.f, 1.f};

    Queue frameQueue;
    Queue passQueue;
    Queue* current = &frameQueue;
    uint8_t layer = 0;
    std::vector<Rect> clipStack;

    // Active explicit pass (inside hooks / post effects).
    bool inPass = false;
    TextureHandle passTarget;
    PassOptions passOptions;
    Mat3 passViewProj;

    struct Hook { int32_t id; int32_t order; ViewHook fn; };
    struct Post { int32_t id; int32_t order; PostEffect fn; };
    std::vector<Hook> viewHooks;
    std::vector<Post> postEffects;
    int32_t nextHookId = 1;

    std::function<void()> overlay;
    std::string screenshotPath;

    double startTime = 0.0;
    double lastTime = 0.0;
    float frameDelta = 0.f;

    int32_t drawCallCount = 0;
    int32_t spriteCountVal = 0;
    int32_t lastDrawCalls = 0;      // reported values: the previous frame's
    int32_t lastSpriteCount = 0;

    // Scratch reused across passes.
    std::vector<uint32_t> order;
    std::vector<gfx::Vertex> passVertices;
    std::vector<uint32_t> passIndices;
    std::vector<gfx::DrawCall> calls;
    std::vector<gfx::MaterialParams> params;
    std::map<uint32_t, int32_t> paramSlots;

    explicit Impl(App& a) : app(a) {}

    // ---- queue helpers ------------------------------------------------------

    // Reserves an item with `nv` vertices and `ni` indices; returns pointers
    // to fill. Bounds are computed in finishItem.
    Item& beginItem(TextureHandle tex, float z, BlendMode blend, MaterialHandle mat,
                    uint8_t itemLayer, uint32_t nv, uint32_t ni,
                    gfx::Vertex** vout, uint32_t** iout) {
        Queue& q = *current;
        Item it{};
        it.z = z;
        it.layer = itemLayer;
        it.blend = static_cast<uint8_t>(blend);
        it.material = mat.id;
        it.texture = tex.id;
        it.firstVertex = static_cast<uint32_t>(q.vertices.size());
        it.vertexCount = nv;
        it.firstIndex = static_cast<uint32_t>(q.indices.size());
        it.indexCount = ni;
        it.clip = -1;
        if (!clipStack.empty() && current == &frameQueue) {
            q.clips.push_back(clipStack.back());
            it.clip = static_cast<int32_t>(q.clips.size() - 1);
        }
        q.vertices.resize(q.vertices.size() + nv);
        q.indices.resize(q.indices.size() + ni);
        *vout = q.vertices.data() + it.firstVertex;
        *iout = q.indices.data() + it.firstIndex;
        q.items.push_back(it);
        return q.items.back();
    }

    static void finishItem(Queue& q, Item& it) {
        float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (uint32_t i = 0; i < it.vertexCount; ++i) {
            const gfx::Vertex& v = q.vertices[it.firstVertex + i];
            minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
            minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
        }
        it.minX = minX; it.minY = minY; it.maxX = maxX; it.maxY = maxY;
    }

    static gfx::Vertex vert(float x, float y, float u, float v, Color c) {
        return {x, y, u, v, c.r, c.g, c.b, c.a};
    }

    void quad(TextureHandle tex, const Vec2 p[4], float u0, float v0, float u1, float v1,
              Color c, float z, BlendMode blend, MaterialHandle mat, uint8_t itemLayer) {
        gfx::Vertex* v;
        uint32_t* idx;
        Item& it = beginItem(tex, z, blend, mat, itemLayer, 4, 6, &v, &idx);
        v[0] = vert(p[0].x, p[0].y, u0, v0, c);
        v[1] = vert(p[1].x, p[1].y, u1, v0, c);
        v[2] = vert(p[2].x, p[2].y, u1, v1, c);
        v[3] = vert(p[3].x, p[3].y, u0, v1, c);
        idx[0] = 0; idx[1] = 1; idx[2] = 2;
        idx[3] = 0; idx[4] = 2; idx[5] = 3;
        finishItem(*current, it);
        ++spriteCountVal;
    }

    void sprite(const SpriteDraw& s) {
        const TextureEntry* e = textures.get(s.texture);
        if (!e) return;
        Rect src = s.srcRect;
        if (src.w <= 0.f || src.h <= 0.f) {
            src = {0.f, 0.f, static_cast<float>(e->width), static_cast<float>(e->height)};
        }
        float u0 = src.x / e->width, v0 = src.y / e->height;
        float u1 = (src.x + src.w) / e->width, v1 = (src.y + src.h) / e->height;
        if (s.flip & Flip::H) std::swap(u0, u1);
        if (s.flip & Flip::V) std::swap(v0, v1);

        float qw = src.w * s.scale.x, qh = src.h * s.scale.y;
        float ox = s.origin.x * s.scale.x, oy = s.origin.y * s.scale.y;
        float lx[4] = {-ox, qw - ox, qw - ox, -ox};
        float ly[4] = {-oy, -oy, qh - oy, qh - oy};
        float c = std::cos(s.rotation), sn = std::sin(s.rotation);
        Vec2 p[4];
        for (int j = 0; j < 4; ++j) {
            p[j] = {s.position.x + lx[j] * c - ly[j] * sn,
                    s.position.y + lx[j] * sn + ly[j] * c};
        }
        quad(s.texture, p, u0, v0, u1, v1, s.tint, s.zOrder, s.blend, s.material, s.layer);
    }

    // Triangles from a vertex list with uv 0 (white texture).
    void solid(const Vec2* pts, uint32_t n, const uint32_t* tris, uint32_t ni, Color c, float z) {
        if (n == 0 || ni == 0) return;
        gfx::Vertex* v;
        uint32_t* idx;
        Item& it = beginItem(white, z, BlendMode::Alpha, {}, layer, n, ni, &v, &idx);
        for (uint32_t i = 0; i < n; ++i) v[i] = vert(pts[i].x, pts[i].y, 0.5f, 0.5f, c);
        std::memcpy(idx, tris, ni * sizeof(uint32_t));
        finishItem(*current, it);
    }

    // Thick polyline with miter joins.
    void polyline(const Vec2* pts, int32_t count, Color c, float thickness, bool closed, float z) {
        if (count < 2) return;
        float hw = thickness * 0.5f;
        std::vector<Vec2> verts;
        std::vector<uint32_t> idx;
        verts.reserve(count * 2);
        auto normalOf = [](Vec2 a, Vec2 b) {
            Vec2 d = (b - a).normalized();
            return Vec2{-d.y, d.x};
        };
        for (int32_t i = 0; i < count; ++i) {
            bool hasPrev = closed || i > 0;
            bool hasNext = closed || i < count - 1;
            Vec2 p = pts[i];
            Vec2 prev = pts[(i - 1 + count) % count];
            Vec2 next = pts[(i + 1) % count];
            Vec2 n;
            float scale = 1.f;
            if (hasPrev && hasNext) {
                Vec2 n0 = normalOf(prev, p), n1 = normalOf(p, next);
                Vec2 m = (n0 + n1).normalized();
                if (m.lengthSq() < 1e-6f) m = n0;
                float d = m.dot(n0);
                scale = d > 0.25f ? 1.f / d : 4.f;   // limit miter length
                n = m;
            } else if (hasNext) {
                n = normalOf(p, next);
            } else {
                n = normalOf(prev, p);
            }
            verts.push_back(p + n * (hw * scale));
            verts.push_back(p - n * (hw * scale));
        }
        int32_t segs = closed ? count : count - 1;
        for (int32_t i = 0; i < segs; ++i) {
            uint32_t a = i * 2, b = ((i + 1) % count) * 2;
            idx.insert(idx.end(), {a, b, b + 1, a, b + 1, a + 1});
        }
        solid(verts.data(), static_cast<uint32_t>(verts.size()), idx.data(),
              static_cast<uint32_t>(idx.size()), c, z);
    }

    void fan(const std::vector<Vec2>& ring, Vec2 center, Color c, float z) {
        std::vector<Vec2> verts;
        verts.reserve(ring.size() + 1);
        verts.push_back(center);
        verts.insert(verts.end(), ring.begin(), ring.end());
        std::vector<uint32_t> idx;
        uint32_t n = static_cast<uint32_t>(ring.size());
        for (uint32_t i = 0; i < n; ++i) {
            idx.insert(idx.end(), {0u, 1 + i, 1 + (i + 1) % n});
        }
        solid(verts.data(), static_cast<uint32_t>(verts.size()), idx.data(),
              static_cast<uint32_t>(idx.size()), c, z);
    }

    // ---- frame execution ----------------------------------------------------

    TextureEntry* tex(TextureHandle h) { return textures.get(h); }

    void ensureScene(int32_t w, int32_t h, TextureFilter filter) {
        if (w == sceneW && h == sceneH && filter == sceneFilter && scene[0].valid()) return;
        for (auto& s : scene) {
            if (TextureEntry* e = textures.get(s)) {
                device->destroyTexture(e->id);
                textures.destroy(s);
            }
            gfx::TextureDesc d;
            d.width = w;
            d.height = h;
            d.filter = toGfx(filter);
            d.renderTarget = true;
            gfx::TextureId id = device->createTexture(d, nullptr);
            s = id ? textures.create({id, w, h, true}) : TextureHandle{};
        }
        sceneW = w;
        sceneH = h;
        sceneFilter = filter;
    }

    void computeOutput() {
        device->drawableSize(&drawableW, &drawableH);
        float dw = static_cast<float>(drawableW), dh = static_cast<float>(drawableH);
        logicalW = configW;
        logicalH = configH;
        float sx = dw / configW, sy = dh / configH;
        switch (scaleMode) {
        case ScaleMode::Stretch:
            output = {0.f, 0.f, dw, dh};
            break;
        case ScaleMode::Letterbox: {
            float s = std::min(sx, sy);
            float w = std::floor(configW * s), h = std::floor(configH * s);
            output = {std::floor((dw - w) * 0.5f), std::floor((dh - h) * 0.5f), w, h};
            break;
        }
        case ScaleMode::Integer: {
            float s = std::max(1.f, std::floor(std::min(sx, sy)));
            float w = configW * s, h = configH * s;
            output = {std::floor((dw - w) * 0.5f), std::floor((dh - h) * 0.5f), w, h};
            break;
        }
        case ScaleMode::Expand: {
            float s = std::min(sx, sy);
            logicalW = std::floor(dw / s);
            logicalH = std::floor(dh / s);
            output = {0.f, 0.f, dw, dh};
            break;
        }
        }
    }

    gfx::FrameUniforms frameUniforms(const Mat3& viewProj, float tw, float th) const {
        gfx::FrameUniforms f{};
        toMat4(viewProj, f.viewProj);
        f.time[0] = static_cast<float>(lastTime - startTime);
        f.time[1] = frameDelta;
        f.time[2] = tw;
        f.time[3] = th;
        return f;
    }

    gfx::ShaderId shaderFor(uint32_t materialId) {
        if (!materialId) return 0;
        MaterialEntry* m = materials.get(MaterialHandle{materialId});
        if (!m) return 0;
        ShaderEntry* s = shaders.get(m->shader);
        return s ? s->id : 0;
    }

    // Builds draw calls for `indices` (into q.items, already in draw order)
    // and submits one pass.
    void submit(const Queue& q, const std::vector<uint32_t>& itemOrder,
                TextureHandle target, bool clear, ColorF clearCol,
                Rect viewportPx, const Mat3& viewProj, float pixelScaleX, float pixelScaleY) {
        TextureEntry* te = target.valid() ? textures.get(target) : nullptr;
        float tw = te ? static_cast<float>(te->width) : static_cast<float>(drawableW);
        float th = te ? static_cast<float>(te->height) : static_cast<float>(drawableH);

        passVertices.clear();
        passIndices.clear();
        calls.clear();
        params.clear();
        paramSlots.clear();

        for (uint32_t ii : itemOrder) {
            const Item& it = q.items[ii];
            uint32_t base = static_cast<uint32_t>(passVertices.size());
            passVertices.insert(passVertices.end(),
                                q.vertices.begin() + it.firstVertex,
                                q.vertices.begin() + it.firstVertex + it.vertexCount);
            uint32_t firstIndex = static_cast<uint32_t>(passIndices.size());
            for (uint32_t k = 0; k < it.indexCount; ++k) {
                passIndices.push_back(base + q.indices[it.firstIndex + k]);
            }

            gfx::DrawCall dc;
            dc.blend = static_cast<gfx::Blend>(toGfx(static_cast<BlendMode>(it.blend)));
            const TextureEntry* t0 = textures.get(TextureHandle{it.texture});
            dc.textures[0] = t0 ? t0->id : 0;
            if (it.material) {
                MaterialEntry* m = materials.get(MaterialHandle{it.material});
                if (m) {
                    dc.shader = shaderFor(it.material);
                    for (int s = 1; s < gfx::kMaxTextureSlots; ++s) {
                        const TextureEntry* ts = textures.get(m->textures[s]);
                        dc.textures[s] = ts ? ts->id : 0;
                    }
                    auto found = paramSlots.find(it.material);
                    if (found == paramSlots.end()) {
                        params.push_back(m->params);
                        found = paramSlots.emplace(it.material, static_cast<int32_t>(params.size() - 1)).first;
                    }
                    dc.paramsIndex = found->second;
                }
            }
            if (it.clip >= 0) {
                const Rect& r = q.clips[it.clip];
                dc.scissor = true;
                dc.scissorRect[0] = static_cast<int32_t>(viewportPx.x + r.x * pixelScaleX);
                dc.scissorRect[1] = static_cast<int32_t>(viewportPx.y + r.y * pixelScaleY);
                dc.scissorRect[2] = static_cast<int32_t>(r.w * pixelScaleX);
                dc.scissorRect[3] = static_cast<int32_t>(r.h * pixelScaleY);
            }
            dc.firstIndex = firstIndex;
            dc.indexCount = it.indexCount;

            if (!calls.empty()) {
                gfx::DrawCall& last = calls.back();
                bool same = last.shader == dc.shader && last.blend == dc.blend &&
                            last.paramsIndex == dc.paramsIndex && last.scissor == dc.scissor &&
                            std::memcmp(last.textures, dc.textures, sizeof(dc.textures)) == 0 &&
                            (!dc.scissor || std::memcmp(last.scissorRect, dc.scissorRect, sizeof(dc.scissorRect)) == 0) &&
                            last.firstIndex + last.indexCount == dc.firstIndex;
                if (same) {
                    last.indexCount += dc.indexCount;
                    continue;
                }
            }
            calls.push_back(dc);
        }

        gfx::PassDesc pd;
        pd.target = te ? te->id : 0;
        pd.clear = clear;
        pd.clearColor = clearCol;
        pd.viewport[0] = static_cast<int32_t>(viewportPx.x);
        pd.viewport[1] = static_cast<int32_t>(viewportPx.y);
        pd.viewport[2] = static_cast<int32_t>(viewportPx.w);
        pd.viewport[3] = static_cast<int32_t>(viewportPx.h);
        pd.frame = frameUniforms(viewProj, viewportPx.w > 0 ? viewportPx.w : tw,
                                 viewportPx.h > 0 ? viewportPx.h : th);
        device->submitPass(pd, passVertices.data(), static_cast<uint32_t>(passVertices.size()),
                           passIndices.data(), static_cast<uint32_t>(passIndices.size()),
                           calls.data(), static_cast<uint32_t>(calls.size()),
                           params.data(), static_cast<uint32_t>(params.size()));
        drawCallCount += static_cast<int32_t>(calls.size());
    }

    static bool itemLess(const Item& a, const Item& b) {
        if (a.layer != b.layer) return a.layer < b.layer;
        if (a.z != b.z) return a.z < b.z;
        if (a.material != b.material) return a.material < b.material;
        if (a.blend != b.blend) return a.blend < b.blend;
        return a.texture < b.texture;
    }

    void sortQueue(Queue& q) {
        order.resize(q.items.size());
        for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            return itemLess(q.items[a], q.items[b]);
        });
    }
};

// =============================================================================
// Construction
// =============================================================================

RendererResource::RendererResource(App& app)
    : impl_(new Impl(app))
{
    impl_->window = static_cast<SDL_Window*>(app.sdlWindow());
    impl_->device = app.graphicsDevice();

    const auto& cfg = app.config();
    impl_->configW = static_cast<float>(cfg.window.width > 0 ? cfg.window.width : 1280);
    impl_->configH = static_cast<float>(cfg.window.height > 0 ? cfg.window.height : 720);
    impl_->logicalW = impl_->configW;
    impl_->logicalH = impl_->configH;
    impl_->scaleMode = cfg.window.scaleMode;
    impl_->clearColor = cfg.render.clearColor;
    // Live reload of drift.toml.
    app.onSettingsChanged("window", [this, &app](const std::vector<std::string>& keys) {
        for (const auto& k : keys) {
            if (k == "scale_mode") impl_->scaleMode = app.config().window.scaleMode;
        }
    });
    app.onSettingsChanged("render", [this, &app](const std::vector<std::string>& keys) {
        for (const auto& k : keys) {
            if (k == "clear_color") impl_->clearColor = app.config().render.clearColor;
        }
    });
    impl_->startTime = impl_->lastTime = static_cast<double>(SDL_GetTicksNS()) * 1e-9;

    if (!impl_->device) {
        DRIFT_LOG_ERROR("RendererResource: no graphics device");
        return;
    }

    uint8_t px[4] = {255, 255, 255, 255};
    impl_->white = createTextureFromPixels(px, 1, 1);

    CameraDesc cam;
    cam.position = {impl_->configW * 0.5f, impl_->configH * 0.5f};
    impl_->defaultCamera = createCamera(cam);

    DRIFT_LOG_INFO("Renderer: %s backend", impl_->device->backendName());
}

RendererResource::~RendererResource() {
    if (impl_->device) {
        impl_->textures.forEach([this](TextureHandle, TextureEntry& e) {
            impl_->device->destroyTexture(e.id);
        });
        impl_->shaders.forEach([this](ShaderHandle, ShaderEntry& e) {
            impl_->device->destroyShader(e.id);
        });
    }
    delete impl_;
}

// =============================================================================
// Frame lifecycle
// =============================================================================

void RendererResource::beginFrame() {
    impl_->frameQueue.clear();
    impl_->current = &impl_->frameQueue;
    impl_->clipStack.clear();
    impl_->layer = 0;
    impl_->lastDrawCalls = impl_->drawCallCount;
    impl_->lastSpriteCount = impl_->spriteCountVal;
    impl_->drawCallCount = 0;
    impl_->spriteCountVal = 0;
    double now = static_cast<double>(SDL_GetTicksNS()) * 1e-9;
    impl_->frameDelta = static_cast<float>(now - impl_->lastTime);
    impl_->lastTime = now;
}

void RendererResource::endFrame() {
    Impl& s = *impl_;
    gfx::Device* dev = s.device;
    if (!dev) return;
    if (!dev->beginFrame()) {
        s.frameQueue.clear();
        return;
    }

    s.computeOutput();
    if (s.output.w < 1.f || s.output.h < 1.f) {
        dev->endFrame();
        return;
    }

    bool integer = s.scaleMode == ScaleMode::Integer;
    int32_t sw = integer ? static_cast<int32_t>(s.logicalW) : static_cast<int32_t>(s.output.w);
    int32_t sh = integer ? static_cast<int32_t>(s.logicalH) : static_cast<int32_t>(s.output.h);
    s.ensureScene(sw, sh, integer ? TextureFilter::Nearest : TextureFilter::Linear);
    if (!s.scene[0].valid() || !s.scene[1].valid()) {
        dev->endFrame();
        return;
    }

    Queue& q = s.frameQueue;
    s.sortQueue(q);
    std::vector<uint32_t> sorted = s.order;

    float pxPerLogicalX = sw / s.logicalW;
    float pxPerLogicalY = sh / s.logicalH;

    // Clear the scene once.
    {
        std::vector<uint32_t> none;
        s.submit(q, none, s.scene[0], true, s.clearColor, {0, 0, (float)sw, (float)sh},
                 Mat3::identity(), 1.f, 1.f);
    }

    // ---- cameras ----
    struct Cam { CameraHandle h; CameraEntry e; };
    std::vector<Cam> cams;
    s.cameras.forEach([&](CameraHandle h, CameraEntry& e) {
        if (e.desc.enabled) cams.push_back({h, e});
    });
    std::stable_sort(cams.begin(), cams.end(), [](const Cam& a, const Cam& b) {
        if (a.e.desc.order != b.e.desc.order) return a.e.desc.order < b.e.desc.order;
        return a.e.created < b.e.created;
    });
    std::sort(s.viewHooks.begin(), s.viewHooks.end(),
              [](const Impl::Hook& a, const Impl::Hook& b) { return a.order < b.order; });

    std::vector<uint32_t> visible;
    for (const Cam& c : cams) {
        const CameraDesc& d = c.e.desc;
        TextureHandle target = d.target.valid() ? d.target : s.scene[0];
        TextureEntry* te = s.textures.get(target);
        if (!te) continue;
        bool toScene = !d.target.valid();
        Rect vp = {d.viewport.x * te->width, d.viewport.y * te->height,
                   d.viewport.w * te->width, d.viewport.h * te->height};
        float viewW = toScene ? d.viewport.w * s.logicalW : vp.w;
        float viewH = toScene ? d.viewport.h * s.logicalH : vp.h;
        Mat3 vpm = cameraViewProj(d, viewW, viewH);
        Rect bounds = cameraBounds(d, viewW, viewH);

        visible.clear();
        for (uint32_t ii : sorted) {
            const Item& it = q.items[ii];
            if (it.layer == ScreenLayer || !(d.layerMask & (1u << it.layer))) continue;
            if (it.maxX < bounds.x || it.minX > bounds.x + bounds.w ||
                it.maxY < bounds.y || it.minY > bounds.y + bounds.h) continue;
            visible.push_back(ii);
        }
        float scaleX = toScene ? pxPerLogicalX : 1.f;
        float scaleY = toScene ? pxPerLogicalY : 1.f;
        s.submit(q, visible, target, d.clear, d.clearColor, vp, vpm, scaleX, scaleY);

        if (!s.viewHooks.empty()) {
            ViewInfo info;
            info.camera = c.h;
            info.viewProj = vpm;
            info.worldBounds = bounds;
            info.target = target;
            info.viewportPx = vp;
            info.targetSize = {static_cast<float>(te->width), static_cast<float>(te->height)};
            for (auto& hook : s.viewHooks) hook.fn(*this, info);
        }
    }

    // ---- post effects ----
    int cur = 0;
    std::sort(s.postEffects.begin(), s.postEffects.end(),
              [](const Impl::Post& a, const Impl::Post& b) { return a.order < b.order; });
    for (auto& fx : s.postEffects) {
        ViewInfo out;
        out.camera = s.defaultCamera;
        out.viewProj = Mat3::ortho(static_cast<float>(sw), static_cast<float>(sh));
        out.worldBounds = {0, 0, (float)sw, (float)sh};
        out.target = s.scene[1 - cur];
        out.viewportPx = {0, 0, (float)sw, (float)sh};
        out.targetSize = {(float)sw, (float)sh};
        fx.fn(*this, s.scene[cur], out);
        cur = 1 - cur;
    }

    // ---- screen layer ----
    visible.clear();
    for (uint32_t ii : sorted) {
        if (q.items[ii].layer == ScreenLayer) visible.push_back(ii);
    }
    if (!visible.empty()) {
        Mat3 screenProj = Mat3::ortho(s.logicalW, s.logicalH);
        s.submit(q, visible, s.scene[cur], false, {}, {0, 0, (float)sw, (float)sh},
                 screenProj, pxPerLogicalX, pxPerLogicalY);
    }

    // ---- present ----
    {
        Queue& pq = s.passQueue;
        pq.clear();
        s.current = &pq;
        Vec2 p[4] = {{s.output.x, s.output.y}, {s.output.x + s.output.w, s.output.y},
                     {s.output.x + s.output.w, s.output.y + s.output.h}, {s.output.x, s.output.y + s.output.h}};
        s.quad(s.scene[cur], p, 0.f, 0.f, 1.f, 1.f, Color::white(), 0.f, BlendMode::Opaque, {}, 0);
        s.current = &s.frameQueue;
        std::vector<uint32_t> one = {0};
        s.submit(pq, one, {}, true, {0.f, 0.f, 0.f, 1.f},
                 {0, 0, (float)s.drawableW, (float)s.drawableH},
                 Mat3::ortho((float)s.drawableW, (float)s.drawableH), 1.f, 1.f);
        pq.clear();
    }

    if (!s.screenshotPath.empty()) {
        int32_t w = 0, h = 0;
        std::vector<uint8_t> px;
        if (dev->readPixels(0, &w, &h, px)) {
            if (stbi_write_png(s.screenshotPath.c_str(), w, h, 4, px.data(), w * 4)) {
                DRIFT_LOG_INFO("Renderer: screenshot saved to %s", s.screenshotPath.c_str());
            } else {
                DRIFT_LOG_ERROR("Renderer: could not write %s", s.screenshotPath.c_str());
            }
        }
        s.screenshotPath.clear();
    }

    if (s.overlay) s.overlay();
    dev->endFrame();
}

void RendererResource::setClearColor(ColorF color) {
    impl_->clearColor = color;
}

// =============================================================================
// Textures
// =============================================================================

static uint8_t* decodeImageFile(const char* path, int* w, int* h) {
    size_t size = 0;
    void* data = SDL_LoadFile(path, &size);
    if (!data) {
        DRIFT_LOG_ERROR("loadTexture: cannot read '%s': %s", path, SDL_GetError());
        return nullptr;
    }
    int n = 0;
    uint8_t* pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(data),
                                            static_cast<int>(size), w, h, &n, 4);
    SDL_free(data);
    if (!pixels) {
        DRIFT_LOG_ERROR("loadTexture: cannot decode '%s': %s", path, stbi_failure_reason());
    }
    return pixels;
}

TextureHandle RendererResource::loadTexture(const char* path, TextureOptions options) {
    if (!path || !impl_->device) return {};
    int w = 0, h = 0;
    uint8_t* pixels = decodeImageFile(path, &w, &h);
    if (!pixels) return {};
    TextureHandle t = createTextureFromPixels(pixels, w, h, options);
    stbi_image_free(pixels);
    return t;
}

bool RendererResource::reloadTexture(TextureHandle texture, const char* path) {
    TextureEntry* e = impl_->textures.get(texture);
    if (!e || !path || e->renderTarget || !impl_->device) return false;
    int w = 0, h = 0;
    uint8_t* pixels = decodeImageFile(path, &w, &h);
    if (!pixels) return false;
    if (w == e->width && h == e->height) {
        impl_->device->updateTexture(e->id, 0, 0, w, h, pixels);
    } else {
        gfx::TextureDesc d;
        d.width = w;
        d.height = h;
        d.filter = toGfx(e->options.filter);
        d.wrap = toGfx(e->options.wrap);
        gfx::TextureId id = impl_->device->createTexture(d, pixels);
        if (!id) {
            stbi_image_free(pixels);
            return false;
        }
        impl_->device->destroyTexture(e->id);
        e->id = id;
        e->width = w;
        e->height = h;
    }
    stbi_image_free(pixels);
    return true;
}

TextureHandle RendererResource::createTextureFromPixels(const uint8_t* rgba, int w, int h,
                                                        TextureOptions options) {
    if (!impl_->device || w <= 0 || h <= 0) return {};
    gfx::TextureDesc d;
    d.width = w;
    d.height = h;
    d.filter = toGfx(options.filter);
    d.wrap = toGfx(options.wrap);
    gfx::TextureId id = impl_->device->createTexture(d, rgba);
    if (!id) return {};
    return impl_->textures.create({id, w, h, false, options});
}

TextureHandle RendererResource::createRenderTarget(int w, int h, TextureOptions options) {
    if (!impl_->device || w <= 0 || h <= 0) return {};
    gfx::TextureDesc d;
    d.width = w;
    d.height = h;
    d.filter = toGfx(options.filter);
    d.wrap = toGfx(options.wrap);
    d.renderTarget = true;
    gfx::TextureId id = impl_->device->createTexture(d, nullptr);
    if (!id) return {};
    return impl_->textures.create({id, w, h, true, options});
}

Image RendererResource::readTexture(TextureHandle texture) const {
    const TextureEntry* e = impl_->textures.get(texture);
    if (!e || !e->renderTarget || !impl_->device) return {};
    int32_t w = 0, h = 0;
    std::vector<uint8_t> px;
    if (!impl_->device->readPixels(e->id, &w, &h, px)) return {};
    Image img(w, h);
    std::memcpy(img.data(), px.data(), std::min(px.size(), static_cast<size_t>(w) * h * 4));
    return img;
}

void RendererResource::updateTexture(TextureHandle texture, int x, int y, int w, int h,
                                     const uint8_t* rgba) {
    if (TextureEntry* e = impl_->textures.get(texture)) {
        impl_->device->updateTexture(e->id, x, y, w, h, rgba);
    }
}

void RendererResource::setTextureOptions(TextureHandle texture, TextureOptions options) {
    if (TextureEntry* e = impl_->textures.get(texture)) {
        e->options = options;
        impl_->device->setTextureSampling(e->id, toGfx(options.filter), toGfx(options.wrap));
    }
}

void RendererResource::destroyTexture(TextureHandle texture) {
    if (TextureEntry* e = impl_->textures.get(texture)) {
        impl_->device->destroyTexture(e->id);
        impl_->textures.destroy(texture);
    }
}

void RendererResource::getTextureSize(TextureHandle texture, int32_t* w, int32_t* h) const {
    const TextureEntry* e = impl_->textures.get(texture);
    if (w) *w = e ? e->width : 0;
    if (h) *h = e ? e->height : 0;
}

Vec2 RendererResource::textureSize(TextureHandle texture) const {
    const TextureEntry* e = impl_->textures.get(texture);
    return e ? Vec2{static_cast<float>(e->width), static_cast<float>(e->height)} : Vec2{};
}

TextureHandle RendererResource::whiteTexture() const { return impl_->white; }

// =============================================================================
// Shaders and materials
// =============================================================================

static std::string readText(const std::string& path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return {};
    std::string s(static_cast<const char*>(data), size);
    SDL_free(data);
    return s;
}

ShaderHandle RendererResource::loadShader(const char* path) {
    if (!path) return {};
    std::string base = path;
    for (const char* ext : {".wgsl", ".frag.glsl", ".glsl"}) {
        size_t n = std::strlen(ext);
        if (base.size() > n && base.compare(base.size() - n, n, ext) == 0) {
            base.erase(base.size() - n);
            break;
        }
    }
    ShaderDesc d;
    d.wgsl = readText(base + ".wgsl");
    d.glslFragment = readText(base + ".frag.glsl");
    d.glslVertex = readText(base + ".vert.glsl");
    if (d.wgsl.empty() && d.glslFragment.empty()) {
        DRIFT_LOG_ERROR("loadShader: no %s.wgsl or %s.frag.glsl found", base.c_str(), base.c_str());
        return {};
    }
    return createShader(d);
}

ShaderHandle RendererResource::createShader(const ShaderDesc& desc) {
    if (!impl_->device) return {};
    gfx::ShaderSource src;
    if (!desc.wgsl.empty()) {
        src.wgsl = std::string(gfx::shaders::prelude_wgsl) + "\n" + desc.wgsl;
    }
    if (!desc.glslFragment.empty()) {
        size_t first = desc.glslFragment.find_first_not_of(" \t\r\n");
        bool fullModule = first != std::string::npos &&
                          desc.glslFragment.compare(first, 8, "#version") == 0;
        src.glslFragment = fullModule ? desc.glslFragment
                                      : std::string(gfx::shaders::prelude_frag_glsl) + desc.glslFragment;
        if (fullModule) src.glslVertex = desc.glslVertex;
    }
    std::string error;
    gfx::ShaderId id = impl_->device->createShader(src, &error);
    if (!id) {
        DRIFT_LOG_ERROR("createShader (%s): %s", impl_->device->backendName(), error.c_str());
        return {};
    }
    return impl_->shaders.create({id});
}

void RendererResource::destroyShader(ShaderHandle shader) {
    if (ShaderEntry* e = impl_->shaders.get(shader)) {
        impl_->device->destroyShader(e->id);
        impl_->shaders.destroy(shader);
    }
}

MaterialHandle RendererResource::createMaterial(ShaderHandle shader) {
    MaterialEntry m;
    m.shader = shader;
    return impl_->materials.create(m);
}

void RendererResource::setMaterialParam(MaterialHandle material, int index, Vec4 value) {
    MaterialEntry* m = impl_->materials.get(material);
    if (!m || index < 0 || index >= gfx::kMaterialParamCount) return;
    m->params.v[index][0] = value.x;
    m->params.v[index][1] = value.y;
    m->params.v[index][2] = value.z;
    m->params.v[index][3] = value.w;
}

void RendererResource::setMaterialTexture(MaterialHandle material, int slot, TextureHandle texture) {
    MaterialEntry* m = impl_->materials.get(material);
    if (!m || slot < 1 || slot >= gfx::kMaxTextureSlots) return;
    m->textures[slot] = texture;
}

void RendererResource::destroyMaterial(MaterialHandle material) {
    impl_->materials.destroy(material);
}

// =============================================================================
// Drawing
// =============================================================================

void RendererResource::draw(const SpriteDraw& sprite) {
    impl_->sprite(sprite);
}

void RendererResource::drawSprite(TextureHandle texture, Vec2 position, float zOrder) {
    SpriteDraw d;
    d.texture = texture;
    d.position = position;
    d.zOrder = zOrder;
    d.layer = impl_->layer;
    impl_->sprite(d);
}

void RendererResource::drawSprite(TextureHandle texture, Vec2 position, Rect srcRect, float zOrder) {
    SpriteDraw d;
    d.texture = texture;
    d.position = position;
    d.srcRect = srcRect;
    d.zOrder = zOrder;
    d.layer = impl_->layer;
    impl_->sprite(d);
}

void RendererResource::drawSprite(TextureHandle texture, Vec2 position, Rect srcRect,
                                  Vec2 scale, float rotation, Vec2 origin,
                                  Color tint, Flip flip, float zOrder, BlendMode blend) {
    SpriteDraw d;
    d.texture = texture;
    d.position = position;
    d.srcRect = srcRect;
    d.scale = scale;
    d.rotation = rotation;
    d.origin = origin;
    d.tint = tint;
    d.flip = flip;
    d.zOrder = zOrder;
    d.blend = blend;
    d.layer = impl_->layer;
    impl_->sprite(d);
}

void RendererResource::drawSpriteBatch(TextureHandle texture, Rect srcRect,
                                       const Vec2* positions, const float* sizes,
                                       const float* rotations, const Color* tints,
                                       int32_t count, float zOrder, BlendMode blend) {
    const TextureEntry* e = impl_->textures.get(texture);
    if (!e || count <= 0) return;
    Rect src = srcRect;
    if (src.w <= 0.f || src.h <= 0.f) src = {0, 0, (float)e->width, (float)e->height};
    float u0 = src.x / e->width, v0 = src.y / e->height;
    float u1 = (src.x + src.w) / e->width, v1 = (src.y + src.h) / e->height;

    // One item for the whole batch: particles share texture, z and blend.
    gfx::Vertex* v;
    uint32_t* idx;
    Item& it = impl_->beginItem(texture, zOrder, blend, {}, impl_->layer,
                                static_cast<uint32_t>(count) * 4, static_cast<uint32_t>(count) * 6, &v, &idx);
    for (int32_t i = 0; i < count; ++i) {
        float h = sizes[i] * 0.5f;
        float c = std::cos(rotations[i]), sn = std::sin(rotations[i]);
        float lx[4] = {-h, h, h, -h}, ly[4] = {-h, -h, h, h};
        float uu[4] = {u0, u1, u1, u0}, vv[4] = {v0, v0, v1, v1};
        for (int j = 0; j < 4; ++j) {
            v[i * 4 + j] = Impl::vert(positions[i].x + lx[j] * c - ly[j] * sn,
                                      positions[i].y + lx[j] * sn + ly[j] * c,
                                      uu[j], vv[j], tints[i]);
        }
        uint32_t b = static_cast<uint32_t>(i) * 4;
        uint32_t* o = idx + i * 6;
        o[0] = b; o[1] = b + 1; o[2] = b + 2; o[3] = b; o[4] = b + 2; o[5] = b + 3;
    }
    Impl::finishItem(*impl_->current, it);
    impl_->spriteCountVal += count;
}

void RendererResource::drawNineSlice(TextureHandle texture, Rect dest, Rect srcRect, Vec4 borders,
                                     Color tint, float zOrder) {
    const TextureEntry* e = impl_->textures.get(texture);
    if (!e) return;
    Rect src = srcRect;
    if (src.w <= 0.f || src.h <= 0.f) src = {0, 0, (float)e->width, (float)e->height};
    float l = borders.x, t = borders.y, r = borders.z, b = borders.w;
    // Shrink borders if the destination is smaller than both borders.
    float sx = dest.w < l + r ? dest.w / (l + r) : 1.f;
    float sy = dest.h < t + b ? dest.h / (t + b) : 1.f;
    float dx[4] = {dest.x, dest.x + l * sx, dest.x + dest.w - r * sx, dest.x + dest.w};
    float dy[4] = {dest.y, dest.y + t * sy, dest.y + dest.h - b * sy, dest.y + dest.h};
    float ux[4] = {src.x, src.x + l, src.x + src.w - r, src.x + src.w};
    float uy[4] = {src.y, src.y + t, src.y + src.h - b, src.y + src.h};
    for (int i = 0; i < 4; ++i) { ux[i] /= e->width; uy[i] /= e->height; }

    gfx::Vertex* v;
    uint32_t* idx;
    Item& it = impl_->beginItem(texture, zOrder, BlendMode::Alpha, {}, impl_->layer, 16, 54, &v, &idx);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            v[y * 4 + x] = Impl::vert(dx[x], dy[y], ux[x], uy[y], tint);
    int k = 0;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) {
            uint32_t a = y * 4 + x;
            uint32_t q[6] = {a, a + 1, a + 5, a, a + 5, a + 4};
            for (uint32_t i : q) idx[k++] = i;
        }
    Impl::finishItem(*impl_->current, it);
}

void RendererResource::drawMesh(TextureHandle texture, const MeshVertex* vertices, int32_t vertexCount,
                                const uint32_t* indices, int32_t indexCount, float zOrder,
                                BlendMode blend, MaterialHandle material) {
    if (vertexCount <= 0 || indexCount <= 0) return;
    if (!impl_->textures.valid(texture)) texture = impl_->white;
    gfx::Vertex* v;
    uint32_t* idx;
    Item& it = impl_->beginItem(texture, zOrder, blend, material, impl_->layer,
                                static_cast<uint32_t>(vertexCount), static_cast<uint32_t>(indexCount), &v, &idx);
    for (int32_t i = 0; i < vertexCount; ++i) {
        const MeshVertex& m = vertices[i];
        v[i] = Impl::vert(m.position.x, m.position.y, m.uv.x, m.uv.y, m.color);
    }
    std::memcpy(idx, indices, static_cast<size_t>(indexCount) * sizeof(uint32_t));
    Impl::finishItem(*impl_->current, it);
}

void RendererResource::drawLine(Vec2 start, Vec2 end, Color color, float thickness, float zOrder) {
    Vec2 pts[2] = {start, end};
    impl_->polyline(pts, 2, color, thickness, false, zOrder);
}

void RendererResource::drawPolyline(const Vec2* points, int32_t count, Color color, float thickness,
                                    bool closed, float zOrder) {
    impl_->polyline(points, count, color, thickness, closed, zOrder);
}

void RendererResource::drawRect(Rect rect, Color color, float thickness, float zOrder) {
    Vec2 pts[4] = {{rect.x, rect.y}, {rect.x + rect.w, rect.y},
                   {rect.x + rect.w, rect.y + rect.h}, {rect.x, rect.y + rect.h}};
    impl_->polyline(pts, 4, color, thickness, true, zOrder);
}

void RendererResource::drawRectFilled(Rect rect, Color color, float zOrder) {
    Vec2 pts[4] = {{rect.x, rect.y}, {rect.x + rect.w, rect.y},
                   {rect.x + rect.w, rect.y + rect.h}, {rect.x, rect.y + rect.h}};
    uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
    impl_->solid(pts, 4, idx, 6, color, zOrder);
}

void RendererResource::drawRoundedRectFilled(Rect rect, float radius, Color color, float zOrder) {
    radius = std::min(radius, std::min(rect.w, rect.h) * 0.5f);
    if (radius <= 0.f) {
        drawRectFilled(rect, color, zOrder);
        return;
    }
    int seg = std::max(3, autoSegments(radius) / 4);
    std::vector<Vec2> ring;
    Vec2 corners[4] = {{rect.x + rect.w - radius, rect.y + radius},
                       {rect.x + rect.w - radius, rect.y + rect.h - radius},
                       {rect.x + radius, rect.y + rect.h - radius},
                       {rect.x + radius, rect.y + radius}};
    for (int c = 0; c < 4; ++c) {
        float a0 = -kPi * 0.5f + c * kPi * 0.5f;
        for (int i = 0; i <= seg; ++i) {
            float a = a0 + (kPi * 0.5f) * i / seg;
            ring.push_back({corners[c].x + std::cos(a) * radius, corners[c].y + std::sin(a) * radius});
        }
    }
    impl_->fan(ring, {rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f}, color, zOrder);
}

void RendererResource::drawCircle(Vec2 center, float radius, Color color, float thickness,
                                  int segments, float zOrder) {
    if (segments < 3) segments = autoSegments(radius);
    std::vector<Vec2> pts(segments);
    for (int i = 0; i < segments; ++i) {
        float a = 2.f * kPi * i / segments;
        pts[i] = {center.x + std::cos(a) * radius, center.y + std::sin(a) * radius};
    }
    impl_->polyline(pts.data(), segments, color, thickness, true, zOrder);
}

void RendererResource::drawCircleFilled(Vec2 center, float radius, Color color, int segments, float zOrder) {
    if (segments < 3) segments = autoSegments(radius);
    std::vector<Vec2> ring(segments);
    for (int i = 0; i < segments; ++i) {
        float a = 2.f * kPi * i / segments;
        ring[i] = {center.x + std::cos(a) * radius, center.y + std::sin(a) * radius};
    }
    impl_->fan(ring, center, color, zOrder);
}

void RendererResource::drawArc(Vec2 center, float radius, float startAngle, float endAngle, Color color,
                               float thickness, float zOrder) {
    float span = endAngle - startAngle;
    int segments = std::max(2, static_cast<int>(autoSegments(radius) * std::fabs(span) / (2.f * kPi)));
    std::vector<Vec2> pts(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        float a = startAngle + span * i / segments;
        pts[i] = {center.x + std::cos(a) * radius, center.y + std::sin(a) * radius};
    }
    impl_->polyline(pts.data(), segments + 1, color, thickness, false, zOrder);
}

void RendererResource::drawTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Color color, float zOrder) {
    Vec2 pts[3] = {a, b, c};
    uint32_t idx[3] = {0, 1, 2};
    impl_->solid(pts, 3, idx, 3, color, zOrder);
}

void RendererResource::drawPolygonFilled(const Vec2* points, int32_t count, Color color, float zOrder) {
    if (count < 3) return;
    // Ear clipping. Work in counter-clockwise order.
    std::vector<uint32_t> poly(count);
    float area = 0.f;
    for (int32_t i = 0; i < count; ++i) {
        const Vec2& p = points[i];
        const Vec2& q = points[(i + 1) % count];
        area += p.x * q.y - q.x * p.y;
    }
    for (int32_t i = 0; i < count; ++i) poly[i] = area > 0 ? i : count - 1 - i;

    std::vector<uint32_t> tris;
    tris.reserve((count - 2) * 3);
    // A convex outline (circles, ellipses, rounded rectangles) is a fan;
    // ear clipping stalls on its collinear and repeated points.
    bool convex = true;
    for (int32_t i = 0; i < count && convex; ++i) {
        Vec2 a = points[poly[(i + count - 1) % count]], b = points[poly[i]], c = points[poly[(i + 1) % count]];
        if (cross(a, b, c) < -1e-4f) convex = false;
    }
    if (convex) {
        for (int32_t i = 1; i + 1 < count; ++i) tris.insert(tris.end(), {poly[0], poly[i], poly[i + 1]});
        impl_->solid(points, static_cast<uint32_t>(count), tris.data(), static_cast<uint32_t>(tris.size()),
                     color, zOrder);
        return;
    }
    int guard = count * count;
    while (poly.size() > 3 && guard-- > 0) {
        bool clipped = false;
        size_t n = poly.size();
        for (size_t i = 0; i < n; ++i) {
            uint32_t ia = poly[(i + n - 1) % n], ib = poly[i], ic = poly[(i + 1) % n];
            Vec2 a = points[ia], b = points[ib], c = points[ic];
            if (cross(a, b, c) <= 0.f) continue;   // reflex
            bool inside = false;
            for (size_t j = 0; j < n && !inside; ++j) {
                uint32_t ip = poly[j];
                if (ip == ia || ip == ib || ip == ic) continue;
                inside = pointInTriangle(points[ip], a, b, c);
            }
            if (inside) continue;
            tris.insert(tris.end(), {ia, ib, ic});
            poly.erase(poly.begin() + static_cast<long>(i));
            clipped = true;
            break;
        }
        if (!clipped) break;   // degenerate input
    }
    if (poly.size() == 3) tris.insert(tris.end(), {poly[0], poly[1], poly[2]});
    impl_->solid(points, static_cast<uint32_t>(count), tris.data(), static_cast<uint32_t>(tris.size()),
                 color, zOrder);
}

void RendererResource::setLayer(uint8_t layer) { impl_->layer = layer; }
uint8_t RendererResource::layer() const { return impl_->layer; }

void RendererResource::pushClipRect(Rect rect) {
    if (!impl_->clipStack.empty()) {
        const Rect& p = impl_->clipStack.back();
        float x0 = std::max(rect.x, p.x), y0 = std::max(rect.y, p.y);
        float x1 = std::min(rect.x + rect.w, p.x + p.w), y1 = std::min(rect.y + rect.h, p.y + p.h);
        rect = {x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
    }
    impl_->clipStack.push_back(rect);
}

void RendererResource::popClipRect() {
    if (!impl_->clipStack.empty()) impl_->clipStack.pop_back();
}

// =============================================================================
// Cameras
// =============================================================================

CameraHandle RendererResource::getDefaultCamera() const { return impl_->defaultCamera; }

CameraHandle RendererResource::createCamera(const CameraDesc& desc) {
    return impl_->cameras.create({desc, impl_->cameraSeq++});
}

void RendererResource::destroyCamera(CameraHandle camera) {
    if (camera == impl_->defaultCamera) return;
    impl_->cameras.destroy(camera);
}

void RendererResource::setCamera(CameraHandle camera, const CameraDesc& desc) {
    if (CameraEntry* e = impl_->cameras.get(camera)) {
        e->desc = desc;
        if (e->desc.zoom < 0.0001f) e->desc.zoom = 0.0001f;
    }
}

CameraDesc RendererResource::camera(CameraHandle camera) const {
    const CameraEntry* e = impl_->cameras.get(camera);
    return e ? e->desc : CameraDesc{};
}

void RendererResource::setCameraPosition(CameraHandle camera, Vec2 position) {
    if (CameraEntry* e = impl_->cameras.get(camera)) e->desc.position = position;
}

void RendererResource::setCameraZoom(CameraHandle camera, float zoom) {
    if (CameraEntry* e = impl_->cameras.get(camera)) e->desc.zoom = std::max(zoom, 0.0001f);
}

void RendererResource::setCameraRotation(CameraHandle camera, float rotation) {
    if (CameraEntry* e = impl_->cameras.get(camera)) e->desc.rotation = rotation;
}

Vec2 RendererResource::getCameraPosition(CameraHandle camera) const {
    const CameraEntry* e = impl_->cameras.get(camera);
    return e ? e->desc.position : Vec2{};
}

float RendererResource::getCameraZoom(CameraHandle camera) const {
    const CameraEntry* e = impl_->cameras.get(camera);
    return e ? e->desc.zoom : 1.f;
}

Vec2 RendererResource::screenToWorld(CameraHandle camera, Vec2 screenPos) const {
    const CameraEntry* e = impl_->cameras.get(camera);
    if (!e) return screenPos;
    const CameraDesc& d = e->desc;
    float vx = d.viewport.x * impl_->logicalW, vy = d.viewport.y * impl_->logicalH;
    float vw = d.viewport.w * impl_->logicalW, vh = d.viewport.h * impl_->logicalH;
    float dx = (screenPos.x - vx - vw * 0.5f) / d.zoom;
    float dy = (screenPos.y - vy - vh * 0.5f) / d.zoom;
    float c = std::cos(d.rotation), s = std::sin(d.rotation);
    return {d.position.x + dx * c - dy * s, d.position.y + dx * s + dy * c};
}

Vec2 RendererResource::worldToScreen(CameraHandle camera, Vec2 worldPos) const {
    const CameraEntry* e = impl_->cameras.get(camera);
    if (!e) return worldPos;
    const CameraDesc& d = e->desc;
    float vx = d.viewport.x * impl_->logicalW, vy = d.viewport.y * impl_->logicalH;
    float vw = d.viewport.w * impl_->logicalW, vh = d.viewport.h * impl_->logicalH;
    float dx = worldPos.x - d.position.x, dy = worldPos.y - d.position.y;
    float c = std::cos(-d.rotation), s = std::sin(-d.rotation);
    return {(dx * c - dy * s) * d.zoom + vx + vw * 0.5f,
            (dx * s + dy * c) * d.zoom + vy + vh * 0.5f};
}

Vec2 RendererResource::screenToWorld(Vec2 screenPos) const {
    return screenToWorld(impl_->defaultCamera, screenPos);
}

Vec2 RendererResource::worldToScreen(Vec2 worldPos) const {
    return worldToScreen(impl_->defaultCamera, worldPos);
}

Rect RendererResource::cameraWorldBounds(CameraHandle camera) const {
    const CameraEntry* e = impl_->cameras.get(camera);
    if (!e) return {};
    return cameraBounds(e->desc, e->desc.viewport.w * impl_->logicalW, e->desc.viewport.h * impl_->logicalH);
}

// =============================================================================
// Resolution
// =============================================================================

float RendererResource::logicalWidth() const { return impl_->logicalW; }
float RendererResource::logicalHeight() const { return impl_->logicalH; }

void RendererResource::setLogicalSize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    impl_->configW = static_cast<float>(w);
    impl_->configH = static_cast<float>(h);
    impl_->logicalW = impl_->configW;
    impl_->logicalH = impl_->configH;
}

ScaleMode RendererResource::scaleMode() const { return impl_->scaleMode; }
void RendererResource::setScaleMode(ScaleMode mode) { impl_->scaleMode = mode; }

Vec2 RendererResource::windowToLogical(Vec2 windowPos) const {
    // windowPos is in window coordinates; the output rect is in pixels.
    int ww = 0, wh = 0;
    SDL_GetWindowSize(impl_->window, &ww, &wh);
    float density = (ww > 0 && impl_->drawableW > 0) ? static_cast<float>(impl_->drawableW) / ww : 1.f;
    const Rect& o = impl_->output;
    if (o.w <= 0.f || o.h <= 0.f) return windowPos;
    float px = windowPos.x * density, py = windowPos.y * density;
    return {(px - o.x) / o.w * impl_->logicalW, (py - o.y) / o.h * impl_->logicalH};
}

Rect RendererResource::outputRect() const { return impl_->output; }

// =============================================================================
// Frame extension
// =============================================================================

int32_t RendererResource::addViewHook(int32_t order, ViewHook hook) {
    int32_t id = impl_->nextHookId++;
    impl_->viewHooks.push_back({id, order, std::move(hook)});
    return id;
}

void RendererResource::removeViewHook(int32_t id) {
    auto& v = impl_->viewHooks;
    v.erase(std::remove_if(v.begin(), v.end(), [id](const Impl::Hook& h) { return h.id == id; }), v.end());
}

int32_t RendererResource::addPostEffect(int32_t order, PostEffect effect) {
    int32_t id = impl_->nextHookId++;
    impl_->postEffects.push_back({id, order, std::move(effect)});
    return id;
}

int32_t RendererResource::addPostEffect(int32_t order, MaterialHandle material) {
    return addPostEffect(order, [material](RendererResource& r, TextureHandle source, const ViewInfo& out) {
        r.beginPass(out.target);
        r.drawFullscreen(source, BlendMode::Opaque, material);
        r.endPass();
    });
}

void RendererResource::removePostEffect(int32_t id) {
    auto& v = impl_->postEffects;
    v.erase(std::remove_if(v.begin(), v.end(), [id](const Impl::Post& p) { return p.id == id; }), v.end());
}

void RendererResource::beginPass(TextureHandle target, const PassOptions& options) {
    Impl& s = *impl_;
    if (s.inPass) {
        DRIFT_LOG_WARN("beginPass: already in a pass");
        return;
    }
    TextureEntry* te = s.textures.get(target);
    if (!te || !te->renderTarget) {
        DRIFT_LOG_ERROR("beginPass: target is not a render target");
        return;
    }
    s.inPass = true;
    s.passTarget = target;
    s.passOptions = options;
    Rect vp = options.viewportPx;
    if (vp.w <= 0.f || vp.h <= 0.f) vp = {0, 0, (float)te->width, (float)te->height};
    s.passOptions.viewportPx = vp;
    s.passViewProj = options.viewProj ? *options.viewProj : Mat3::ortho(vp.w, vp.h);
    s.passQueue.clear();
    s.current = &s.passQueue;
}

void RendererResource::endPass() {
    Impl& s = *impl_;
    if (!s.inPass) return;
    s.current = &s.frameQueue;
    s.inPass = false;
    // Explicit passes draw in submission order.
    std::vector<uint32_t> ord(s.passQueue.items.size());
    for (uint32_t i = 0; i < ord.size(); ++i) ord[i] = i;
    s.submit(s.passQueue, ord, s.passTarget, s.passOptions.clear, s.passOptions.clearColor,
             s.passOptions.viewportPx, s.passViewProj, 1.f, 1.f);
    s.passQueue.clear();
}

void RendererResource::drawFullscreen(TextureHandle source, BlendMode blend, MaterialHandle material,
                                      Color tint) {
    Impl& s = *impl_;
    if (!s.inPass) {
        DRIFT_LOG_WARN("drawFullscreen: only valid between beginPass and endPass");
        return;
    }
    // Cover the pass viewport in pixel space regardless of the pass projection.
    Mat3 inv = s.passViewProj.inverse();
    Vec2 p[4];
    const float clip[4][2] = {{-1.f, 1.f}, {1.f, 1.f}, {1.f, -1.f}, {-1.f, -1.f}};
    for (int i = 0; i < 4; ++i) inv.transformPoint(clip[i][0], clip[i][1], &p[i].x, &p[i].y);
    if (!s.textures.valid(source)) source = s.white;
    s.quad(source, p, 0.f, 0.f, 1.f, 1.f, tint, 0.f, blend, material, 0);
}

// =============================================================================
// Debugging and backend access
// =============================================================================

void RendererResource::captureScreenshot(const char* path) {
    if (path) impl_->screenshotPath = path;
}

int32_t RendererResource::drawCalls() const { return impl_->lastDrawCalls; }
int32_t RendererResource::spriteCount() const { return impl_->lastSpriteCount; }

const char* RendererResource::backendName() const {
    return impl_->device ? impl_->device->backendName() : "none";
}

void RendererResource::setOverlayCallback(std::function<void()> fn) {
    impl_->overlay = std::move(fn);
}

gfx::Device* RendererResource::device() const { return impl_->device; }
void* RendererResource::getWindow() const { return impl_->window; }

uint64_t RendererResource::imguiTextureId(TextureHandle texture) const {
    const TextureEntry* e = impl_->textures.get(texture);
    return (e && impl_->device) ? impl_->device->imguiTexture(e->id) : 0;
}

} // namespace drift
