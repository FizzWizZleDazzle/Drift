#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/CameraController.hpp>
#include <drift/resources/Time.hpp>

#include <cmath>

namespace drift {

#ifndef SWIG
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

        transform.position.x += ox * shake.intensity;
        transform.position.y += oy * shake.intensity;

        // Decay
        shake.intensity *= std::exp(-shake.decay * dt);
    });
}
#endif

class CameraPlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        app.addSystem<cameraFollowSystem>("camera_follow", Phase::PostUpdate);
        app.addSystem<cameraShakeSystem>("camera_shake", Phase::PostUpdate);
#endif
    }
    DRIFT_PLUGIN(CameraPlugin)
};

} // namespace drift
