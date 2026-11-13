// =============================================================================
// Drift 2D Game Engine - PhysicsResource Implementation
// =============================================================================
// Class-based wrapper around Box2D 3.x.  Replaces the old global-state
// physics.cpp with a proper Resource that owns all state through its Impl.
// =============================================================================

#include <drift/resources/PhysicsResource.hpp>
#include <drift/Log.hpp>
#include <drift/resources/RendererResource.hpp>

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
    PhysicsDebugDraw debug;

    // Sub-step count passed to b2World_Step.  4 is a good balance between
    // accuracy and performance for a 2D game at 60 Hz.
    static constexpr int32_t SUBSTEP_COUNT = 4;

    // Cached events after each step.
    std::vector<ContactEvent> contactBegin;
    std::vector<ContactEvent> contactEnd;
    std::vector<HitEvent>     hitEvents;
    std::vector<ContactEvent> sensorBegin;
    std::vector<ContactEvent> sensorEnd;

    // The first manifold point and normal (A to B) of the contact between
    // two shapes that just began touching.
    std::vector<b2ContactData> contactScratch;
    void contactPoint(b2ShapeId a, b2ShapeId b, ContactEvent& ce) {
        int capacity = b2Shape_GetContactCapacity(a);
        if (capacity <= 0) return;
        contactScratch.resize(static_cast<size_t>(capacity));
        int n = b2Shape_GetContactData(a, contactScratch.data(), capacity);
        for (int i = 0; i < n; ++i) {
            const b2ContactData& d = contactScratch[static_cast<size_t>(i)];
            bool forward = B2_ID_EQUALS(d.shapeIdA, a) && B2_ID_EQUALS(d.shapeIdB, b);
            bool reverse = B2_ID_EQUALS(d.shapeIdA, b) && B2_ID_EQUALS(d.shapeIdB, a);
            if (!forward && !reverse) continue;
            if (d.manifold.pointCount > 0) ce.point = from_b2_vec2(d.manifold.points[0].point);
            Vec2 n2 = from_b2_vec2(d.manifold.normal);
            ce.normal = forward ? n2 : Vec2{-n2.x, -n2.y};
            return;
        }
    }

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
            contactPoint(e.shapeIdA, e.shapeIdB, ce);
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

void PhysicsResource::setGravity(Vec2 gravity) {
    if (B2_IS_NULL(impl_->worldId)) return;
    b2World_SetGravity(impl_->worldId, to_b2_vec2(gravity));
}

Vec2 PhysicsResource::gravity() const {
    if (B2_IS_NULL(impl_->worldId)) return {};
    return from_b2_vec2(b2World_GetGravity(impl_->worldId));
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
    b2Body_SetAwake(id, true);
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

void PhysicsResource::applyTorque(PhysicsBody body, float torque) {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return;
    b2Body_ApplyTorque(id, torque, true);
}

void PhysicsResource::applyAngularImpulse(PhysicsBody body, float impulse) {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return;
    b2Body_ApplyAngularImpulse(id, impulse, true);
}

float PhysicsResource::getBodyAngularVelocity(PhysicsBody body) const {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return 0.f;
    return b2Body_GetAngularVelocity(id);
}

void PhysicsResource::setBodyAngularVelocity(PhysicsBody body, float omega) {
    b2BodyId id = to_b2(body);
    if (!b2Body_IsValid(id)) return;
    b2Body_SetAngularVelocity(id, omega);
}

bool PhysicsResource::bodyValid(PhysicsBody body) const {
    return b2Body_IsValid(to_b2(body));
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
        result.shape    = shape_to_u64(rayResult.shapeId);
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

namespace {
struct OverlapCollect {
    PhysicsShape* out;
    int32_t count = 0;
    int32_t max;
    bool usePoint = false;
    b2Vec2 point{};
};

bool collectOverlap(b2ShapeId shapeId, void* context) {
    auto* c = static_cast<OverlapCollect*>(context);
    if (c->count >= c->max) return false;
    if (c->usePoint && !b2Shape_TestPoint(shapeId, c->point)) return true;
    PhysicsShape s;
    std::memcpy(&s, &shapeId, sizeof(s));
    c->out[c->count++] = s;
    return true;
}
} // namespace

int32_t PhysicsResource::overlapCircle(Vec2 center, float radius, CollisionFilter filter,
                                       PhysicsShape* outShapes, int32_t maxShapes) const {
    if (B2_IS_NULL(impl_->worldId) || !outShapes || maxShapes <= 0) return 0;
    b2QueryFilter qf = b2DefaultQueryFilter();
    qf.categoryBits = filter.category;
    qf.maskBits     = filter.mask;
    b2Circle circle{{0.f, 0.f}, radius};
    b2Transform xf{to_b2_vec2(center), b2Rot_identity};
    OverlapCollect ctx{outShapes, 0, maxShapes};
    b2World_OverlapCircle(impl_->worldId, &circle, xf, qf, collectOverlap, &ctx);
    return ctx.count;
}

int32_t PhysicsResource::overlapPoint(Vec2 point, CollisionFilter filter,
                                      PhysicsShape* outShapes, int32_t maxShapes) const {
    if (B2_IS_NULL(impl_->worldId) || !outShapes || maxShapes <= 0) return 0;
    b2QueryFilter qf = b2DefaultQueryFilter();
    qf.categoryBits = filter.category;
    qf.maskBits     = filter.mask;
    b2AABB box{{point.x - 0.01f, point.y - 0.01f}, {point.x + 0.01f, point.y + 0.01f}};
    OverlapCollect ctx{outShapes, 0, maxShapes, true, to_b2_vec2(point)};
    b2World_OverlapAABB(impl_->worldId, box, qf, collectOverlap, &ctx);
    return ctx.count;
}

// =============================================================================
// Joints
// =============================================================================

static inline b2JointId to_b2(PhysicsJoint joint) {
    b2JointId id;
    std::memcpy(&id, &joint, sizeof(id));
    return id;
}

PhysicsJoint PhysicsResource::createJoint(const JointDef& def) {
    PhysicsJoint out{};
    if (B2_IS_NULL(impl_->worldId)) return out;
    b2BodyId a = to_b2(def.bodyA), b = to_b2(def.bodyB);
    if (!b2Body_IsValid(a) || !b2Body_IsValid(b)) return out;
    b2JointId id = b2_nullJointId;
    switch (def.type) {
        case JointType::Distance: {
            b2DistanceJointDef d = b2DefaultDistanceJointDef();
            d.bodyIdA = a;
            d.bodyIdB = b;
            d.localAnchorA = to_b2_vec2(def.anchorA);
            d.localAnchorB = to_b2_vec2(def.anchorB);
            d.length = std::max(def.length, 0.01f);
            if (def.maxLength > def.minLength) {
                d.enableLimit = true;
                d.minLength = def.minLength;
                d.maxLength = def.maxLength;
            }
            d.enableSpring = def.stiffness > 0.f;
            d.hertz = def.stiffness;
            d.dampingRatio = def.damping;
            d.enableMotor = def.motor;
            d.motorSpeed = def.motorSpeed;
            d.maxMotorForce = def.maxMotorForce;
            d.collideConnected = def.collideConnected;
            id = b2CreateDistanceJoint(impl_->worldId, &d);
        } break;
        case JointType::Revolute: {
            b2RevoluteJointDef d = b2DefaultRevoluteJointDef();
            d.bodyIdA = a;
            d.bodyIdB = b;
            d.localAnchorA = to_b2_vec2(def.anchorA);
            d.localAnchorB = to_b2_vec2(def.anchorB);
            d.enableLimit = def.limit;
            d.lowerAngle = def.lower;
            d.upperAngle = def.upper;
            d.enableMotor = def.motor;
            d.motorSpeed = def.motorSpeed;
            d.maxMotorTorque = def.maxMotorForce;
            d.enableSpring = def.stiffness > 0.f;
            d.hertz = def.stiffness;
            d.dampingRatio = def.damping;
            d.collideConnected = def.collideConnected;
            id = b2CreateRevoluteJoint(impl_->worldId, &d);
        } break;
        case JointType::Prismatic: {
            b2PrismaticJointDef d = b2DefaultPrismaticJointDef();
            d.bodyIdA = a;
            d.bodyIdB = b;
            d.localAnchorA = to_b2_vec2(def.anchorA);
            d.localAnchorB = to_b2_vec2(def.anchorB);
            d.localAxisA = b2Normalize(to_b2_vec2(def.axis));
            d.enableLimit = def.limit;
            d.lowerTranslation = def.lower;
            d.upperTranslation = def.upper;
            d.enableMotor = def.motor;
            d.motorSpeed = def.motorSpeed;
            d.maxMotorForce = def.maxMotorForce;
            d.enableSpring = def.stiffness > 0.f;
            d.hertz = def.stiffness;
            d.dampingRatio = def.damping;
            d.collideConnected = def.collideConnected;
            id = b2CreatePrismaticJoint(impl_->worldId, &d);
        } break;
        case JointType::Weld: {
            b2WeldJointDef d = b2DefaultWeldJointDef();
            d.bodyIdA = a;
            d.bodyIdB = b;
            d.localAnchorA = to_b2_vec2(def.anchorA);
            d.localAnchorB = to_b2_vec2(def.anchorB);
            d.referenceAngle = b2Rot_GetAngle(b2Body_GetRotation(b)) - b2Rot_GetAngle(b2Body_GetRotation(a));
            d.linearHertz = def.stiffness;
            d.angularHertz = def.stiffness;
            d.linearDampingRatio = def.damping;
            d.angularDampingRatio = def.damping;
            d.collideConnected = def.collideConnected;
            id = b2CreateWeldJoint(impl_->worldId, &d);
        } break;
    }
    std::memcpy(&out, &id, sizeof(out));
    return out;
}

void PhysicsResource::destroyJoint(PhysicsJoint joint) {
    b2JointId id = to_b2(joint);
    if (b2Joint_IsValid(id)) b2DestroyJoint(id);
}

bool PhysicsResource::jointValid(PhysicsJoint joint) const {
    return b2Joint_IsValid(to_b2(joint));
}

// =============================================================================
// Stats
// =============================================================================

int32_t PhysicsResource::bodyCount() const {
    if (B2_IS_NULL(impl_->worldId)) return 0;
    b2Counters counters = b2World_GetCounters(impl_->worldId);
    return counters.bodyCount;
}

// =============================================================================
// Debug draw
// =============================================================================

namespace {

constexpr float kDebugZ = 1e5f;
constexpr float kDebugLine = 1.5f;

Color debugColor(b2HexColor c, uint8_t alpha = 255) {
    uint32_t v = static_cast<uint32_t>(c);
    return Color{static_cast<uint8_t>((v >> 16) & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF),
                 static_cast<uint8_t>(v & 0xFF), alpha};
}

RendererResource& rr(void* ctx) { return *static_cast<RendererResource*>(ctx); }

void ddPolygon(const b2Vec2* v, int n, b2HexColor color, void* ctx) {
    rr(ctx).drawPolyline(reinterpret_cast<const Vec2*>(v), n, debugColor(color), kDebugLine, true, kDebugZ);
}

void ddSolidPolygon(b2Transform xf, const b2Vec2* v, int n, float radius, b2HexColor color, void* ctx) {
    (void)radius;
    Vec2 pts[b2_maxPolygonVertices];
    n = n > b2_maxPolygonVertices ? b2_maxPolygonVertices : n;
    for (int i = 0; i < n; ++i) {
        b2Vec2 p = b2TransformPoint(xf, v[i]);
        pts[i] = {p.x, p.y};
    }
    rr(ctx).drawPolygonFilled(pts, n, debugColor(color, 60), kDebugZ);
    rr(ctx).drawPolyline(pts, n, debugColor(color), kDebugLine, true, kDebugZ);
}

void ddCircle(b2Vec2 c, float radius, b2HexColor color, void* ctx) {
    rr(ctx).drawCircle({c.x, c.y}, radius, debugColor(color), kDebugLine, 0, kDebugZ);
}

void ddSolidCircle(b2Transform xf, float radius, b2HexColor color, void* ctx) {
    Vec2 c{xf.p.x, xf.p.y};
    rr(ctx).drawCircleFilled(c, radius, debugColor(color, 60), 0, kDebugZ);
    rr(ctx).drawCircle(c, radius, debugColor(color), kDebugLine, 0, kDebugZ);
    b2Vec2 axis = b2RotateVector(xf.q, b2Vec2{radius, 0.f});
    rr(ctx).drawLine(c, {c.x + axis.x, c.y + axis.y}, debugColor(color), kDebugLine, kDebugZ);
}

void ddCapsule(b2Vec2 p1, b2Vec2 p2, float radius, b2HexColor color, void* ctx) {
    Color col = debugColor(color);
    b2Vec2 d = b2Normalize(b2Sub(p2, p1));
    b2Vec2 n{-d.y * radius, d.x * radius};
    rr(ctx).drawCircle({p1.x, p1.y}, radius, col, kDebugLine, 0, kDebugZ);
    rr(ctx).drawCircle({p2.x, p2.y}, radius, col, kDebugLine, 0, kDebugZ);
    rr(ctx).drawLine({p1.x + n.x, p1.y + n.y}, {p2.x + n.x, p2.y + n.y}, col, kDebugLine, kDebugZ);
    rr(ctx).drawLine({p1.x - n.x, p1.y - n.y}, {p2.x - n.x, p2.y - n.y}, col, kDebugLine, kDebugZ);
}

void ddSolidCapsule(b2Vec2 p1, b2Vec2 p2, float radius, b2HexColor color, void* ctx) {
    ddCapsule(p1, p2, radius, color, ctx);
}

void ddSegment(b2Vec2 p1, b2Vec2 p2, b2HexColor color, void* ctx) {
    rr(ctx).drawLine({p1.x, p1.y}, {p2.x, p2.y}, debugColor(color), kDebugLine, kDebugZ);
}

void ddTransform(b2Transform xf, void* ctx) {
    (void)xf;
    (void)ctx;
}

void ddPoint(b2Vec2 p, float size, b2HexColor color, void* ctx) {
    // Box2D sizes points in screen pixels; draw them as small discs.
    rr(ctx).drawCircleFilled({p.x, p.y}, size * 0.5f + 1.f, debugColor(color), 8, kDebugZ);
}

void ddString(b2Vec2, const char*, void*) {}

} // namespace

void PhysicsResource::setDebugDraw(const PhysicsDebugDraw& options) {
    impl_->debug = options;
}

const PhysicsDebugDraw& PhysicsResource::debugDraw() const {
    return impl_->debug;
}

void PhysicsResource::drawDebug(RendererResource& renderer) const {
    if (!b2World_IsValid(impl_->worldId)) return;
    b2DebugDraw dd = {};
    dd.DrawPolygon = ddPolygon;
    dd.DrawSolidPolygon = ddSolidPolygon;
    dd.DrawCircle = ddCircle;
    dd.DrawSolidCircle = ddSolidCircle;
    dd.DrawCapsule = ddCapsule;
    dd.DrawSolidCapsule = ddSolidCapsule;
    dd.DrawSegment = ddSegment;
    dd.DrawTransform = ddTransform;
    dd.DrawPoint = ddPoint;
    dd.DrawString = ddString;
    dd.drawShapes = impl_->debug.shapes;
    dd.drawAABBs = impl_->debug.aabbs;
    dd.drawContacts = impl_->debug.contacts;
    dd.drawContactNormals = impl_->debug.contacts;
    dd.drawJoints = impl_->debug.joints;
    dd.context = &renderer;
    b2World_Draw(impl_->worldId, &dd);
}

} // namespace drift
