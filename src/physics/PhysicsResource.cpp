// =============================================================================
// Drift 2D Game Engine - PhysicsResource Implementation
// =============================================================================
// Class-based wrapper around Box2D 3.x.  Replaces the old global-state
// physics.cpp with a proper Resource that owns all state through its Impl.
// =============================================================================

#include <drift/resources/PhysicsResource.h>
#include <drift/Log.h>

#include "box2d/box2d.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace drift {

// =============================================================================
// Handle conversion helpers
// =============================================================================
// PhysicsBody  and b2BodyId  share the same layout: { int32_t, int16_t, uint16_t }.
// PhysicsShape and b2ShapeId share the same layout: { int32_t, int16_t, uint16_t }.
// We use memcpy for strict-aliasing-safe conversion.

static_assert(sizeof(PhysicsBody)  == sizeof(b2BodyId),  "PhysicsBody / b2BodyId size mismatch");
static_assert(sizeof(PhysicsShape) == sizeof(b2ShapeId), "PhysicsShape / b2ShapeId size mismatch");
static_assert(sizeof(uint64_t)     == sizeof(b2ShapeId), "uint64_t / b2ShapeId size mismatch");

static inline b2BodyId to_b2(PhysicsBody body) {
    b2BodyId id;
    std::memcpy(&id, &body, sizeof(id));
    return id;
}

static inline PhysicsBody from_b2_body(b2BodyId id) {
    PhysicsBody body;
    std::memcpy(&body, &id, sizeof(body));
    return body;
}

static inline b2ShapeId to_b2(PhysicsShape shape) {
    b2ShapeId id;
    std::memcpy(&id, &shape, sizeof(id));
    return id;
}

static inline PhysicsShape from_b2_shape(b2ShapeId id) {
    PhysicsShape shape;
    std::memcpy(&shape, &id, sizeof(shape));
    return shape;
}

// Pack a b2ShapeId into a uint64_t for ContactEvent / HitEvent storage.
static inline uint64_t shape_to_u64(b2ShapeId id) {
    uint64_t v;
    std::memcpy(&v, &id, sizeof(v));
    return v;
}

// =============================================================================
// Box2D type conversion helpers
// =============================================================================

static inline b2Vec2 to_b2_vec2(Vec2 v) {
    return {v.x, v.y};
}

static inline Vec2 from_b2_vec2(b2Vec2 v) {
    return {v.x, v.y};
}

static b2BodyType to_b2_body_type(BodyType type) {
    switch (type) {
        case BodyType::Static:    return b2_staticBody;
        case BodyType::Dynamic:   return b2_dynamicBody;
        case BodyType::Kinematic: return b2_kinematicBody;
        default:                  return b2_dynamicBody;
    }
}

static b2ShapeDef make_b2_shape_def(const ShapeDef& def) {
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density     = def.density;
    sd.friction    = def.friction;
    sd.restitution = def.restitution;
    sd.isSensor    = def.isSensor;
    sd.filter.categoryBits = def.filter.category;
    sd.filter.maskBits     = def.filter.mask;
    return sd;
}

// =============================================================================
// Impl
// =============================================================================

struct PhysicsResource::Impl {
    b2WorldId worldId = b2_nullWorldId;

    // Sub-step count passed to b2World_Step.  4 is a good balance between
    // accuracy and performance for a 2D game at 60 Hz.
    static constexpr int32_t SUBSTEP_COUNT = 4;

    // Cached events after each step.
    std::vector<ContactEvent> contactBegin;
    std::vector<ContactEvent> contactEnd;
    std::vector<HitEvent>     hitEvents;
    std::vector<ContactEvent> sensorBegin;
    std::vector<ContactEvent> sensorEnd;

    // Cache contact events from b2ContactEvents.
    void cacheContactEvents() {
        contactBegin.clear();
        contactEnd.clear();
        hitEvents.clear();

        b2ContactEvents events = b2World_GetContactEvents(worldId);

        for (int32_t i = 0; i < events.beginCount; ++i) {
            const b2ContactBeginTouchEvent& e = events.beginEvents[i];
            ContactEvent ce;
            ce.shapeA = shape_to_u64(e.shapeIdA);
            ce.shapeB = shape_to_u64(e.shapeIdB);
            contactBegin.push_back(ce);
        }

        for (int32_t i = 0; i < events.endCount; ++i) {
            const b2ContactEndTouchEvent& e = events.endEvents[i];
            ContactEvent ce;
            ce.shapeA = shape_to_u64(e.shapeIdA);
            ce.shapeB = shape_to_u64(e.shapeIdB);
            contactEnd.push_back(ce);
        }

        for (int32_t i = 0; i < events.hitCount; ++i) {
            const b2ContactHitEvent& e = events.hitEvents[i];
            HitEvent he;
            he.shapeA = shape_to_u64(e.shapeIdA);
            he.shapeB = shape_to_u64(e.shapeIdB);
            he.normal = from_b2_vec2(e.normal);
            he.speed  = e.approachSpeed;
            hitEvents.push_back(he);
        }
    }

    // Cache sensor events from b2SensorEvents.
    void cacheSensorEvents() {
        sensorBegin.clear();
        sensorEnd.clear();

        b2SensorEvents events = b2World_GetSensorEvents(worldId);

        for (int32_t i = 0; i < events.beginCount; ++i) {
            const b2SensorBeginTouchEvent& e = events.beginEvents[i];
            ContactEvent ce;
            ce.shapeA = shape_to_u64(e.sensorShapeId);
            ce.shapeB = shape_to_u64(e.visitorShapeId);
            sensorBegin.push_back(ce);
        }

        for (int32_t i = 0; i < events.endCount; ++i) {
            const b2SensorEndTouchEvent& e = events.endEvents[i];
            ContactEvent ce;
            ce.shapeA = shape_to_u64(e.sensorShapeId);
            ce.shapeB = shape_to_u64(e.visitorShapeId);
            sensorEnd.push_back(ce);
        }
    }
};

// =============================================================================
// Constructor / Destructor
// =============================================================================

PhysicsResource::PhysicsResource(Vec2 gravity) : impl_(new Impl) {
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = to_b2_vec2(gravity);

    impl_->worldId = b2CreateWorld(&worldDef);
    if (B2_IS_NULL(impl_->worldId)) {
        DRIFT_LOG_ERROR("PhysicsResource: b2CreateWorld failed");
    } else {
        DRIFT_LOG_INFO("PhysicsResource: world created (gravity=%.1f, %.1f)",
                       gravity.x, gravity.y);
    }
}

PhysicsResource::~PhysicsResource() {
    if (B2_IS_NON_NULL(impl_->worldId)) {
        b2DestroyWorld(impl_->worldId);
    }
    DRIFT_LOG_INFO("PhysicsResource: world destroyed");
    delete impl_;
}

// =============================================================================
// Step
// =============================================================================

void PhysicsResource::step(float dt) {
    if (B2_IS_NULL(impl_->worldId)) return;

    b2World_Step(impl_->worldId, dt, Impl::SUBSTEP_COUNT);

    impl_->cacheContactEvents();
    impl_->cacheSensorEvents();
}

// =============================================================================
// Bodies
// =============================================================================

PhysicsBody PhysicsResource::createBody(const BodyDef& def) {
    PhysicsBody handle{};
    if (B2_IS_NULL(impl_->worldId)) return handle;

    b2BodyDef bd = b2DefaultBodyDef();
    bd.type           = to_b2_body_type(def.type);
    bd.position       = to_b2_vec2(def.position);
    bd.rotation       = b2MakeRot(def.rotation);
    bd.fixedRotation  = def.fixedRotation;
    bd.linearDamping  = def.linearDamping;
    bd.angularDamping = def.angularDamping;
    bd.gravityScale   = def.gravityScale;

    b2BodyId bodyId = b2CreateBody(impl_->worldId, &bd);
    return from_b2_body(bodyId);
}

void PhysicsResource::destroyBody(PhysicsBody body) {
    if (B2_IS_NULL(impl_->worldId)) return;
    b2BodyId id = to_b2(body);
    if (b2Body_IsValid(id)) {
        b2DestroyBody(id);
    }
}

Vec2 PhysicsResource::getBodyPosition(PhysicsBody body) const {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return Vec2::zero();
    return from_b2_vec2(b2Body_GetPosition(id));
}

float PhysicsResource::getBodyRotation(PhysicsBody body) const {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return 0.0f;
    b2Rot rot = b2Body_GetRotation(id);
    return b2Rot_GetAngle(rot);
}

void PhysicsResource::setBodyPosition(PhysicsBody body, Vec2 pos) {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return;
    b2Rot rot = b2Body_GetRotation(id);
    b2Body_SetTransform(id, to_b2_vec2(pos), rot);
}

void PhysicsResource::setBodyRotation(PhysicsBody body, float rotation) {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return;
    b2Vec2 pos = b2Body_GetPosition(id);
    b2Body_SetTransform(id, pos, b2MakeRot(rotation));
}

Vec2 PhysicsResource::getBodyVelocity(PhysicsBody body) const {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return Vec2::zero();
    return from_b2_vec2(b2Body_GetLinearVelocity(id));
}

void PhysicsResource::setBodyVelocity(PhysicsBody body, Vec2 vel) {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return;
    b2Body_SetLinearVelocity(id, to_b2_vec2(vel));
}

void PhysicsResource::applyForce(PhysicsBody body, Vec2 force) {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return;
    b2Body_ApplyForceToCenter(id, to_b2_vec2(force), true);
}

void PhysicsResource::applyImpulse(PhysicsBody body, Vec2 impulse) {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return;
    b2Body_ApplyLinearImpulseToCenter(id, to_b2_vec2(impulse), true);
}

void PhysicsResource::setGravityScale(PhysicsBody body, float scale) {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return;
    b2Body_SetGravityScale(id, scale);
}

void PhysicsResource::setBodyUserData(PhysicsBody body, void* data) {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return;
    b2Body_SetUserData(id, data);
}

void* PhysicsResource::getBodyUserData(PhysicsBody body) const {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return nullptr;
    return b2Body_GetUserData(id);
}

// =============================================================================
// Shapes
// =============================================================================

PhysicsShape PhysicsResource::addCircle(PhysicsBody body, Vec2 offset, float radius, const ShapeDef& def) {
    PhysicsShape handle{};
    b2BodyId bid = to_b2(body);
    if (!b2Body_IsValid(bid)) return handle;

    b2ShapeDef sd = make_b2_shape_def(def);

    b2Circle circle;
    circle.center = to_b2_vec2(offset);
    circle.radius = radius;

    b2ShapeId sid = b2CreateCircleShape(bid, &sd, &circle);
    return from_b2_shape(sid);
}

PhysicsShape PhysicsResource::addBox(PhysicsBody body, float halfW, float halfH, const ShapeDef& def) {
    PhysicsShape handle{};
    b2BodyId bid = to_b2(body);
    if (!b2Body_IsValid(bid)) return handle;

    b2ShapeDef sd = make_b2_shape_def(def);

    b2Polygon box = b2MakeBox(halfW, halfH);
    b2ShapeId sid = b2CreatePolygonShape(bid, &sd, &box);
    return from_b2_shape(sid);
}

PhysicsShape PhysicsResource::addPolygon(PhysicsBody body, const Vec2* vertices, int32_t count, const ShapeDef& def) {
    PhysicsShape handle{};
    b2BodyId bid = to_b2(body);
    if (!b2Body_IsValid(bid) || !vertices || count < 3) return handle;

    b2ShapeDef sd = make_b2_shape_def(def);

    const int32_t maxVerts = b2_maxPolygonVertices;
    int32_t n = (count > maxVerts) ? maxVerts : count;

    b2Vec2 points[b2_maxPolygonVertices];
    for (int32_t i = 0; i < n; ++i) {
        points[i] = to_b2_vec2(vertices[i]);
    }

    b2Hull hull = b2ComputeHull(points, n);
    if (hull.count == 0) {
        DRIFT_LOG_WARN("PhysicsResource::addPolygon: b2ComputeHull failed (degenerate?)");
        return handle;
    }

    b2Polygon poly = b2MakePolygon(&hull, 0.0f);
    b2ShapeId sid = b2CreatePolygonShape(bid, &sd, &poly);
    return from_b2_shape(sid);
}

PhysicsShape PhysicsResource::addCapsule(PhysicsBody body, Vec2 p1, Vec2 p2, float radius, const ShapeDef& def) {
    PhysicsShape handle{};
    b2BodyId bid = to_b2(body);
    if (!b2Body_IsValid(bid)) return handle;

    b2ShapeDef sd = make_b2_shape_def(def);

    b2Capsule capsule;
    capsule.center1 = to_b2_vec2(p1);
    capsule.center2 = to_b2_vec2(p2);
    capsule.radius  = radius;

    b2ShapeId sid = b2CreateCapsuleShape(bid, &sd, &capsule);
    return from_b2_shape(sid);
}

void PhysicsResource::setShapeUserData(PhysicsShape shape, void* data) {
    b2ShapeId sid = to_b2(shape);
    if (!b2Shape_IsValid(sid)) return;
    b2Shape_SetUserData(sid, data);
}

void* PhysicsResource::getShapeUserData(PhysicsShape shape) const {
    b2ShapeId sid = to_b2(shape);
    if (!b2Shape_IsValid(sid)) return nullptr;
    return b2Shape_GetUserData(sid);
}

// =============================================================================
// Contact / sensor events
// =============================================================================

const std::vector<ContactEvent>& PhysicsResource::contactBeginEvents() const {
    return impl_->contactBegin;
}

const std::vector<ContactEvent>& PhysicsResource::contactEndEvents() const {
    return impl_->contactEnd;
}

const std::vector<HitEvent>& PhysicsResource::hitEvents() const {
    return impl_->hitEvents;
}

const std::vector<ContactEvent>& PhysicsResource::sensorBeginEvents() const {
    return impl_->sensorBegin;
}

const std::vector<ContactEvent>& PhysicsResource::sensorEndEvents() const {
    return impl_->sensorEnd;
}

// =============================================================================
// Spatial queries
// =============================================================================

RaycastHit PhysicsResource::raycast(Vec2 origin, Vec2 direction, float distance, CollisionFilter filter) const {
    RaycastHit result;
    if (B2_IS_NULL(impl_->worldId) || distance <= 0.0f) return result;

    b2Vec2 b2Origin    = to_b2_vec2(origin);
    b2Vec2 b2Direction = to_b2_vec2(direction);

    // translation = direction * distance
    b2Vec2 translation = {b2Direction.x * distance, b2Direction.y * distance};

    b2QueryFilter qf = b2DefaultQueryFilter();
    qf.categoryBits = filter.category;
    qf.maskBits     = filter.mask;

    b2RayResult rayResult = b2World_CastRayClosest(impl_->worldId, b2Origin, translation, qf);

    if (rayResult.hit) {
        result.hit      = true;
        result.point    = from_b2_vec2(rayResult.point);
        result.normal   = from_b2_vec2(rayResult.normal);
        result.fraction = rayResult.fraction;
    }

    return result;
}

int32_t PhysicsResource::overlapAABB(Rect aabb, CollisionFilter filter,
                                      PhysicsShape* outShapes, int32_t maxShapes) const {
    if (B2_IS_NULL(impl_->worldId) || !outShapes || maxShapes <= 0) return 0;

    b2AABB b2Aabb;
    b2Aabb.lowerBound = {aabb.x, aabb.y};
    b2Aabb.upperBound = {aabb.x + aabb.w, aabb.y + aabb.h};

    b2QueryFilter qf = b2DefaultQueryFilter();
    qf.categoryBits = filter.category;
    qf.maskBits     = filter.mask;

    struct OverlapCtx {
        PhysicsShape* out;
        int32_t       count;
        int32_t       max;
    };

    OverlapCtx ctx;
    ctx.out   = outShapes;
    ctx.count = 0;
    ctx.max   = maxShapes;

    auto overlapCallback = [](b2ShapeId shapeId, void* context) -> bool {
        OverlapCtx* c = static_cast<OverlapCtx*>(context);
        if (c->count >= c->max) return false; // stop query
        c->out[c->count] = from_b2_shape(shapeId);
        c->count++;
        return true; // continue
    };

    b2World_OverlapAABB(impl_->worldId, b2Aabb, qf, overlapCallback, &ctx);

    return ctx.count;
}

// =============================================================================
// Stats
// =============================================================================

int32_t PhysicsResource::bodyCount() const {
    if (B2_IS_NULL(impl_->worldId)) return 0;
    b2Counters counters = b2World_GetCounters(impl_->worldId);
    return counters.bodyCount;
}

} // namespace drift
