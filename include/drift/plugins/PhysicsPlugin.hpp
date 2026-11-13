#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/Events.hpp>
#include <drift/Reflect.hpp>
#include <drift/PhysicsSyncSystem.hpp>
#include <drift/CollisionBridge.hpp>
#include <drift/resources/PhysicsResource.hpp>
#include <drift/resources/RendererResource.hpp>
#ifdef DRIFT_DEV
#include <drift/resources/UIResource.hpp>
#include "imgui.h"
#endif
#include <drift/resources/Time.hpp>
#include <drift/components/Physics.hpp>

namespace drift {

inline void physics_step(ResMut<PhysicsResource> physics, Res<Time> time) { physics->step(time->delta); }

inline void physics_debug_draw(Res<PhysicsResource> physics, ResMut<RendererResource> renderer) {
    if (physics->debugDraw().enabled) physics->drawDebug(*renderer);
}

// The [physics] section of drift.toml: the world's starting settings.
// Change them while running through PhysicsResource.
struct PhysicsSettings : public Resource {
    DRIFT_RESOURCE(PhysicsSettings)

    Vec2 gravity = {0.f, 9.8f};

    static void describe(reflect::TypeBuilder<PhysicsSettings>& t) {
        t.field("gravity", &PhysicsSettings::gravity);
    }
};

class PhysicsPlugin : public Plugin {
public:
    void build(App& app) override {
        auto& settings = app.settings<PhysicsSettings>("physics");
        auto* physicsRes = app.addResource<PhysicsResource>(settings.gravity);
        app.onSettingsChanged("physics", [physicsRes, &settings](const std::vector<std::string>&) {
            physicsRes->setGravity(settings.gravity);
        });

        // Register physics components
        auto& world = app.world();
        reflect::enumType<BodyType>("BodyType", {
            {"Static", BodyType::Static}, {"Dynamic", BodyType::Dynamic},
            {"Kinematic", BodyType::Kinematic}});
        reflect::type<CollisionFilter>("CollisionFilter")
            .field("category", &CollisionFilter::category)
            .field("mask", &CollisionFilter::mask);
        reflect::component<RigidBody2D>(world, "RigidBody2D")
            .field("type", &RigidBody2D::type)
            .field("fixedRotation", &RigidBody2D::fixedRotation)
            .field("gravityScale", &RigidBody2D::gravityScale)
            .field("linearDamping", &RigidBody2D::linearDamping)
            .field("angularDamping", &RigidBody2D::angularDamping);
        reflect::component<BoxCollider2D>(world, "BoxCollider2D")
            .field("halfSize", &BoxCollider2D::halfSize)
            .field("offset", &BoxCollider2D::offset)
            .field("isSensor", &BoxCollider2D::isSensor)
            .field("density", &BoxCollider2D::density)
            .field("friction", &BoxCollider2D::friction)
            .field("restitution", &BoxCollider2D::restitution)
            .field("filter", &BoxCollider2D::filter);
        reflect::component<CircleCollider2D>(world, "CircleCollider2D")
            .field("radius", &CircleCollider2D::radius)
            .field("offset", &CircleCollider2D::offset)
            .field("isSensor", &CircleCollider2D::isSensor)
            .field("density", &CircleCollider2D::density)
            .field("friction", &CircleCollider2D::friction)
            .field("restitution", &CircleCollider2D::restitution)
            .field("filter", &CircleCollider2D::filter);
        reflect::enumType<JointType>("JointType", {
            {"Distance", JointType::Distance}, {"Revolute", JointType::Revolute},
            {"Prismatic", JointType::Prismatic}, {"Weld", JointType::Weld}});
        reflect::component<Joint2D>(world, "Joint2D")
            .field("type", &Joint2D::type)
            .entity("target", &Joint2D::target)
            .field("anchor", &Joint2D::anchor)
            .field("targetAnchor", &Joint2D::targetAnchor)
            .field("length", &Joint2D::length)
            .field("minLength", &Joint2D::minLength)
            .field("maxLength", &Joint2D::maxLength)
            .field("stiffness", &Joint2D::stiffness)
            .field("damping", &Joint2D::damping)
            .field("limit", &Joint2D::limit)
            .field("lower", &Joint2D::lower)
            .field("upper", &Joint2D::upper)
            .field("motor", &Joint2D::motor)
            .field("motorSpeed", &Joint2D::motorSpeed)
            .field("maxMotorForce", &Joint2D::maxMotorForce)
            .field("axis", &Joint2D::axis)
            .field("collideConnected", &Joint2D::collideConnected);
        reflect::component<Velocity2D>(world, "Velocity2D")
            .field("linear", &Velocity2D::linear)
            .field("angular", &Velocity2D::angular);

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
        app.addSystem<physics_debug_draw>("physics_debug_draw", Phase::Render);

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
                    cb->addCollisionStart(ea, eb, ce.point, ce.normal);
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
    }
#ifdef DRIFT_DEV
    void finish(App& app) override {
        auto* ui = app.getResource<UIResource>();
        if (!ui) return;
        ui->addDevPanel("Physics", [](App& a) {
            auto* physics = a.getResource<PhysicsResource>();
            if (!physics) return;
            PhysicsDebugDraw dd = physics->debugDraw();
            bool changed = ImGui::Checkbox("Debug draw", &dd.enabled);
            ImGui::BeginDisabled(!dd.enabled);
            changed |= ImGui::Checkbox("Shapes", &dd.shapes);
            changed |= ImGui::Checkbox("AABBs", &dd.aabbs);
            changed |= ImGui::Checkbox("Contacts", &dd.contacts);
            changed |= ImGui::Checkbox("Joints", &dd.joints);
            ImGui::EndDisabled();
            if (changed) physics->setDebugDraw(dd);
            ImGui::Text("Bodies: %d", physics->bodyCount());
        });
    }
#endif

    DRIFT_PLUGIN(PhysicsPlugin)
};

} // namespace drift
