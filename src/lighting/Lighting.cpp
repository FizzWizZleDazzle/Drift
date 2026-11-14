// =============================================================================
// Drift 2D Game Engine - Lighting
// =============================================================================
// Collects lights and occluders each frame, then builds a light map per lit
// camera from a view hook:
//
//   1. Clear the light map to the ambient color.
//   2. Draw every light without shadows additively, each with its own
//      material so all of them fit in one pass.
//   3. For each shadow-casting light: draw the light into a scratch target,
//      cover its shadows (occluder edges extruded away from the light) with
//      black, and add the light's area of the scratch target to the map.
//   4. Multiply the light map over the camera's viewport.
// =============================================================================

#include <drift/plugins/LightingPlugin.hpp>
#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/components/GlobalTransform.hpp>
#include <drift/components/Physics.hpp>
#include <drift/resources/RendererResource.hpp>

#include "lighting_shaders.gen.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

namespace drift {

namespace {

constexpr float kPi = 3.14159265358979f;

struct LightData {
    Vec2     position;
    ColorF   color;          // premultiplied by intensity
    float    radius = 0.f;
    float    falloff = 1.f;
    Vec2     direction;      // unit, spot lights only
    float    cosOuter = -2.f;
    float    cosInner = -2.f;
    bool     shadows = false;
    uint32_t layerMask = 0;
    Rect     bounds;
};

struct OccluderData {
    uint32_t first = 0, count = 0;   // into LightingState::points
    Rect     bounds;
    bool     shadowSelf = false;
    uint32_t layerMask = 0;
};

// What the collect system found this frame. Read by the view hook.
struct LightingState : public Resource {
    DRIFT_RESOURCE(LightingState)
    std::vector<LightData>    lights;
    std::vector<OccluderData> occluders;
    std::vector<Vec2>         points;
    uint64_t                  frame = 0;
};

Transform2D worldTransform(const Transform2D& local, const GlobalTransform2D* global) {
    if (!global) return local;
    return {global->position, global->rotation, global->scale};
}

Rect boundsOf(const Vec2* pts, uint32_t n) {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (uint32_t i = 0; i < n; ++i) {
        x0 = std::min(x0, pts[i].x); x1 = std::max(x1, pts[i].x);
        y0 = std::min(y0, pts[i].y); y1 = std::max(y1, pts[i].y);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

Vec2 rotate(Vec2 v, float c, float s) { return {v.x * c - v.y * s, v.x * s + v.y * c}; }

void addLight(LightingState& st, const Transform2D& t, ColorF color, float intensity, float radius,
              float falloff, bool shadows, uint32_t mask) {
    if (radius <= 0.f || intensity <= 0.f) return;
    LightData l;
    l.position = t.position;
    l.color = {color.r * intensity, color.g * intensity, color.b * intensity, 1.f};
    l.radius = radius;
    l.falloff = std::max(falloff, 0.01f);
    l.shadows = shadows;
    l.layerMask = mask;
    l.bounds = {t.position.x - radius, t.position.y - radius, radius * 2.f, radius * 2.f};
    st.lights.push_back(l);
}

void lighting_collect(ResMut<LightingState> st,
                      Query<Transform2D, PointLight2D, Optional<GlobalTransform2D>> points,
                      Query<Transform2D, SpotLight2D, Optional<GlobalTransform2D>> spots,
                      Query<Transform2D, LightOccluder2D, Optional<GlobalTransform2D>,
                            Optional<BoxCollider2D>, Optional<CircleCollider2D>> occluders) {
    st->lights.clear();
    st->occluders.clear();
    st->points.clear();
    ++st->frame;

    points.iter([&](const Transform2D& lt, const PointLight2D& l, const GlobalTransform2D* g) {
        addLight(*st, worldTransform(lt, g), l.color, l.intensity, l.radius, l.falloff,
                 l.castShadows, l.layerMask);
    });

    spots.iter([&](const Transform2D& lt, const SpotLight2D& l, const GlobalTransform2D* g) {
        Transform2D t = worldTransform(lt, g);
        size_t before = st->lights.size();
        addLight(*st, t, l.color, l.intensity, l.radius, l.falloff, l.castShadows, l.layerMask);
        if (st->lights.size() == before) return;
        LightData& d = st->lights.back();
        float a = t.rotation + l.direction;
        d.direction = {std::cos(a), std::sin(a)};
        float half = std::clamp(l.angle * 0.5f, 0.001f, kPi);
        float soft = std::clamp(l.softness, 0.f, 1.f);
        d.cosOuter = std::cos(half);
        d.cosInner = std::cos(half * (1.f - soft));
        if (d.cosInner <= d.cosOuter + 1e-4f) d.cosInner = d.cosOuter + 1e-4f;
    });

    occluders.iter([&](const Transform2D& lt, const LightOccluder2D& o, const GlobalTransform2D* g,
                       const BoxCollider2D* box, const CircleCollider2D* circle) {
        Transform2D t = worldTransform(lt, g);
        float c = std::cos(t.rotation), s = std::sin(t.rotation);
        Vec2 scale = t.scale;
        Vec2 offset = o.offset;

        // Local-space outline, before scale, rotation and translation.
        Vec2 local[LightOccluder2D::kMaxPoints > 24 ? LightOccluder2D::kMaxPoints : 24];
        uint32_t n = 0;
        auto boxPts = [&](Vec2 h) {
            local[0] = {-h.x, -h.y}; local[1] = {h.x, -h.y};
            local[2] = {h.x, h.y};   local[3] = {-h.x, h.y};
            n = 4;
        };
        auto circlePts = [&](float r) {
            n = 24;
            for (uint32_t i = 0; i < n; ++i) {
                float a = 2.f * kPi * i / n;
                local[i] = {std::cos(a) * r, std::sin(a) * r};
            }
        };
        switch (o.shape) {
        case OccluderShape::Box:    boxPts(o.halfSize); break;
        case OccluderShape::Circle: circlePts(o.radius); break;
        case OccluderShape::Polygon:
            n = std::min<uint32_t>(o.pointCount, LightOccluder2D::kMaxPoints);
            for (uint32_t i = 0; i < n; ++i) local[i] = o.points[i];
            break;
        case OccluderShape::FromCollider:
            // Colliders are not scaled by the transform (matches physics).
            scale = Vec2::one();
            if (box) { boxPts(box->halfSize); offset = offset + box->offset; }
            else if (circle) { circlePts(circle->radius); offset = offset + circle->offset; }
            break;
        }
        if (n < 2) return;

        OccluderData d;
        d.first = static_cast<uint32_t>(st->points.size());
        d.count = n;
        d.shadowSelf = o.shadowSelf;
        d.layerMask = o.layerMask;
        for (uint32_t i = 0; i < n; ++i) {
            Vec2 p = {(local[i].x + offset.x) * scale.x, (local[i].y + offset.y) * scale.y};
            st->points.push_back(t.position + rotate(p, c, s));
        }
        d.bounds = boundsOf(st->points.data() + d.first, n);
        st->occluders.push_back(d);
    });
}

// GPU side: shader, one material per light drawn this frame, and the light
// map plus scratch target for every lit camera.
struct LightRenderer {
    ShaderHandle shader;
    std::vector<MaterialHandle> materials;

    struct Targets {
        TextureHandle lightMap;
        TextureHandle scratch;
        int32_t w = 0, h = 0;
        uint64_t lastFrame = 0;
    };
    std::unordered_map<uint32_t, Targets> cameras;   // by CameraHandle id

    std::vector<RendererResource::MeshVertex> verts;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> shadowed;
    uint64_t lastSweep = 0;

    MaterialHandle material(RendererResource& r, size_t i) {
        while (materials.size() <= i) materials.push_back(r.createMaterial(shader));
        return materials[i];
    }

    Targets& targetsFor(RendererResource& r, CameraHandle cam, int32_t w, int32_t h, uint64_t frame) {
        Targets& t = cameras[cam.id];
        if (t.w != w || t.h != h || !t.lightMap.valid()) {
            r.destroyTexture(t.lightMap);
            r.destroyTexture(t.scratch);
            t.lightMap = r.createRenderTarget(w, h, {TextureFilter::Linear});
            t.scratch = r.createRenderTarget(w, h, {TextureFilter::Linear});
            t.w = w;
            t.h = h;
        }
        t.lastFrame = frame;
        return t;
    }

    // Drops targets of cameras that have not been lit for a while.
    void sweep(RendererResource& r, uint64_t frame) {
        if (frame - lastSweep < 60) return;
        lastSweep = frame;
        for (auto it = cameras.begin(); it != cameras.end();) {
            if (frame - it->second.lastFrame > 120) {
                r.destroyTexture(it->second.lightMap);
                r.destroyTexture(it->second.scratch);
                it = cameras.erase(it);
            } else {
                ++it;
            }
        }
    }

    void setParams(RendererResource& r, MaterialHandle m, const LightData& l) {
        r.setMaterialParam(m, 0, {l.position.x, l.position.y, l.radius, l.falloff});
        r.setMaterialParam(m, 1, {l.color.r, l.color.g, l.color.b, 1.f});
        r.setMaterialParam(m, 2, {l.direction.x, l.direction.y, l.cosOuter, l.cosInner});
    }

    void drawQuad(RendererResource& r, TextureHandle tex, Rect b, const Vec2 uv[4],
                  BlendMode blend, MaterialHandle m, Color tint = Color::white()) {
        RendererResource::MeshVertex v[4] = {
            {{b.x, b.y}, uv[0], tint},
            {{b.x + b.w, b.y}, uv[1], tint},
            {{b.x + b.w, b.y + b.h}, uv[2], tint},
            {{b.x, b.y + b.h}, uv[3], tint},
        };
        const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
        r.drawMesh(tex, v, 4, idx, 6, 0.f, blend, m);
    }

    void drawLight(RendererResource& r, MaterialHandle m, const LightData& l) {
        const Vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        drawQuad(r, r.whiteTexture(), l.bounds, uv, BlendMode::Additive, m);
    }

    // Appends the shadow of one occluder edge: the area behind (a, b) as
    // seen from the light, out past the light's radius.
    void extrudeEdge(Vec2 L, Vec2 a, Vec2 b, float far) {
        Vec2 da = (a - L).normalized(), db = (b - L).normalized();
        if (da.lengthSq() == 0.f || db.lengthSq() == 0.f) return;
        float angA = std::atan2(da.y, da.x);
        float span = std::atan2(da.x * db.y - da.y * db.x, da.dot(db));   // a -> b, signed
        int steps = std::max(1, static_cast<int>(std::ceil(std::fabs(span) / (kPi * 0.25f))));

        uint32_t base = static_cast<uint32_t>(verts.size());
        Color black = Color::black();
        verts.push_back({a, {0.5f, 0.5f}, black});
        verts.push_back({b, {0.5f, 0.5f}, black});
        // Far arc from b's direction back to a's.
        for (int i = steps; i >= 0; --i) {
            float ang = angA + span * static_cast<float>(i) / steps;
            verts.push_back({L + Vec2{std::cos(ang), std::sin(ang)} * far, {0.5f, 0.5f}, black});
        }
        uint32_t count = static_cast<uint32_t>(verts.size()) - base;
        for (uint32_t i = 1; i + 1 < count; ++i) {
            indices.insert(indices.end(), {base, base + i, base + i + 1});
        }
    }

    void buildShadows(const LightingState& st, const LightData& l, uint32_t camMask) {
        verts.clear();
        indices.clear();
        for (const OccluderData& o : st.occluders) {
            if (!(o.layerMask & l.layerMask) || !(o.layerMask & camMask)) continue;
            if (!o.bounds.overlaps(l.bounds)) continue;
            const Vec2* p = st.points.data() + o.first;
            float area = 0.f;
            float farthest = 0.f;
            for (uint32_t i = 0; i < o.count; ++i) {
                const Vec2& q = p[(i + 1) % o.count];
                area += p[i].x * q.y - q.x * p[i].y;
                farthest = std::max(farthest, (p[i] - l.position).length());
            }
            float far = std::max(l.radius * 2.f, farthest + l.radius);
            for (uint32_t i = 0; i < o.count; ++i) {
                Vec2 a = p[i], b = p[(i + 1) % o.count];
                Vec2 e = b - a;
                Vec2 n = area >= 0.f ? Vec2{e.y, -e.x} : Vec2{-e.y, e.x};   // outward
                bool facesLight = n.dot(l.position - a) > 0.f;
                if (facesLight == o.shadowSelf) extrudeEdge(l.position, a, b, far);
            }
        }
    }

    bool hasShadows(const LightingState& st, const LightData& l, uint32_t camMask) const {
        if (!l.shadows) return false;
        for (const OccluderData& o : st.occluders) {
            if ((o.layerMask & l.layerMask) && (o.layerMask & camMask) && o.bounds.overlaps(l.bounds)) return true;
        }
        return false;
    }

    void render(RendererResource& r, const ViewInfo& view, const LightingSettings& settings,
                const LightingState& st) {
        if (!settings.enabled) return;
        uint32_t camMask = r.camera(view.camera).layerMask;
        if (!(camMask & settings.layerMask)) return;

        float scale = std::clamp(settings.resolutionScale, 0.05f, 2.f);
        int32_t w = std::max(1, static_cast<int32_t>(std::lround(view.viewportPx.w * scale)));
        int32_t h = std::max(1, static_cast<int32_t>(std::lround(view.viewportPx.h * scale)));
        sweep(r, st.frame);
        Targets& t = targetsFor(r, view.camera, w, h, st.frame);
        if (!t.lightMap.valid() || !t.scratch.valid()) return;

        ColorF ambient = settings.ambient;
        ambient.a = 1.f;
        PassOptions world;
        world.viewProj = &view.viewProj;

        // Ambient plus every light that casts no shadow here.
        size_t mi = 0;
        shadowed.clear();
        PassOptions first = world;
        first.clear = true;
        first.clearColor = ambient;
        r.beginPass(t.lightMap, first);
        for (uint32_t i = 0; i < st.lights.size(); ++i) {
            const LightData& l = st.lights[i];
            if (!(l.layerMask & camMask) || !l.bounds.overlaps(view.worldBounds)) continue;
            if (hasShadows(st, l, camMask)) {
                shadowed.push_back(i);
                continue;
            }
            MaterialHandle m = material(r, mi++);
            setParams(r, m, l);
            drawLight(r, m, l);
        }
        r.endPass();

        // Shadow-casting lights, one at a time through the scratch target.
        for (uint32_t i : shadowed) {
            const LightData& l = st.lights[i];
            MaterialHandle m = material(r, mi++);
            setParams(r, m, l);

            PassOptions scratch = world;
            scratch.clear = true;
            scratch.clearColor = {0.f, 0.f, 0.f, 0.f};
            r.beginPass(t.scratch, scratch);
            drawLight(r, m, l);
            buildShadows(st, l, camMask);
            if (!indices.empty()) {
                r.drawMesh(r.whiteTexture(), verts.data(), static_cast<int32_t>(verts.size()),
                           indices.data(), static_cast<int32_t>(indices.size()), 0.f, BlendMode::Opaque);
            }
            r.endPass();

            // Copy the light's area of the scratch target into the map. Both
            // share the camera transform, so uv = the corner's clip position.
            Vec2 uv[4];
            const Rect& b = l.bounds;
            const Vec2 corners[4] = {{b.x, b.y}, {b.x + b.w, b.y}, {b.x + b.w, b.y + b.h}, {b.x, b.y + b.h}};
            for (int k = 0; k < 4; ++k) {
                float cx, cy;
                view.viewProj.transformPoint(corners[k].x, corners[k].y, &cx, &cy);
                uv[k] = {(cx + 1.f) * 0.5f, (1.f - cy) * 0.5f};
            }
            r.beginPass(t.lightMap, world);
            drawQuad(r, t.scratch, b, uv, BlendMode::Additive, {});
            r.endPass();
        }

        r.beginPass(view.target, {.viewportPx = view.viewportPx});
        r.drawFullscreen(t.lightMap, BlendMode::Multiply);
        r.endPass();
    }
};

} // namespace

void LightingPlugin::settings(App& app) {
    app.settings<LightingSettings>("lighting");
}

void LightingPlugin::build(App& app) {
    World& world = app.world();
    world.registerComponent<PointLight2D>("PointLight2D");
    world.registerComponent<SpotLight2D>("SpotLight2D");
    world.registerComponent<LightOccluder2D>("LightOccluder2D");
    app.initResource<LightingState>();
    app.addSystem<lighting_collect>("lighting_collect", Phase::Render);
}

void LightingPlugin::finish(App& app) {
    auto* renderer = app.getResource<RendererResource>();
    auto* settings = app.getResource<LightingSettings>();
    auto* state = app.getResource<LightingState>();
    if (!renderer || !settings || !state) return;

    auto lr = std::make_shared<LightRenderer>();
    ShaderDesc desc;
    desc.wgsl = lighting::shaders::light_wgsl;
    desc.glslFragment = lighting::shaders::light_frag_glsl;
    lr->shader = renderer->createShader(desc);
    if (!lr->shader.valid()) return;

    renderer->addViewHook(0, [lr, settings, state](RendererResource& r, const ViewInfo& view) {
        lr->render(r, view, *settings, *state);
    });
}

} // namespace drift
