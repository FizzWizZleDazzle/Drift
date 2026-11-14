#include <drift/DebugViews.hpp>
#include <drift/App.hpp>
#include <drift/resources/RendererResource.hpp>

#include <cmath>

namespace drift {

void debugBox(Vec2 pos, float rotation, Vec2 scale, Vec2 size, Vec2 origin, Vec2 out[4]) {
    Vec2 corners[4] = {{0.f, 0.f}, {size.x, 0.f}, {size.x, size.y}, {0.f, size.y}};
    float c = std::cos(rotation), s = std::sin(rotation);
    for (int i = 0; i < 4; ++i) {
        Vec2 l = {(corners[i].x - origin.x) * scale.x, (corners[i].y - origin.y) * scale.y};
        out[i] = {pos.x + l.x * c - l.y * s, pos.y + l.x * s + l.y * c};
    }
}

void DebugViews::add(const std::string& name, const std::string& description, Draw draw) {
    for (View& v : views_) {
        if (v.name == name) {
            if (v.description.empty()) v.description = description;
            v.draws.push_back(std::move(draw));
            return;
        }
    }
    views_.push_back({name, description, {std::move(draw)}, false});
}

bool DebugViews::set(const std::string& name, bool on) {
    for (View& v : views_) {
        if (v.name == name) {
            v.on = on;
            return true;
        }
    }
    return false;
}

bool DebugViews::enabled(const std::string& name) const {
    for (const View& v : views_) {
        if (v.name == name) return v.on;
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> DebugViews::list() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const View& v : views_) out.emplace_back(v.name, v.description);
    return out;
}

void DebugViews::addOutlines(OutlineSource source) {
    outlines_.push_back(std::move(source));
}

void DebugViews::forEachOutline(App& app, const OutlineSink& sink) const {
    for (const auto& source : outlines_) source(app, sink);
}

namespace {

bool inside(const Vec2 (&q)[4], Vec2 p) {
    // Inside a convex quad (either winding): p is on the same side of every edge.
    int pos = 0, neg = 0;
    for (int i = 0; i < 4; ++i) {
        Vec2 a = q[i], b = q[(i + 1) % 4];
        float cr = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (cr > 0.f) ++pos;
        else if (cr < 0.f) ++neg;
    }
    return pos == 0 || neg == 0;
}

} // namespace

EntityId DebugViews::pick(App& app, Vec2 screenPos) const {
    auto* renderer = app.getResource<RendererResource>();
    if (!renderer) return InvalidEntityId;
    Vec2 world = renderer->screenToWorld(screenPos);
    EntityId best = InvalidEntityId;
    uint8_t bestLayer = 0;
    float bestZ = 0.f;
    forEachOutline(app, [&](const DebugOutline& o) {
        if (!inside(o.corners, o.layer == ScreenLayer ? screenPos : world)) return;
        if (best == InvalidEntityId || o.layer > bestLayer || (o.layer == bestLayer && o.z >= bestZ)) {
            best = o.entity;
            bestLayer = o.layer;
            bestZ = o.z;
        }
    });
    return best;
}

void DebugViews::draw(App& app, RendererResource& renderer) {
    uint8_t before = renderer.layer();
    for (View& v : views_) {
        if (!v.on) continue;
        for (const Draw& d : v.draws) {
            renderer.setLayer(OverlayLayer);
            d(app, renderer);
        }
    }
    renderer.setLayer(before);
}

} // namespace drift
