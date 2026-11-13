#pragma once

#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/Events.hpp>
#include <drift/components/Physics.hpp>
#include <drift/resources/PhysicsResource.hpp>
#include <drift/resources/Time.hpp>

#include <unordered_map>
#include <cstdio>
#include <cstring>
#include <string>

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
    // The body and collider settings each body was made from; a body whose
    // settings change is made again.
    std::unordered_map<EntityId, std::string> bodySettings;
    // Joints made from Joint2D components, with the settings they were made
    // from and the bodies they join.
    struct JointEntry {
        PhysicsJoint joint{};
        std::string settings;
    };
    std::unordered_map<EntityId, JointEntry> joints;
    // The velocity each body had when it was last copied into Velocity2D.
    // Only a Velocity2D the game changed since then is written to the body,
    // so impulses and forces applied to the body directly are kept.
    std::unordered_map<EntityId, Velocity2D> syncedVelocity;
    // A static body at the origin that joints without a target pin to.
    PhysicsBody ground{};
    bool hasGround = false;

    bool hasBody(EntityId e) const { return entityToBody.count(e) > 0; }

    // Forgets an entity's body after it was destroyed.
    void forget(EntityId e) {
        entityToBody.erase(e);
        bodySettings.erase(e);
        syncedVelocity.erase(e);
        for (auto it = shapeToEntity.begin(); it != shapeToEntity.end();) {
            if (it->second == e) it = shapeToEntity.erase(it);
            else ++it;
        }
    }
};

// The settings a body is made from, as text, to notice when they change.
inline std::string physicsBodySettings(const RigidBody2D* rb, const char* shape, Vec2 size, Vec2 offset,
                                       float density, float friction, float restitution, bool sensor,
                                       CollisionFilter filter) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%d %d %g %g %g|%s %g %g %g %g %g %g %g %d %u %u",
                  rb ? int(rb->type) : -1, rb ? int(rb->fixedRotation) : 0, rb ? rb->gravityScale : 1.f,
                  rb ? rb->linearDamping : 0.f, rb ? rb->angularDamping : 0.f, shape, size.x, size.y, offset.x,
                  offset.y, density, friction, restitution, int(sensor), filter.category, filter.mask);
    return buf;
}

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

// Makes, remakes and removes the joints of Joint2D components. A joint waits
// until both of its bodies exist.
inline void physics_sync_joints(App& app, PhysicsResource& physics, PhysicsBridge& bridge) {
    auto& world = app.world();
    auto& registry = world.componentRegistry();
    std::unordered_map<EntityId, bool> wanted;
    if (registry.has<Joint2D>()) {
        ComponentId jointId = registry.get<Joint2D>();
        for (EntityId e : world.entitiesWith(jointId)) {
            const auto* j = world.get<Joint2D>(e, jointId);
            auto self = bridge.entityToBody.find(e);
            if (!j || self == bridge.entityToBody.end()) continue;
            PhysicsBody other{};
            if (j->target != InvalidEntityId) {
                auto it = bridge.entityToBody.find(j->target);
                if (it == bridge.entityToBody.end()) continue;
                other = it->second;
            } else {
                if (!bridge.hasGround || !physics.bodyValid(bridge.ground)) {
                    BodyDef g;
                    g.type = BodyType::Static;
                    bridge.ground = physics.createBody(g);
                    bridge.hasGround = true;
                }
                other = bridge.ground;
            }
            wanted[e] = true;
            // Bodies are made again when their settings change, which
            // destroys their joints; the body ids are part of the key.
            char buf[320];
            std::snprintf(buf, sizeof(buf), "%d %llu %d:%d %d:%d %g %g %g %g %g %g %g %g %g %d %g %g %d %g %g %g %g %d",
                          int(j->type), (unsigned long long)j->target, self->second.index, self->second.revision,
                          other.index, other.revision, j->anchor.x, j->anchor.y, j->targetAnchor.x,
                          j->targetAnchor.y, j->length, j->minLength, j->maxLength, j->stiffness, j->damping,
                          int(j->limit), j->lower, j->upper, int(j->motor), j->motorSpeed, j->maxMotorForce,
                          j->axis.x, j->axis.y, int(j->collideConnected));
            auto existing = bridge.joints.find(e);
            if (existing != bridge.joints.end()) {
                if (existing->second.settings == buf && physics.jointValid(existing->second.joint)) continue;
                physics.destroyJoint(existing->second.joint);
                bridge.joints.erase(existing);
            }
            JointDef def;
            def.type = j->type;
            def.bodyA = other;
            def.bodyB = self->second;
            def.anchorA = j->targetAnchor;
            def.anchorB = j->anchor;
            def.length = j->length;
            if (def.length <= 0.f) {
                // The distance between the anchors as the bodies are now.
                Vec2 pa = physics.getBodyPosition(other) + j->targetAnchor;
                Vec2 pb = physics.getBodyPosition(self->second) + j->anchor;
                def.length = (pb - pa).length();
            }
            def.minLength = j->minLength;
            def.maxLength = j->maxLength;
            def.stiffness = j->stiffness;
            def.damping = j->damping;
            def.limit = j->limit;
            def.lower = j->lower;
            def.upper = j->upper;
            def.motor = j->motor;
            def.motorSpeed = j->motorSpeed;
            def.maxMotorForce = j->maxMotorForce;
            def.axis = j->axis;
            def.collideConnected = j->collideConnected;
            bridge.joints[e] = {physics.createJoint(def), buf};
        }
    }
    for (auto it = bridge.joints.begin(); it != bridge.joints.end();) {
        if (!wanted.count(it->first)) {
            physics.destroyJoint(it->second.joint);
            it = bridge.joints.erase(it);
        } else {
            ++it;
        }
    }
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

    auto rigidBodyOf = [&](EntityId entity) -> const RigidBody2D* {
        if (!registry.has<RigidBody2D>() || !world.hasComponent(entity, registry.get<RigidBody2D>())) return nullptr;
        return world.get<RigidBody2D>(entity, registry.get<RigidBody2D>());
    };

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
            std::string settings = physicsBodySettings(rigidBodyOf(entity), "box", collider.halfSize, collider.offset,
                                                       collider.density, collider.friction, collider.restitution,
                                                       collider.isSensor, collider.filter);
            if (bridge->hasBody(entity)) {
                if (bridge->bodySettings[entity] == settings) return; // already synced
                physics->destroyBody(bridge->entityToBody[entity]);   // settings changed: make it again
                bridge->forget(entity);
            }

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
                if (vel) {
                    physics->setBodyVelocity(body, vel->linear);
                    physics->setBodyAngularVelocity(body, vel->angular);
                }
            }

            ShapeDef sdef;
            sdef.density = collider.density;
            sdef.friction = collider.friction;
            sdef.restitution = collider.restitution;
            sdef.isSensor = collider.isSensor;
            sdef.filter = collider.filter;

            PhysicsShape shape = physics->addBox(body, collider.halfSize.x, collider.halfSize.y, sdef);
            bridge->bodySettings[entity] = settings;
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
            std::string settings = physicsBodySettings(rigidBodyOf(entity), "circle", {collider.radius, 0.f},
                                                       collider.offset, collider.density, collider.friction,
                                                       collider.restitution, collider.isSensor, collider.filter);
            if (bridge->hasBody(entity)) {
                if (bridge->bodySettings[entity] == settings) return;
                physics->destroyBody(bridge->entityToBody[entity]);
                bridge->forget(entity);
            }

            BodyDef bdef;
            bdef.type = BodyType::Static;
            bdef.position = t.position;
            bdef.rotation = t.rotation;
            readRigidBody(entity, bdef);

            PhysicsBody body = physics->createBody(bdef);
            bridge->entityToBody[entity] = body;

            if (registry.has<Velocity2D>() && world.hasComponent(entity, registry.get<Velocity2D>())) {
                auto* vel = world.get<Velocity2D>(entity, registry.get<Velocity2D>());
                if (vel) {
                    physics->setBodyVelocity(body, vel->linear);
                    physics->setBodyAngularVelocity(body, vel->angular);
                }
            }

            ShapeDef sdef;
            sdef.density = collider.density;
            sdef.friction = collider.friction;
            sdef.restitution = collider.restitution;
            sdef.isSensor = collider.isSensor;
            sdef.filter = collider.filter;

            PhysicsShape shape = physics->addCircle(body, collider.offset, collider.radius, sdef);
            bridge->bodySettings[entity] = settings;
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
    for (auto e : toRemove) bridge->forget(e);

    physics_sync_joints(app, *physics, *bridge);
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
                    auto synced = bridge->syncedVelocity.find(entity);
                    bool changed = synced == bridge->syncedVelocity.end() ||
                                   synced->second.linear.x != vel->linear.x || synced->second.linear.y != vel->linear.y ||
                                   synced->second.angular != vel->angular;
                    if (changed) {
                        physics->setBodyVelocity(body, vel->linear);
                        physics->setBodyAngularVelocity(body, vel->angular);
                    }
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
// Only syncs bodies that have Velocity2D (indicating physics-driven movement):
// dynamic and kinematic ones. Bodies without Velocity2D are assumed to be
// ECS-driven (game code controls position).
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

        BodyType type = BodyType::Dynamic;
        if (registry.has<RigidBody2D>() && world.hasComponent(entity, registry.get<RigidBody2D>())) {
            auto* rb = world.get<RigidBody2D>(entity, registry.get<RigidBody2D>());
            if (rb) type = rb->type;
        }

        // Box2D moves dynamic bodies, and kinematic ones by their Velocity2D;
        // either way the entity follows. Only a dynamic body's velocity comes
        // back: a kinematic one keeps the velocity the game gave it.
        if (type != BodyType::Static) {
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

            if (type == BodyType::Dynamic) {
                Vec2 vel = physics->getBodyVelocity(body);
                auto* v = world.getMut<Velocity2D>(entity, velocity2dComp);
                if (v) {
                    v->linear = vel;
                    v->angular = physics->getBodyAngularVelocity(body);
                    bridge->syncedVelocity[entity] = *v;
                }
            }
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
                collisionStartEvents->send(CollisionStart{a, b, ce.point, ce.normal});
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
