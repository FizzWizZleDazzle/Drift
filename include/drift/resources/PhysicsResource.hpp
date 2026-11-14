#pragma once

#include <drift/Resource.hpp>
#include <drift/Types.hpp>
#include <drift/components/Physics.hpp>

#include <vector>

namespace drift {

// Physics handles are direct Box2D aliases (not generational pool handles).
struct PhysicsBody { int32_t index; int16_t world; uint16_t revision; };
struct PhysicsShape { int32_t index; int16_t world; uint16_t revision; };
struct PhysicsJoint { int32_t index; int16_t world; uint16_t revision; };
struct PhysicsChain { int32_t index; int16_t world; uint16_t revision; };

// A joint between two bodies; see Joint2D for the fields. Anchors are in
// each body's local space.
struct JointDef {
    JointType type = JointType::Revolute;
    PhysicsBody bodyA{};
    PhysicsBody bodyB{};
    Vec2 anchorA;
    Vec2 anchorB;
    float length = 1.f;
    float minLength = 0.f;
    float maxLength = 0.f;
    float stiffness = 0.f;
    float damping = 0.f;
    bool limit = false;
    float lower = 0.f;
    float upper = 0.f;
    bool motor = false;
    float motorSpeed = 0.f;
    float maxMotorForce = 0.f;
    Vec2 axis = {1.f, 0.f};
    bool collideConnected = false;
};

class RendererResource;

// What the "colliders" debug view draws (PhysicsPlugin adds the view; switch
// it with DebugViews::set). It draws on OverlayLayer, so lighting and post
// effects leave it at full brightness.
struct PhysicsDebugDraw {
    bool shapes = true;       // colliders: filled outlines, sensors dashed; colors from physicsDebugColor
    bool aabbs = false;       // shape bounding boxes
    bool contacts = true;     // where contacts began, with their normals, for half a second of game time
    bool velocities = true;   // each moving body's velocity as an arrow (a quarter second of travel)
    bool joints = true;
};

// The debug view's color for a body: green static, blue kinematic, orange
// dynamic; a sleeping body's color at under half brightness.
Color physicsDebugColor(BodyType type, bool awake);

// How many pixels make one meter for the physics world. Box2D's
// tolerances are tuned in meters (how far shapes may overlap, how close
// a contact starts, how slow a body sleeps, a top speed of 400 meters a
// second); this scales them to pixels.
constexpr float DefaultPixelsPerMeter = 32.f;

class PhysicsResource : public Resource {
public:
    // Box2D keeps the length scale in a global it reads when worlds and
    // bodies are made, so the last PhysicsResource made sets it.
    explicit PhysicsResource(Vec2 gravity = {0.f, 9.8f}, float pixelsPerMeter = DefaultPixelsPerMeter);
    ~PhysicsResource() override;

    DRIFT_RESOURCE(PhysicsResource)

    void step(float dt);

    // World gravity in pixels per second squared (default {0, 9.8}; +Y is down).
    void setGravity(Vec2 gravity);
    Vec2 gravity() const;

    // Bodies
    PhysicsBody createBody(const BodyDef& def);
    void destroyBody(PhysicsBody body);
    Vec2 getBodyPosition(PhysicsBody body) const;
    float getBodyRotation(PhysicsBody body) const;
    void setBodyPosition(PhysicsBody body, Vec2 pos);
    void setBodyRotation(PhysicsBody body, float rotation);
    // True when the body sits exactly where setBodyPosition(pos) and
    // setBodyRotation(rotation) would put it, to skip moves that change
    // nothing (a move wakes the body).
    bool bodyIsAt(PhysicsBody body, Vec2 pos, float rotation) const;
    Vec2 getBodyVelocity(PhysicsBody body) const;
    void setBodyVelocity(PhysicsBody body, Vec2 vel);
    void applyForce(PhysicsBody body, Vec2 force);
    void applyImpulse(PhysicsBody body, Vec2 impulse);
    void applyTorque(PhysicsBody body, float torque);
    void applyAngularImpulse(PhysicsBody body, float impulse);
    float getBodyAngularVelocity(PhysicsBody body) const;
    void setBodyAngularVelocity(PhysicsBody body, float omega);
    bool bodyValid(PhysicsBody body) const;
    void setGravityScale(PhysicsBody body, float scale);
    void setBodyUserData(PhysicsBody body, void* data);
    void* getBodyUserData(PhysicsBody body) const;

    // Shapes
    PhysicsShape addCircle(PhysicsBody body, Vec2 offset, float radius, const ShapeDef& def = {});
    PhysicsShape addBox(PhysicsBody body, float halfW, float halfH, const ShapeDef& def = {});
    PhysicsShape addPolygon(PhysicsBody body, const Vec2* vertices, int32_t count, const ShapeDef& def = {});
    PhysicsShape addCapsule(PhysicsBody body, Vec2 p1, Vec2 p2, float radius, const ShapeDef& def = {});
    // A closed loop of segments that collides on one side only: the
    // outside of a loop with positive signed area (see outlineArea in
    // drift/ImageShapes.hpp), the inside of one with negative area. Cheap
    // for large static outlines such as terrain. Loops have no mass and
    // cannot be sensors (`def.density` and `def.isSensor` are ignored),
    // and they do not collide with other loops. `userData` becomes every
    // segment's shape user data. Points closer together than Box2D can
    // tell apart are merged; an empty chain comes back when fewer than 3
    // remain.
    PhysicsChain addLoop(PhysicsBody body, const Vec2* points, int32_t count, const ShapeDef& def = {},
                         void* userData = nullptr);
    void setShapeUserData(PhysicsShape shape, void* data);
    void* getShapeUserData(PhysicsShape shape) const;

    // Joints. Destroying a body destroys its joints.
    PhysicsJoint createJoint(const JointDef& def);
    void destroyJoint(PhysicsJoint joint);
    bool jointValid(PhysicsJoint joint) const;

    // Contact events
    const std::vector<ContactEvent>& contactBeginEvents() const;
    const std::vector<ContactEvent>& contactEndEvents() const;
    const std::vector<HitEvent>& hitEvents() const;
    const std::vector<ContactEvent>& sensorBeginEvents() const;
    const std::vector<ContactEvent>& sensorEndEvents() const;

    // Queries
    // The closest shape along the ray; `filter.mask` picks the categories it
    // can hit. Sensors count.
    RaycastHit raycast(Vec2 origin, Vec2 direction, float distance, CollisionFilter filter = {}) const;
    // Shapes whose bounding boxes touch the rect.
    int32_t overlapAABB(Rect aabb, CollisionFilter filter, PhysicsShape* outShapes, int32_t maxShapes) const;
    // Shapes that touch a circle, and shapes that contain a point (exact).
    int32_t overlapCircle(Vec2 center, float radius, CollisionFilter filter, PhysicsShape* outShapes,
                          int32_t maxShapes) const;
    int32_t overlapPoint(Vec2 point, CollisionFilter filter, PhysicsShape* outShapes, int32_t maxShapes) const;

    int32_t bodyCount() const;

    // What the "colliders" debug view shows.
    void setDebugDraw(const PhysicsDebugDraw& options);
    const PhysicsDebugDraw& debugDraw() const;
    // Queue the debug shapes on the renderer now (Render phase); the
    // "colliders" view calls it.
    void drawDebug(RendererResource& renderer) const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
