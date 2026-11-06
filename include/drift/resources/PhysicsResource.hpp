#pragma once

#include <drift/Resource.hpp>
#include <drift/Types.hpp>

#include <vector>

namespace drift {

// Physics handles are direct Box2D aliases (not generational pool handles).
struct PhysicsBody { int32_t index; int16_t world; uint16_t revision; };
struct PhysicsShape { int32_t index; int16_t world; uint16_t revision; };

class RendererResource;

// What the physics debug overlay draws. Off by default.
struct PhysicsDebugDraw {
    bool enabled = false;
    bool shapes = true;       // collider outlines (filled for solid bodies)
    bool aabbs = false;       // shape bounding boxes
    bool contacts = false;    // contact points and normals
    bool joints = false;
};

class PhysicsResource : public Resource {
public:
    explicit PhysicsResource(Vec2 gravity = {0.f, 9.8f});
    ~PhysicsResource() override;

    DRIFT_RESOURCE(PhysicsResource)

    void step(float dt);

    // World gravity in units per second squared (default {0, 9.8}; +Y is down).
    void setGravity(Vec2 gravity);
    Vec2 gravity() const;

    // Bodies
    PhysicsBody createBody(const BodyDef& def);
    void destroyBody(PhysicsBody body);
    Vec2 getBodyPosition(PhysicsBody body) const;
    float getBodyRotation(PhysicsBody body) const;
    void setBodyPosition(PhysicsBody body, Vec2 pos);
    void setBodyRotation(PhysicsBody body, float rotation);
    Vec2 getBodyVelocity(PhysicsBody body) const;
    void setBodyVelocity(PhysicsBody body, Vec2 vel);
    void applyForce(PhysicsBody body, Vec2 force);
    void applyImpulse(PhysicsBody body, Vec2 impulse);
    void setGravityScale(PhysicsBody body, float scale);
    void setBodyUserData(PhysicsBody body, void* data);
    void* getBodyUserData(PhysicsBody body) const;

    // Shapes
    PhysicsShape addCircle(PhysicsBody body, Vec2 offset, float radius, const ShapeDef& def = {});
    PhysicsShape addBox(PhysicsBody body, float halfW, float halfH, const ShapeDef& def = {});
    PhysicsShape addPolygon(PhysicsBody body, const Vec2* vertices, int32_t count, const ShapeDef& def = {});
    PhysicsShape addCapsule(PhysicsBody body, Vec2 p1, Vec2 p2, float radius, const ShapeDef& def = {});
    void setShapeUserData(PhysicsShape shape, void* data);
    void* getShapeUserData(PhysicsShape shape) const;

    // Contact events
    const std::vector<ContactEvent>& contactBeginEvents() const;
    const std::vector<ContactEvent>& contactEndEvents() const;
    const std::vector<HitEvent>& hitEvents() const;
    const std::vector<ContactEvent>& sensorBeginEvents() const;
    const std::vector<ContactEvent>& sensorEndEvents() const;

    // Queries
    RaycastHit raycast(Vec2 origin, Vec2 direction, float distance, CollisionFilter filter = {}) const;
    int32_t overlapAABB(Rect aabb, CollisionFilter filter, PhysicsShape* outShapes, int32_t maxShapes) const;

    int32_t bodyCount() const;

    // Debug overlay, drawn in world space by PhysicsPlugin's render system.
    void setDebugDraw(const PhysicsDebugDraw& options);
    const PhysicsDebugDraw& debugDraw() const;
    // Queue the debug shapes on the renderer now (Render phase).
    void drawDebug(RendererResource& renderer) const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
