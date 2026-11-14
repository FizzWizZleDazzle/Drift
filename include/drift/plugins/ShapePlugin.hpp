#pragma once

#include <drift/Plugin.hpp>
#include <drift/Reflect.hpp>
#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/components/GlobalTransform.hpp>
#include <drift/components/Shape.hpp>
#include <drift/resources/RendererResource.hpp>

#include <algorithm>
#include <cmath>

namespace drift {

// The outline of a shape in its own space, before rotation: an ellipse for
// a circle scaled unevenly, a rectangle with rounded corners.
inline int shapeOutline(const Shape2D& s, Vec2 scale, Vec2* out, int max) {
    const float pi = 3.14159265f;
    if (s.kind == ShapeKind::Circle) {
        float rx = s.radius * std::abs(scale.x), ry = s.radius * std::abs(scale.y);
        int n = std::min(max, 40);
        for (int i = 0; i < n; ++i) {
            float a = 2.f * pi * float(i) / float(n);
            out[i] = {std::cos(a) * rx, std::sin(a) * ry};
        }
        return n;
    }
    Vec2 size = {s.size.x * std::abs(scale.x), s.size.y * std::abs(scale.y)};
    Vec2 a = {-s.anchor.x * size.x, -s.anchor.y * size.y};
    if (scale.x < 0.f) a.x = -a.x - size.x;
    if (scale.y < 0.f) a.y = -a.y - size.y;
    float r = std::min({s.cornerRadius, size.x * 0.5f, size.y * 0.5f});
    if (r <= 0.f) {
        out[0] = a;
        out[1] = {a.x + size.x, a.y};
        out[2] = {a.x + size.x, a.y + size.y};
        out[3] = {a.x, a.y + size.y};
        return 4;
    }
    // Corner centers clockwise from the top-left, each with a quarter arc.
    Vec2 centers[4] = {{a.x + r, a.y + r}, {a.x + size.x - r, a.y + r},
                       {a.x + size.x - r, a.y + size.y - r}, {a.x + r, a.y + size.y - r}};
    const int per = std::min(8, max / 4 - 1);
    int n = 0;
    for (int c = 0; c < 4; ++c) {
        float start = pi + float(c) * pi * 0.5f;
        for (int i = 0; i <= per; ++i) {
            float t = start + (pi * 0.5f) * float(i) / float(per);
            out[n++] = {centers[c].x + std::cos(t) * r, centers[c].y + std::sin(t) * r};
        }
    }
    return n;
}

inline void drawShape(RendererResource& r, const Shape2D& s, Vec2 pos, float rot, Vec2 scale) {
    if (!s.visible) return;
    uint8_t before = r.layer();
    r.setLayer(s.layer);
    bool even = std::abs(std::abs(scale.x) - std::abs(scale.y)) < 1e-4f;
    if (s.kind == ShapeKind::Circle && even) {
        float radius = s.radius * std::abs(scale.x);
        if (s.outline > 0.f) r.drawCircle(pos, radius, s.color, s.outline, 0, s.zOrder);
        else r.drawCircleFilled(pos, radius, s.color, 0, s.zOrder);
    } else if (s.kind == ShapeKind::Rectangle && rot == 0.f && s.outline <= 0.f && scale.x > 0.f && scale.y > 0.f) {
        Vec2 size = {s.size.x * scale.x, s.size.y * scale.y};
        Rect rect = {pos.x - s.anchor.x * size.x, pos.y - s.anchor.y * size.y, size.x, size.y};
        if (s.cornerRadius > 0.f) r.drawRoundedRectFilled(rect, s.cornerRadius, s.color, s.zOrder);
        else r.drawRectFilled(rect, s.color, s.zOrder);
    } else {
        // Turned, mirrored, unevenly scaled or outlined: build the outline
        // and place it.
        Vec2 local[48];
        int n = shapeOutline(s, scale, local, 48);
        float c = std::cos(rot), sn = std::sin(rot);
        Vec2 pts[48];
        for (int i = 0; i < n; ++i) {
            pts[i] = {pos.x + local[i].x * c - local[i].y * sn, pos.y + local[i].x * sn + local[i].y * c};
        }
        if (s.outline > 0.f) r.drawPolyline(pts, n, s.color, s.outline, true, s.zOrder);
        else r.drawPolygonFilled(pts, n, s.color, s.zOrder);
    }
    r.setLayer(before);
}

// A box around everything a shape covers, however it turns.
inline Rect shapeReach(const Shape2D& s, Vec2 pos, Vec2 scale) {
    float sx = std::abs(scale.x), sy = std::abs(scale.y);
    float reach = s.kind == ShapeKind::Circle
        ? s.radius * std::max(sx, sy)
        : std::hypot(s.size.x * sx, s.size.y * sy) *
              std::max({1.f, std::abs(s.anchor.x), std::abs(s.anchor.y),
                        std::abs(1.f - s.anchor.x), std::abs(1.f - s.anchor.y)});
    reach += std::max(s.outline, 0.f);
    return {pos.x - reach, pos.y - reach, reach * 2.f, reach * 2.f};
}

// Draws every Shape2D some camera may show, at its world transform
// (GlobalTransform2D in a hierarchy, Transform2D otherwise).
inline void shapeRender(ResMut<RendererResource> renderer,
                        Query<Shape2D, Transform2D, Optional<GlobalTransform2D>> shapes) {
    shapes.iter([&](const Shape2D& s, const Transform2D& t, const GlobalTransform2D* g) {
        Vec2 pos = g ? g->position : t.position;
        Vec2 scale = g ? g->scale : t.scale;
        if (!s.visible || !renderer->visible(shapeReach(s, pos, scale), s.layer)) return;
        if (g) drawShape(*renderer, s, g->position, g->rotation, g->scale);
        else drawShape(*renderer, s, t.position, t.rotation, t.scale);
    });
}

class ShapePlugin : public Plugin {
public:
    void build(App& app) override {
        reflect::enumType<ShapeKind>("ShapeKind", {
            {"Rectangle", ShapeKind::Rectangle}, {"Circle", ShapeKind::Circle}});
        reflect::component<Shape2D>(app.world(), "Shape2D")
            .field("kind", &Shape2D::kind)
            .field("size", &Shape2D::size)
            .field("radius", &Shape2D::radius)
            .field("color", &Shape2D::color)
            .field("outline", &Shape2D::outline)
            .field("cornerRadius", &Shape2D::cornerRadius)
            .field("anchor", &Shape2D::anchor)
            .field("zOrder", &Shape2D::zOrder)
            .field("layer", &Shape2D::layer)
            .field("visible", &Shape2D::visible);
        app.addSystem<shapeRender>("shape_render", Phase::Render);
    }
    DRIFT_PLUGIN(ShapePlugin)
};

} // namespace drift
