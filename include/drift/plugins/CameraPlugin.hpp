#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
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
    }
    DRIFT_PLUGIN(CameraPlugin)
};

} // namespace drift
