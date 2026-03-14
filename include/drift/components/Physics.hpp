#pragma once

#include <drift/Types.hpp>

namespace drift {

// Rigid body component for physics simulation
struct RigidBody2D {
    BodyType type = BodyType::Dynamic;
    bool fixedRotation = false;
    float gravityScale = 1.f;
    float linearDamping = 0.f;
    float angularDamping = 0.f;
};

// Box-shaped collider component
struct BoxCollider2D {
    Vec2 halfSize = {0.5f, 0.5f};
    Vec2 offset = Vec2::zero();
    bool isSensor = false;
    float density = 1.0f;
    float friction = 0.3f;
    float restitution = 0.0f;
    CollisionFilter filter = {};
};

// Circle-shaped collider component
struct CircleCollider2D {
    float radius = 0.5f;
    Vec2 offset = Vec2::zero();
    bool isSensor = false;
    float density = 1.0f;
    float friction = 0.3f;
    float restitution = 0.0f;
    CollisionFilter filter = {};
};

// Velocity component (used by simple movement system and physics sync)
struct Velocity2D {
    Vec2 linear = Vec2::zero();
    float angular = 0.f;
};

// Collision events (emitted by physics sync system)
struct CollisionStart {
    EntityId entityA = InvalidEntityId;
    EntityId entityB = InvalidEntityId;
};

struct CollisionEnd {
    EntityId entityA = InvalidEntityId;
    EntityId entityB = InvalidEntityId;
};

struct SensorStart {
    EntityId sensorEntity = InvalidEntityId;
    EntityId visitorEntity = InvalidEntityId;
};

struct SensorEnd {
    EntityId sensorEntity = InvalidEntityId;
    EntityId visitorEntity = InvalidEntityId;
};

} // namespace drift
