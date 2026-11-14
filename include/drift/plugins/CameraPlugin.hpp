#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/DebugViews.hpp>
#include <drift/Query.hpp>
#include <drift/Reflect.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/CameraController.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>

#include <algorithm>
#include <cmath>

namespace drift {

inline void cameraFollowSystem(Res<Time> time,
                                QueryMut<Transform2D, CameraFollow, Camera> cameras,
                                Query<Transform2D> targets) {
    float dt = time->delta;

    cameras.iter([&](Transform2D& camTransform, CameraFollow& follow, Camera&) {
        if (follow.target == InvalidEntityId) return;

        // Look up target position
        Vec2 targetPos = {};
        bool found = false;
        targets.iterWithEntity([&](EntityId id, const Transform2D& t) {
            if (id == follow.target) {
                targetPos = t.position;
                found = true;
            }
        });
        if (!found) return;

        Vec2 desired = {targetPos.x + follow.offset.x, targetPos.y + follow.offset.y};

        // Dead zone check
        float dx = desired.x - camTransform.position.x;
        float dy = desired.y - camTransform.position.y;
        if (std::abs(dx) < follow.deadZone.x && std::abs(dy) < follow.deadZone.y) {
            return;
        }

        // Smooth lerp
        float t_val = 1.f - std::exp(-follow.smoothing * dt);
        camTransform.position.x += dx * t_val;
        camTransform.position.y += dy * t_val;
    });
}

// The half size of the world rect a camera shows: its target (or the
// logical screen) times its viewport, over its zoom.
inline Vec2 cameraHalfView(const RendererResource& renderer, const Camera& cam) {
    Vec2 size = cam.target.valid() ? renderer.textureSize(cam.target)
                                   : Vec2{renderer.logicalWidth(), renderer.logicalHeight()};
    float zoom = cam.zoom > 0.f ? cam.zoom : 1.f;
    return {size.x * cam.viewport.w * 0.5f / zoom, size.y * cam.viewport.h * 0.5f / zoom};
}

inline void cameraBoundsSystem(Res<RendererResource> renderer,
                               QueryMut<Transform2D, CameraBounds, Camera> cameras) {
    cameras.iter([&](Transform2D& t, CameraBounds& bounds, Camera& cam) {
        const Rect& a = bounds.area;
        if (a.w <= 0.f || a.h <= 0.f) return;
        Vec2 half = cameraHalfView(*renderer, cam);
        auto hold = [](float p, float lo, float size, float h) {
            if (size <= h * 2.f) return lo + size * 0.5f;
            return std::clamp(p, lo + h, lo + size - h);
        };
        t.position.x = hold(t.position.x, a.x, a.w, half.x);
        t.position.y = hold(t.position.y, a.y, a.h, half.y);
    });
}

// Takes back the shake offset applied last frame before anything reads or
// moves the camera, so a shake never leaves the camera displaced.
inline void cameraUnshakeSystem(QueryMut<Transform2D, CameraShake, Camera> cameras) {
    cameras.iter([&](Transform2D& transform, CameraShake& shake, Camera&) {
        transform.position.x -= shake.offset.x;
        transform.position.y -= shake.offset.y;
        shake.offset = {};
    });
}

inline void cameraShakeSystem(Res<Time> time,
                               QueryMut<Transform2D, CameraShake, Camera> cameras) {
    float dt = time->delta;

    cameras.iter([&](Transform2D& transform, CameraShake& shake, Camera&) {
        if (shake.intensity <= 0.001f) {
            shake.intensity = 0.f;
            return;
        }

        shake.elapsed += dt;

        // Perlin-like shake using sin with different frequencies
        float ox = std::sin(shake.elapsed * shake.frequency * 1.0f) *
                   std::cos(shake.elapsed * shake.frequency * 0.7f);
        float oy = std::cos(shake.elapsed * shake.frequency * 1.3f) *
                   std::sin(shake.elapsed * shake.frequency * 0.9f);

        shake.offset = {ox * shake.intensity, oy * shake.intensity};
        transform.position.x += shake.offset.x;
        transform.position.y += shake.offset.y;

        // Decay
        shake.intensity *= std::exp(-shake.decay * dt);
    });
}

class CameraPlugin : public Plugin {
public:
    void build(App& app) override {
        reflect::component<CameraFollow>(app.world(), "CameraFollow")
            .entity("target", &CameraFollow::target)
            .field("smoothing", &CameraFollow::smoothing)
            .field("offset", &CameraFollow::offset)
            .field("deadZone", &CameraFollow::deadZone);
        reflect::component<CameraShake>(app.world(), "CameraShake")
            .field("intensity", &CameraShake::intensity)
            .field("decay", &CameraShake::decay)
            .field("frequency", &CameraShake::frequency)
            .field("elapsed", &CameraShake::elapsed)
            .field("offset", &CameraShake::offset);
        reflect::component<CameraBounds>(app.world(), "CameraBounds")
            .field("area", &CameraBounds::area);
        app.addSystem<cameraUnshakeSystem>("camera_unshake", Phase::PreUpdate);
        app.addSystem<cameraFollowSystem>("camera_follow", Phase::PostUpdate);
        app.addSystem<cameraBoundsSystem>("camera_bounds", Phase::PostUpdate).after("camera_follow");
        app.addSystem<cameraShakeSystem>("camera_shake", Phase::PostUpdate).after("camera_bounds");

        app.initResource<DebugViews>()->add(
            "camera", "Each camera's view, its CameraBounds area, follow dead zone and target",
            [](App& a, RendererResource& r) { drawCameraDebug(a, r); });
    }

    // The "camera" debug view: the view rect (cyan), the bounds area
    // (magenta), the follow dead zone (yellow) and a line to the target.
    static void drawCameraDebug(App& a, RendererResource& r) {
        World& w = a.world();
        const auto& reg = w.componentRegistry();
        Query<Transform2D> targets(w, reg);
        Query<Camera, Transform2D, Optional<CameraBounds>, Optional<CameraFollow>> q(w, reg);
        q.iter([&](const Camera& cam, const Transform2D& t, const CameraBounds* bounds, const CameraFollow* follow) {
            Vec2 half = cameraHalfView(r, cam);
            // Inset a little, so a camera's own view rect shows inside the screen edge.
            float in = 2.f / (cam.zoom > 0.f ? cam.zoom : 1.f);
            r.drawRect({t.position.x - half.x + in, t.position.y - half.y + in, half.x * 2.f - in * 2.f,
                        half.y * 2.f - in * 2.f},
                       Color{80, 230, 255, 255}, 3.f);
            r.drawLine({t.position.x - 10.f, t.position.y}, {t.position.x + 10.f, t.position.y},
                       Color{80, 230, 255, 255}, 2.f);
            r.drawLine({t.position.x, t.position.y - 10.f}, {t.position.x, t.position.y + 10.f},
                       Color{80, 230, 255, 255}, 2.f);
            if (bounds && bounds->area.w > 0.f && bounds->area.h > 0.f) {
                // Clip the area to near the view so a huge area still draws its edges.
                Rect a0 = bounds->area;
                float x0 = std::max(a0.x, t.position.x - half.x * 4.f), x1 = std::min(a0.x + a0.w, t.position.x + half.x * 4.f);
                float y0 = std::max(a0.y, t.position.y - half.y * 4.f), y1 = std::min(a0.y + a0.h, t.position.y + half.y * 4.f);
                Color m{255, 90, 230, 255};
                if (a0.x >= x0) r.drawLine({a0.x, y0}, {a0.x, y1}, m, 3.f);
                if (a0.x + a0.w <= x1) r.drawLine({a0.x + a0.w, y0}, {a0.x + a0.w, y1}, m, 3.f);
                if (a0.y >= y0) r.drawLine({x0, a0.y}, {x1, a0.y}, m, 3.f);
                if (a0.y + a0.h <= y1) r.drawLine({x0, a0.y + a0.h}, {x1, a0.y + a0.h}, m, 3.f);
            }
            if (follow) {
                Color y{255, 230, 60, 255};
                if (follow->deadZone.x > 0.f || follow->deadZone.y > 0.f) {
                    r.drawRect({t.position.x - follow->deadZone.x, t.position.y - follow->deadZone.y,
                                follow->deadZone.x * 2.f, follow->deadZone.y * 2.f}, y, 2.f);
                }
                targets.iterWithEntity([&](EntityId id, const Transform2D& tt) {
                    if (id != follow->target) return;
                    Vec2 goal = {tt.position.x + follow->offset.x, tt.position.y + follow->offset.y};
                    r.drawLine(t.position, goal, y, 2.f);
                    r.drawCircle(goal, 6.f, y, 2.f);
                });
            }
        });
    }
    DRIFT_PLUGIN(CameraPlugin)
};

} // namespace drift
