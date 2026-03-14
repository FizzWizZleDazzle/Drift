#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/Events.hpp>
#include <drift/PhysicsSyncSystem.hpp>
#include <drift/CollisionBridge.hpp>
#include <drift/resources/PhysicsResource.hpp>
#include <drift/resources/Time.hpp>
#include <drift/components/Physics.hpp>

namespace drift {

#ifndef SWIG
inline void physics_step(ResMut<PhysicsResource> physics, Res<Time> time) { physics->step(time->delta); }
#endif

class PhysicsPlugin : public Plugin {
public:
    void build(App& app) override {
#ifndef SWIG
        app.addResource<PhysicsResource>(Vec2{0.f, 9.8f});

        // Register physics components
        auto& world = app.world();
        world.registerComponent<RigidBody2D>("RigidBody2D");
        world.registerComponent<BoxCollider2D>("BoxCollider2D");
        world.registerComponent<CircleCollider2D>("CircleCollider2D");
        world.registerComponent<Velocity2D>("Velocity2D");

        // Register collision event types
        app.initEvent<CollisionStart>();
        app.initEvent<CollisionEnd>();
        app.initEvent<SensorStart>();
        app.initEvent<SensorEnd>();

        // PhysicsBridge resource is registered lazily by PhysicsSyncSystem
        // but we add it here for clarity
        app.addResource<PhysicsBridge>();

        // Simple movement system (Update phase, before physics)
        app.addSystem("simple_movement", Phase::Update, [](App& a) {
            simple_movement(a);
        });

        // PostUpdate systems in order:
        // 1. Sync ECS -> Box2D
        app.addSystem("physics_sync_to_box2d", Phase::PostUpdate, [](App& a) {
            physics_sync_to_box2d(a);
        });

        // 2. Sync velocities/kinematic positions before step
        app.addSystem("physics_sync_ecs_to_box2d", Phase::PostUpdate, [](App& a) {
            physics_sync_ecs_to_box2d(a);
        });

        // 3. Physics step
        app.addSystem<physics_step>("physics_step", Phase::PostUpdate);

        // 4. Sync Box2D -> ECS
        app.addSystem("physics_sync_from_box2d", Phase::PostUpdate, [](App& a) {
            physics_sync_from_box2d(a);
        });

        // 5. Emit collision events
        app.addSystem("physics_emit_events", Phase::PostUpdate, [](App& a) {
            physics_emit_events(a);
        });

        // 6. Populate CollisionBridge directly from PhysicsResource contact events
        //    (same-frame data, no double-buffer delay)
        app.initResource<CollisionBridge>();
        app.addSystem("collision_bridge_sync", Phase::PostUpdate, [](App& a) {
            auto* cb = a.getResource<CollisionBridge>();
            auto* physics = a.getResource<PhysicsResource>();
            auto* bridge = a.getResource<PhysicsBridge>();
            if (!cb || !physics || !bridge) return;
            cb->clear();

            auto lookupEntity = [&](uint64_t shapeU64) -> EntityId {
                PhysicsShape shape = u64_to_shape(shapeU64);
                void* userData = physics->getShapeUserData(shape);
                if (userData) {
                    return static_cast<EntityId>(reinterpret_cast<uintptr_t>(userData));
                }
                auto it = bridge->shapeToEntity.find(shapeU64);
                return it != bridge->shapeToEntity.end() ? it->second : InvalidEntityId;
            };

            for (auto& ce : physics->contactBeginEvents()) {
                EntityId ea = lookupEntity(ce.shapeA);
                EntityId eb = lookupEntity(ce.shapeB);
                if (ea != InvalidEntityId && eb != InvalidEntityId)
                    cb->addCollisionStart(ea, eb);
            }
            for (auto& ce : physics->contactEndEvents()) {
                EntityId ea = lookupEntity(ce.shapeA);
                EntityId eb = lookupEntity(ce.shapeB);
                if (ea != InvalidEntityId && eb != InvalidEntityId)
                    cb->addCollisionEnd(ea, eb);
            }
            for (auto& ce : physics->sensorBeginEvents()) {
                EntityId ea = lookupEntity(ce.shapeA);
                EntityId eb = lookupEntity(ce.shapeB);
                if (ea != InvalidEntityId && eb != InvalidEntityId)
                    cb->addSensorStart(ea, eb);
            }
            for (auto& ce : physics->sensorEndEvents()) {
                EntityId ea = lookupEntity(ce.shapeA);
                EntityId eb = lookupEntity(ce.shapeB);
                if (ea != InvalidEntityId && eb != InvalidEntityId)
                    cb->addSensorEnd(ea, eb);
            }
        });
#endif
    }
    DRIFT_PLUGIN(PhysicsPlugin)
};

} // namespace drift
