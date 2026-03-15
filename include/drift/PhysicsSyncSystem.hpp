#pragma once

#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/Events.hpp>
#include <drift/components/Physics.hpp>
#include <drift/resources/PhysicsResource.hpp>
#include <drift/resources/Time.hpp>

#include <unordered_map>
#include <cstring>

namespace drift {

// Bidirectional mapping between ECS entities and Box2D bodies.
class PhysicsBridge : public Resource {
public:
    ~PhysicsBridge() override {
        // Don't destroy bodies here — PhysicsResource owns the Box2D world
        // and may already be destroyed. Just clear our maps.
        entityToBody.clear();
        shapeToEntity.clear();
    }

    std::unordered_map<EntityId, PhysicsBody> entityToBody;
    // shape uint64 (packed b2ShapeId) -> EntityId
    std::unordered_map<uint64_t, EntityId> shapeToEntity;

    bool hasBody(EntityId e) const { return entityToBody.count(e) > 0; }
};

// Convert a ContactEvent shape field back to a PhysicsShape for querying user data.
static inline PhysicsShape u64_to_shape(uint64_t v) {
    PhysicsShape shape;
    std::memcpy(&shape, &v, sizeof(shape));
    return shape;
}

static inline uint64_t shape_to_u64(PhysicsShape shape) {
    uint64_t v;
    std::memcpy(&v, &shape, sizeof(v));
    return v;
}

// System 1: Create/destroy Box2D bodies to match ECS entities with collider components.
inline void physics_sync_to_box2d(App& app) {
    auto* physics = app.getResource<PhysicsResource>();
    auto* bridge = app.getResource<PhysicsBridge>();
    if (!physics || !bridge) return;

    auto& world = app.world();
    auto& registry = world.componentRegistry();

    // Track which entities still have bodies this frame
    std::unordered_map<EntityId, bool> seen;

    // Helper: read RigidBody2D data for an entity (if present)
    auto readRigidBody = [&](EntityId entity, BodyDef& bdef) {
        if (registry.has<RigidBody2D>() && world.hasComponent(entity, registry.get<RigidBody2D>())) {
            auto* rb = world.get<RigidBody2D>(entity, registry.get<RigidBody2D>());
            if (rb) {
                bdef.type = rb->type;
                bdef.fixedRotation = rb->fixedRotation;
                bdef.gravityScale = rb->gravityScale;
                bdef.linearDamping = rb->linearDamping;
                bdef.angularDamping = rb->angularDamping;
            }
        }
    };

    // Process entities with BoxCollider2D
    if (registry.has<BoxCollider2D>()) {
        QueryMut<Transform2D, BoxCollider2D> boxQuery(world, registry);
        boxQuery.iterWithEntity([&](EntityId entity, Transform2D& t, BoxCollider2D& collider) {
            seen[entity] = true;
            if (bridge->hasBody(entity)) return; // already synced

            BodyDef bdef;
            bdef.type = BodyType::Static;
            // Body position = entity position + collider offset (so box is centered on visual)
            bdef.position = t.position + collider.offset;
            bdef.rotation = t.rotation;
            readRigidBody(entity, bdef);

            PhysicsBody body = physics->createBody(bdef);
            bridge->entityToBody[entity] = body;

            // Set initial velocity if present
            if (registry.has<Velocity2D>() && world.hasComponent(entity, registry.get<Velocity2D>())) {
                auto* vel = world.get<Velocity2D>(entity, registry.get<Velocity2D>());
                if (vel) physics->setBodyVelocity(body, vel->linear);
            }

            ShapeDef sdef;
            sdef.density = collider.density;
            sdef.friction = collider.friction;
            sdef.restitution = collider.restitution;
            sdef.isSensor = collider.isSensor;
            sdef.filter = collider.filter;

            PhysicsShape shape = physics->addBox(body, collider.halfSize.x, collider.halfSize.y, sdef);
            uint64_t shapeKey = shape_to_u64(shape);
            bridge->shapeToEntity[shapeKey] = entity;

            // Store entity ID as shape user data for fast contact lookup
            physics->setShapeUserData(shape, reinterpret_cast<void*>(static_cast<uintptr_t>(entity)));
        });
    }

    // Process entities with CircleCollider2D
    if (registry.has<CircleCollider2D>()) {
        QueryMut<Transform2D, CircleCollider2D> circleQuery(world, registry);
        circleQuery.iterWithEntity([&](EntityId entity, Transform2D& t, CircleCollider2D& collider) {
            seen[entity] = true;
            if (bridge->hasBody(entity)) return;

            BodyDef bdef;
            bdef.type = BodyType::Static;
            bdef.position = t.position;
            bdef.rotation = t.rotation;
            readRigidBody(entity, bdef);

            PhysicsBody body = physics->createBody(bdef);
            bridge->entityToBody[entity] = body;

            if (registry.has<Velocity2D>() && world.hasComponent(entity, registry.get<Velocity2D>())) {
                auto* vel = world.get<Velocity2D>(entity, registry.get<Velocity2D>());
                if (vel) physics->setBodyVelocity(body, vel->linear);
            }

            ShapeDef sdef;
            sdef.density = collider.density;
            sdef.friction = collider.friction;
            sdef.restitution = collider.restitution;
            sdef.isSensor = collider.isSensor;
            sdef.filter = collider.filter;

            PhysicsShape shape = physics->addCircle(body, collider.offset, collider.radius, sdef);
            uint64_t shapeKey = shape_to_u64(shape);
            bridge->shapeToEntity[shapeKey] = entity;

            physics->setShapeUserData(shape, reinterpret_cast<void*>(static_cast<uintptr_t>(entity)));
        });
    }

    // Clean up bridge entries for entities that no longer have colliders or are dead.
    // Destroy their Box2D bodies to avoid stale collision events.
    std::vector<EntityId> toRemove;
    for (auto& [entity, body] : bridge->entityToBody) {
        if (!seen.count(entity) || !world.isAlive(entity)) {
            physics->destroyBody(body);
            toRemove.push_back(entity);
        }
    }
    for (auto e : toRemove) {
        bridge->entityToBody.erase(e);
        for (auto sit = bridge->shapeToEntity.begin(); sit != bridge->shapeToEntity.end(); ) {
            if (sit->second == e) {
                sit = bridge->shapeToEntity.erase(sit);
            } else {
                ++sit;
            }
        }
    }
}

// System 2: Sync ECS Transform2D/Velocity2D -> Box2D before step (for kinematic/user-driven bodies)
inline void physics_sync_ecs_to_box2d(App& app) {
    auto* physics = app.getResource<PhysicsResource>();
    auto* bridge = app.getResource<PhysicsBridge>();
    if (!physics || !bridge) return;

    auto& world = app.world();
    auto& registry = world.componentRegistry();

    // Sync velocity from ECS to Box2D for dynamic bodies
    if (registry.has<Velocity2D>()) {
        for (auto& [entity, body] : bridge->entityToBody) {
            if (!world.isAlive(entity)) continue;
            if (registry.has<Velocity2D>() && world.hasComponent(entity, registry.get<Velocity2D>())) {
                auto* vel = world.get<Velocity2D>(entity, registry.get<Velocity2D>());
                if (vel) {
                    physics->setBodyVelocity(body, vel->linear);
                }
            }
        }
    }

    // Sync body positions from ECS to Box2D for non-physics-driven entities.
    // Entities with Velocity2D are driven by Box2D — don't teleport them every frame,
    // as b2Body_SetTransform resets center0=center which disrupts velocity integration.
    ComponentId transformComp = registry.get<Transform2D>();
    bool hasVelocity = registry.has<Velocity2D>();
    ComponentId velocity2dComp = hasVelocity ? registry.get<Velocity2D>() : 0;

    for (auto& [entity, body] : bridge->entityToBody) {
        if (!world.isAlive(entity)) continue;

        // Skip position sync for physics-driven bodies (Velocity2D present)
        if (hasVelocity && world.hasComponent(entity, velocity2dComp)) continue;

        auto* t = world.get<Transform2D>(entity, transformComp);
        if (!t) continue;

        Vec2 offset = Vec2::zero();
        if (registry.has<BoxCollider2D>() && world.hasComponent(entity, registry.get<BoxCollider2D>())) {
            auto* box = world.get<BoxCollider2D>(entity, registry.get<BoxCollider2D>());
            if (box) offset = box->offset;
        }
        physics->setBodyPosition(body, t->position + offset);
        physics->setBodyRotation(body, t->rotation);
    }
}

// System 3: Sync Box2D positions/velocities back to ECS components after step.
// Only syncs dynamic bodies that have Velocity2D (indicating physics-driven movement).
// Bodies without Velocity2D are assumed to be ECS-driven (game code controls position).
inline void physics_sync_from_box2d(App& app) {
    auto* physics = app.getResource<PhysicsResource>();
    auto* bridge = app.getResource<PhysicsBridge>();
    if (!physics || !bridge) return;

    auto& world = app.world();
    auto& registry = world.componentRegistry();

    if (!registry.has<Velocity2D>()) return;

    ComponentId transform2dComp = registry.get<Transform2D>();
    ComponentId velocity2dComp = registry.get<Velocity2D>();

    for (auto& [entity, body] : bridge->entityToBody) {
        if (!world.isAlive(entity)) continue;

        // Only sync back dynamic bodies that have Velocity2D (physics-driven)
        if (!world.hasComponent(entity, velocity2dComp)) continue;

        bool isDynamic = true;
        if (registry.has<RigidBody2D>() && world.hasComponent(entity, registry.get<RigidBody2D>())) {
            auto* rb = world.get<RigidBody2D>(entity, registry.get<RigidBody2D>());
            if (rb && rb->type != BodyType::Dynamic) isDynamic = false;
        }

        if (isDynamic) {
            Vec2 pos = physics->getBodyPosition(body);
            float rot = physics->getBodyRotation(body);

            // Subtract collider offset to get entity position
            Vec2 offset = Vec2::zero();
            if (registry.has<BoxCollider2D>() && world.hasComponent(entity, registry.get<BoxCollider2D>())) {
                auto* box = world.get<BoxCollider2D>(entity, registry.get<BoxCollider2D>());
                if (box) offset = box->offset;
            }

            auto* t = world.getMut<Transform2D>(entity, transform2dComp);
            if (t) {
                t->position = pos - offset;
                t->rotation = rot;
            }

            Vec2 vel = physics->getBodyVelocity(body);
            auto* v = world.getMut<Velocity2D>(entity, velocity2dComp);
            if (v) v->linear = vel;
        }
    }
}

// System 4: Translate Box2D contact/sensor events into typed collision events
inline void physics_emit_events(App& app) {
    auto* physics = app.getResource<PhysicsResource>();
    auto* bridge = app.getResource<PhysicsBridge>();
    if (!physics || !bridge) return;

    auto* collisionStartEvents = app.getResource<Events<CollisionStart>>();
    auto* collisionEndEvents = app.getResource<Events<CollisionEnd>>();
    auto* sensorStartEvents = app.getResource<Events<SensorStart>>();
    auto* sensorEndEvents = app.getResource<Events<SensorEnd>>();

    auto lookupEntity = [&](uint64_t shapeU64) -> EntityId {
        PhysicsShape shape = u64_to_shape(shapeU64);
        void* userData = physics->getShapeUserData(shape);
        if (userData) {
            return static_cast<EntityId>(reinterpret_cast<uintptr_t>(userData));
        }
        // Fallback: check bridge map
        auto it = bridge->shapeToEntity.find(shapeU64);
        return it != bridge->shapeToEntity.end() ? it->second : InvalidEntityId;
    };

    // Contact begin events
    if (collisionStartEvents) {
        for (auto& ce : physics->contactBeginEvents()) {
            EntityId a = lookupEntity(ce.shapeA);
            EntityId b = lookupEntity(ce.shapeB);
            if (a != InvalidEntityId && b != InvalidEntityId) {
                collisionStartEvents->send(CollisionStart{a, b});
            }
        }
    }

    // Contact end events
    if (collisionEndEvents) {
        for (auto& ce : physics->contactEndEvents()) {
            EntityId a = lookupEntity(ce.shapeA);
            EntityId b = lookupEntity(ce.shapeB);
            if (a != InvalidEntityId && b != InvalidEntityId) {
                collisionEndEvents->send(CollisionEnd{a, b});
            }
        }
    }

    // Sensor begin events
    if (sensorStartEvents) {
        for (auto& ce : physics->sensorBeginEvents()) {
            EntityId sensor = lookupEntity(ce.shapeA);
            EntityId visitor = lookupEntity(ce.shapeB);
            if (sensor != InvalidEntityId && visitor != InvalidEntityId) {
                sensorStartEvents->send(SensorStart{sensor, visitor});
            }
        }
    }

    // Sensor end events
    if (sensorEndEvents) {
        for (auto& ce : physics->sensorEndEvents()) {
            EntityId sensor = lookupEntity(ce.shapeA);
            EntityId visitor = lookupEntity(ce.shapeB);
            if (sensor != InvalidEntityId && visitor != InvalidEntityId) {
                sensorEndEvents->send(SensorEnd{sensor, visitor});
            }
        }
    }
}

// Simple movement system for entities with Velocity2D + Transform2D but WITHOUT RigidBody2D
inline void simple_movement(App& app) {
    auto* time = app.getResource<Time>();
    if (!time) return;

    auto& world = app.world();
    auto& registry = world.componentRegistry();

    if (!registry.has<Velocity2D>()) return;

    // Only apply to entities without RigidBody2D (physics handles those)
    if (registry.has<RigidBody2D>()) {
        QueryMut<Transform2D, Velocity2D, Without<RigidBody2D>> movers(world, registry);
        movers.iter([dt = time->delta](Transform2D& t, Velocity2D& v) {
            t.position += v.linear * dt;
            t.rotation += v.angular * dt;
        });
    } else {
        // No RigidBody2D registered at all, so all Velocity2D entities are simple movers
        QueryMut<Transform2D, Velocity2D> movers(world, registry);
        movers.iter([dt = time->delta](Transform2D& t, Velocity2D& v) {
            t.position += v.linear * dt;
            t.rotation += v.angular * dt;
        });
    }
}

} // namespace drift
