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

// Joins this entity's body to another's, or pins it to the world. The
// joint is made once both bodies exist, and made again when a field
// changes. Anchors are in each body's local space; with no target the
// target anchor is a point in the world.
enum class JointType : uint8_t {
    Distance,   // keeps the anchors `length` apart; a spring with stiffness > 0
    Revolute,   // a hinge: the anchors meet and the bodies turn about them
    Prismatic,  // a slider along `axis`
    Weld,       // glues the bodies together
};

struct Joint2D {
    JointType type = JointType::Revolute;
    EntityId target = InvalidEntityId;   // invalid: pinned to the world
    Vec2 anchor = Vec2::zero();
    Vec2 targetAnchor = Vec2::zero();
    float length = 0.f;          // Distance: <= 0 keeps the distance at creation
    float minLength = 0.f;       // Distance: with maxLength > minLength, a rope-like range
    float maxLength = 0.f;
    float stiffness = 0.f;       // Distance and Weld: spring frequency in Hz, 0 for rigid
    float damping = 0.f;         // spring damping ratio
    bool limit = false;          // Revolute: angle limits in radians; Prismatic: translation
    float lower = 0.f;
    float upper = 0.f;
    bool motor = false;
    float motorSpeed = 0.f;      // radians or units per second
    float maxMotorForce = 0.f;   // torque for Revolute
    Vec2 axis = {1.f, 0.f};      // Prismatic, in this body's local space
    bool collideConnected = false;
};

// Collision events (emitted by physics sync system). Point and normal (from
// A to B) are the first contact point of a CollisionStart.
struct CollisionStart {
    EntityId entityA = InvalidEntityId;
    EntityId entityB = InvalidEntityId;
    Vec2 point;
    Vec2 normal;
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
