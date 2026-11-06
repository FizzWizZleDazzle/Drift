#pragma once

#include <drift/Plugin.hpp>
#include <drift/Reflect.hpp>
#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/components/GlobalTransform.hpp>
#include <drift/components/Shape.hpp>
#include <drift/resources/RendererResource.hpp>

#include <cmath>

namespace drift {

inline void drawShape(RendererResource& r, const Shape2D& s, Vec2 pos, float rot, Vec2 scale) {
    if (!s.visible) return;
    uint8_t before = r.layer();
    r.setLayer(s.layer);
    if (s.kind == ShapeKind::Circle) {
        float radius = s.radius * std::abs(scale.x);
        if (s.outline > 0.f) r.drawCircle(pos, radius, s.color, s.outline, 0, s.zOrder);
        else r.drawCircleFilled(pos, radius, s.color, 0, s.zOrder);
    } else {
        Vec2 size = {s.size.x * scale.x, s.size.y * scale.y};
        Vec2 a = {-s.anchor.x * size.x, -s.anchor.y * size.y};
        if (rot == 0.f && s.outline <= 0.f) {
            Rect rect = {pos.x + a.x, pos.y + a.y, size.x, size.y};
            if (s.cornerRadius > 0.f) r.drawRoundedRectFilled(rect, s.cornerRadius, s.color, s.zOrder);
            else r.drawRectFilled(rect, s.color, s.zOrder);
        } else {
            float c = std::cos(rot), sn = std::sin(rot);
            Vec2 local[4] = {a, {a.x + size.x, a.y}, {a.x + size.x, a.y + size.y}, {a.x, a.y + size.y}};
            Vec2 pts[4];
            for (int i = 0; i < 4; ++i) {
                pts[i] = {pos.x + local[i].x * c - local[i].y * sn, pos.y + local[i].x * sn + local[i].y * c};
            }
            if (s.outline > 0.f) r.drawPolyline(pts, 4, s.color, s.outline, true, s.zOrder);
            else r.drawPolygonFilled(pts, 4, s.color, s.zOrder);
        }
    }
    r.setLayer(before);
}

// Draws every Shape2D at its world transform (GlobalTransform2D in a
// hierarchy, Transform2D otherwise).
inline void shapeRender(ResMut<RendererResource> renderer,
                        Query<Shape2D, Transform2D, Optional<GlobalTransform2D>> shapes) {
    shapes.iter([&](const Shape2D& s, const Transform2D& t, const GlobalTransform2D* g) {
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
