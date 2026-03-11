// =============================================================================
// Drift 2D Game Engine - Physics Implementation
// =============================================================================
// Implements the public drift/physics.h API by wrapping Box2D 3.x.
// World management, body/shape creation, contact/sensor event caching,
// and spatial queries (raycast, AABB overlap).
// =============================================================================

#include "box2d/box2d.h"

#include <drift/physics.h>
#include <drift/drift.h>

#include <algorithm>
#include <cstring>
#include <vector>

// =============================================================================
// Handle conversion helpers
// =============================================================================
// DriftBody  and b2BodyId  share the same layout: { int32_t, int16_t, uint16_t }.
// DriftShape and b2ShapeId share the same layout: { int32_t, int16_t, uint16_t }.
// We use memcpy for strict-aliasing-safe conversion.

static inline b2BodyId drift_to_b2_body(DriftBody body) {
    b2BodyId id;
    static_assert(sizeof(DriftBody) == sizeof(b2BodyId), "DriftBody / b2BodyId size mismatch");
    std::memcpy(&id, &body, sizeof(id));
    return id;
}

static inline DriftBody b2_to_drift_body(b2BodyId id) {
    DriftBody body;
    static_assert(sizeof(DriftBody) == sizeof(b2BodyId), "DriftBody / b2BodyId size mismatch");
    std::memcpy(&body, &id, sizeof(body));
    return body;
}

static inline b2ShapeId drift_to_b2_shape(DriftShape shape) {
    b2ShapeId id;
    static_assert(sizeof(DriftShape) == sizeof(b2ShapeId), "DriftShape / b2ShapeId size mismatch");
    std::memcpy(&id, &shape, sizeof(id));
    return id;
}

static inline DriftShape b2_to_drift_shape(b2ShapeId id) {
    DriftShape shape;
    static_assert(sizeof(DriftShape) == sizeof(b2ShapeId), "DriftShape / b2ShapeId size mismatch");
    std::memcpy(&shape, &id, sizeof(shape));
    return shape;
}

// =============================================================================
// Internal state (anonymous namespace)
// =============================================================================
namespace {

static b2WorldId g_world_id = b2_nullWorldId;

// Sub-step count passed to b2World_Step.  4 is a good balance between
// accuracy and performance for a 2D game at 60 Hz.
static constexpr int32_t SUBSTEP_COUNT = 4;

// ---- Cached events after each step ------------------------------------------

static std::vector<DriftContactEvent> g_contact_begin;
static std::vector<DriftContactEvent> g_contact_end;
static std::vector<DriftHitEvent>     g_hit_events;
static std::vector<DriftContactEvent> g_sensor_begin;
static std::vector<DriftContactEvent> g_sensor_end;

// ---- Helpers ----------------------------------------------------------------

static b2BodyType drift_to_b2_body_type(DriftBodyType type) {
    switch (type) {
        case DRIFT_BODY_STATIC:    return b2_staticBody;
        case DRIFT_BODY_DYNAMIC:   return b2_dynamicBody;
        case DRIFT_BODY_KINEMATIC: return b2_kinematicBody;
        default:                   return b2_dynamicBody;
    }
}

// Build a b2ShapeDef from a DriftShapeDef.
static b2ShapeDef make_b2_shape_def(const DriftShapeDef* def) {
    b2ShapeDef sd = b2DefaultShapeDef();
    if (def) {
        sd.density     = def->density;
        sd.friction    = def->friction;
        sd.restitution = def->restitution;
        sd.isSensor    = def->is_sensor;
        sd.filter.categoryBits = def->filter.category;
        sd.filter.maskBits     = def->filter.mask;
    }
    return sd;
}

// Convert b2Vec2 <-> DriftVec2.
static inline b2Vec2 drift_to_b2_vec2(DriftVec2 v) {
    return {v.x, v.y};
}

static inline DriftVec2 b2_to_drift_vec2(b2Vec2 v) {
    return {v.x, v.y};
}

// Cache contact events from b2ContactEvents.
static void cache_contact_events(void) {
    g_contact_begin.clear();
    g_contact_end.clear();
    g_hit_events.clear();

    b2ContactEvents events = b2World_GetContactEvents(g_world_id);

    // Begin contact
    for (int32_t i = 0; i < events.beginCount; ++i) {
        const b2ContactBeginTouchEvent& e = events.beginEvents[i];
        DriftContactEvent ce;
        ce.shape_a = b2_to_drift_shape(e.shapeIdA);
        ce.shape_b = b2_to_drift_shape(e.shapeIdB);
        g_contact_begin.push_back(ce);
    }

    // End contact
    for (int32_t i = 0; i < events.endCount; ++i) {
        const b2ContactEndTouchEvent& e = events.endEvents[i];
        DriftContactEvent ce;
        ce.shape_a = b2_to_drift_shape(e.shapeIdA);
        ce.shape_b = b2_to_drift_shape(e.shapeIdB);
        g_contact_end.push_back(ce);
    }

    // Hit events (contact with approach speed above threshold)
    for (int32_t i = 0; i < events.hitCount; ++i) {
        const b2ContactHitEvent& e = events.hitEvents[i];
        DriftHitEvent he;
        he.shape_a = b2_to_drift_shape(e.shapeIdA);
        he.shape_b = b2_to_drift_shape(e.shapeIdB);
        he.normal  = b2_to_drift_vec2(e.normal);
        he.speed   = e.approachSpeed;
        g_hit_events.push_back(he);
    }
}

// Cache sensor events from b2SensorEvents.
static void cache_sensor_events(void) {
    g_sensor_begin.clear();
    g_sensor_end.clear();

    b2SensorEvents events = b2World_GetSensorEvents(g_world_id);

    for (int32_t i = 0; i < events.beginCount; ++i) {
        const b2SensorBeginTouchEvent& e = events.beginEvents[i];
        DriftContactEvent ce;
        ce.shape_a = b2_to_drift_shape(e.sensorShapeId);
        ce.shape_b = b2_to_drift_shape(e.visitorShapeId);
        g_sensor_begin.push_back(ce);
    }

    for (int32_t i = 0; i < events.endCount; ++i) {
        const b2SensorEndTouchEvent& e = events.endEvents[i];
        DriftContactEvent ce;
        ce.shape_a = b2_to_drift_shape(e.sensorShapeId);
        ce.shape_b = b2_to_drift_shape(e.visitorShapeId);
        g_sensor_end.push_back(ce);
    }
}

} // anonymous namespace

// =============================================================================
// Public API
// =============================================================================

extern "C" {

// ---- World ------------------------------------------------------------------

DriftResult drift_physics_init(DriftVec2 gravity) {
    if (B2_IS_NON_NULL(g_world_id)) {
        drift_log(DRIFT_LOG_WARN, "drift_physics_init: world already exists, destroying first");
        b2DestroyWorld(g_world_id);
    }

    b2WorldDef world_def = b2DefaultWorldDef();
    world_def.gravity = drift_to_b2_vec2(gravity);

    g_world_id = b2CreateWorld(&world_def);
    if (B2_IS_NULL(g_world_id)) {
        drift_log(DRIFT_LOG_ERROR, "drift_physics_init: b2CreateWorld failed");
        return DRIFT_ERROR_INIT_FAILED;
    }

    drift_log(DRIFT_LOG_INFO, "Physics world created (gravity=%.1f, %.1f)", gravity.x, gravity.y);
    return DRIFT_OK;
}

void drift_physics_shutdown(void) {
    if (B2_IS_NON_NULL(g_world_id)) {
        b2DestroyWorld(g_world_id);
        g_world_id = b2_nullWorldId;
    }

    g_contact_begin.clear();
    g_contact_end.clear();
    g_hit_events.clear();
    g_sensor_begin.clear();
    g_sensor_end.clear();

    drift_log(DRIFT_LOG_INFO, "Physics world destroyed");
}

void drift_physics_step(float dt) {
    if (B2_IS_NULL(g_world_id)) return;

    b2World_Step(g_world_id, dt, SUBSTEP_COUNT);

    // Cache all events produced during this step so callers can query them
    // until the next step.
    cache_contact_events();
    cache_sensor_events();
}

// ---- Bodies -----------------------------------------------------------------

DriftBody drift_body_create(const DriftBodyDef* def) {
    DriftBody handle;
    std::memset(&handle, 0, sizeof(handle));

    if (B2_IS_NULL(g_world_id) || !def) return handle;

    b2BodyDef bd = b2DefaultBodyDef();
    bd.type            = drift_to_b2_body_type(def->type);
    bd.position        = drift_to_b2_vec2(def->position);
    bd.rotation        = b2MakeRot(def->rotation);
    bd.fixedRotation   = def->fixed_rotation;
    bd.linearDamping   = def->linear_damping;
    bd.angularDamping  = def->angular_damping;
    bd.gravityScale    = def->gravity_scale;

    b2BodyId body_id = b2CreateBody(g_world_id, &bd);
    return b2_to_drift_body(body_id);
}

void drift_body_destroy(DriftBody body) {
    if (B2_IS_NULL(g_world_id)) return;
    b2BodyId id = drift_to_b2_body(body);
    if (b2Body_IsValid(id)) {
        b2DestroyBody(id);
    }
}

DriftVec2 drift_body_get_position(DriftBody body) {
    b2BodyId id = drift_to_b2_body(body);
    if (!b2Body_IsValid(id)) return DRIFT_VEC2_ZERO;
    return b2_to_drift_vec2(b2Body_GetPosition(id));
}

float drift_body_get_rotation(DriftBody body) {
    b2BodyId id = drift_to_b2_body(body);
    if (!b2Body_IsValid(id)) return 0.0f;
    b2Rot rot = b2Body_GetRotation(id);
    return b2Rot_GetAngle(rot);
}

void drift_body_set_position(DriftBody body, DriftVec2 pos) {
    b2BodyId id = drift_to_b2_body(body);
    if (!b2Body_IsValid(id)) return;
    b2Rot rot = b2Body_GetRotation(id);
    b2Body_SetTransform(id, drift_to_b2_vec2(pos), rot);
}

void drift_body_set_rotation(DriftBody body, float rotation) {
    b2BodyId id = drift_to_b2_body(body);
    if (!b2Body_IsValid(id)) return;
    b2Vec2 pos = b2Body_GetPosition(id);
    b2Body_SetTransform(id, pos, b2MakeRot(rotation));
}

DriftVec2 drift_body_get_velocity(DriftBody body) {
    b2BodyId id = drift_to_b2_body(body);
    if (!b2Body_IsValid(id)) return DRIFT_VEC2_ZERO;
    return b2_to_drift_vec2(b2Body_GetLinearVelocity(id));
}

void drift_body_set_velocity(DriftBody body, DriftVec2 vel) {
    b2BodyId id = drift_to_b2_body(body);
    if (!b2Body_IsValid(id)) return;
    b2Body_SetLinearVelocity(id, drift_to_b2_vec2(vel));
}

void drift_body_apply_force(DriftBody body, DriftVec2 force) {
    b2BodyId id = drift_to_b2_body(body);
    if (!b2Body_IsValid(id)) return;
    b2Body_ApplyForceToCenter(id, drift_to_b2_vec2(force), true);
}

void drift_body_apply_impulse(DriftBody body, DriftVec2 impulse) {
    b2BodyId id = drift_to_b2_body(body);
    if (!b2Body_IsValid(id)) return;
    b2Body_ApplyLinearImpulseToCenter(id, drift_to_b2_vec2(impulse), true);
}

void drift_body_set_gravity_scale(DriftBody body, float scale) {
    b2BodyId id = drift_to_b2_body(body);
    if (!b2Body_IsValid(id)) return;
    b2Body_SetGravityScale(id, scale);
}

void drift_body_set_user_data(DriftBody body, void* data) {
    b2BodyId id = drift_to_b2_body(body);
    if (!b2Body_IsValid(id)) return;
    b2Body_SetUserData(id, data);
}

void* drift_body_get_user_data(DriftBody body) {
    b2BodyId id = drift_to_b2_body(body);
    if (!b2Body_IsValid(id)) return nullptr;
    return b2Body_GetUserData(id);
}

// ---- Shapes (colliders) -----------------------------------------------------

DriftShape drift_shape_add_circle(DriftBody body, DriftVec2 offset, float radius, const DriftShapeDef* def) {
    DriftShape handle;
    std::memset(&handle, 0, sizeof(handle));

    b2BodyId bid = drift_to_b2_body(body);
    if (!b2Body_IsValid(bid)) return handle;

    b2ShapeDef sd = make_b2_shape_def(def);

    b2Circle circle;
    circle.center = drift_to_b2_vec2(offset);
    circle.radius = radius;

    b2ShapeId sid = b2CreateCircleShape(bid, &sd, &circle);
    return b2_to_drift_shape(sid);
}

DriftShape drift_shape_add_box(DriftBody body, float half_w, float half_h, const DriftShapeDef* def) {
    DriftShape handle;
    std::memset(&handle, 0, sizeof(handle));

    b2BodyId bid = drift_to_b2_body(body);
    if (!b2Body_IsValid(bid)) return handle;

    b2ShapeDef sd = make_b2_shape_def(def);

    b2Polygon box = b2MakeBox(half_w, half_h);
    b2ShapeId sid = b2CreatePolygonShape(bid, &sd, &box);
    return b2_to_drift_shape(sid);
}

DriftShape drift_shape_add_polygon(DriftBody body, const DriftVec2* vertices, int32_t count, const DriftShapeDef* def) {
    DriftShape handle;
    std::memset(&handle, 0, sizeof(handle));

    b2BodyId bid = drift_to_b2_body(body);
    if (!b2Body_IsValid(bid) || !vertices || count < 3) return handle;

    b2ShapeDef sd = make_b2_shape_def(def);

    // Build a b2Hull from the input vertices, then create a convex polygon.
    // b2ComputeHull expects an array of b2Vec2.
    // Box2D 3.x supports up to b2_maxPolygonVertices (8) vertices.
    const int32_t max_verts = b2_maxPolygonVertices;
    int32_t n = (count > max_verts) ? max_verts : count;

    b2Vec2 points[b2_maxPolygonVertices];
    for (int32_t i = 0; i < n; ++i) {
        points[i] = drift_to_b2_vec2(vertices[i]);
    }

    b2Hull hull = b2ComputeHull(points, n);
    if (hull.count == 0) {
        drift_log(DRIFT_LOG_WARN, "drift_shape_add_polygon: b2ComputeHull failed (degenerate?)");
        return handle;
    }

    b2Polygon poly = b2MakePolygon(&hull, 0.0f);
    b2ShapeId sid = b2CreatePolygonShape(bid, &sd, &poly);
    return b2_to_drift_shape(sid);
}

DriftShape drift_shape_add_capsule(DriftBody body, DriftVec2 p1, DriftVec2 p2, float radius, const DriftShapeDef* def) {
    DriftShape handle;
    std::memset(&handle, 0, sizeof(handle));

    b2BodyId bid = drift_to_b2_body(body);
    if (!b2Body_IsValid(bid)) return handle;

    b2ShapeDef sd = make_b2_shape_def(def);

    b2Capsule capsule;
    capsule.center1 = drift_to_b2_vec2(p1);
    capsule.center2 = drift_to_b2_vec2(p2);
    capsule.radius  = radius;

    b2ShapeId sid = b2CreateCapsuleShape(bid, &sd, &capsule);
    return b2_to_drift_shape(sid);
}

void drift_shape_set_user_data(DriftShape shape, void* data) {
    b2ShapeId sid = drift_to_b2_shape(shape);
    if (!b2Shape_IsValid(sid)) return;
    b2Shape_SetUserData(sid, data);
}

void* drift_shape_get_user_data(DriftShape shape) {
    b2ShapeId sid = drift_to_b2_shape(shape);
    if (!b2Shape_IsValid(sid)) return nullptr;
    return b2Shape_GetUserData(sid);
}

// ---- Contact / sensor events ------------------------------------------------

int32_t drift_physics_get_contact_begin_count(void) {
    return (int32_t)g_contact_begin.size();
}

const DriftContactEvent* drift_physics_get_contact_begin_events(void) {
    return g_contact_begin.empty() ? nullptr : g_contact_begin.data();
}

int32_t drift_physics_get_contact_end_count(void) {
    return (int32_t)g_contact_end.size();
}

const DriftContactEvent* drift_physics_get_contact_end_events(void) {
    return g_contact_end.empty() ? nullptr : g_contact_end.data();
}

int32_t drift_physics_get_hit_count(void) {
    return (int32_t)g_hit_events.size();
}

const DriftHitEvent* drift_physics_get_hit_events(void) {
    return g_hit_events.empty() ? nullptr : g_hit_events.data();
}

int32_t drift_physics_get_sensor_begin_count(void) {
    return (int32_t)g_sensor_begin.size();
}

const DriftContactEvent* drift_physics_get_sensor_begin_events(void) {
    return g_sensor_begin.empty() ? nullptr : g_sensor_begin.data();
}

int32_t drift_physics_get_sensor_end_count(void) {
    return (int32_t)g_sensor_end.size();
}

const DriftContactEvent* drift_physics_get_sensor_end_events(void) {
    return g_sensor_end.empty() ? nullptr : g_sensor_end.data();
}

// ---- Spatial queries --------------------------------------------------------

DriftRaycastHit drift_physics_raycast(DriftVec2 origin, DriftVec2 direction, float distance, DriftCollisionFilter filter) {
    DriftRaycastHit result;
    std::memset(&result, 0, sizeof(result));
    result.hit = false;

    if (B2_IS_NULL(g_world_id) || distance <= 0.0f) return result;

    b2Vec2 b2_origin    = drift_to_b2_vec2(origin);
    b2Vec2 b2_direction = drift_to_b2_vec2(direction);

    // Compute the translation vector (direction * distance).
    b2Vec2 translation = {b2_direction.x * distance, b2_direction.y * distance};

    b2QueryFilter qf = b2DefaultQueryFilter();
    qf.categoryBits = filter.category;
    qf.maskBits     = filter.mask;

    b2RayResult ray_result = b2World_CastRayClosest(g_world_id, b2_origin, translation, qf);

    if (ray_result.hit) {
        result.hit      = true;
        result.shape    = b2_to_drift_shape(ray_result.shapeId);
        result.point    = b2_to_drift_vec2(ray_result.point);
        result.normal   = b2_to_drift_vec2(ray_result.normal);
        result.fraction = ray_result.fraction;
    }

    return result;
}

int32_t drift_physics_overlap_aabb(DriftRect aabb, DriftCollisionFilter filter,
                                   DriftShape* out_shapes, int32_t max_shapes) {
    if (B2_IS_NULL(g_world_id) || !out_shapes || max_shapes <= 0) return 0;

    b2AABB b2_aabb;
    b2_aabb.lowerBound = {aabb.x, aabb.y};
    b2_aabb.upperBound = {aabb.x + aabb.w, aabb.y + aabb.h};

    b2QueryFilter qf = b2DefaultQueryFilter();
    qf.categoryBits = filter.category;
    qf.maskBits     = filter.mask;

    // We use the overlap query callback.  Collect shape IDs into a local
    // vector, then copy them to the caller's buffer.

    struct OverlapCtx {
        DriftShape* out;
        int32_t     count;
        int32_t     max;
    };

    OverlapCtx ctx;
    ctx.out   = out_shapes;
    ctx.count = 0;
    ctx.max   = max_shapes;

    auto overlap_callback = [](b2ShapeId shapeId, void* context) -> bool {
        OverlapCtx* c = static_cast<OverlapCtx*>(context);
        if (c->count >= c->max) return false; // stop query
        c->out[c->count] = b2_to_drift_shape(shapeId);
        c->count++;
        return true; // continue
    };

    b2World_OverlapAABB(g_world_id, b2_aabb, qf, overlap_callback, &ctx);

    return ctx.count;
}

// ---- Stats ------------------------------------------------------------------

int32_t drift_physics_get_body_count(void) {
    if (B2_IS_NULL(g_world_id)) return 0;
    b2Counters counters = b2World_GetCounters(g_world_id);
    return counters.bodyCount;
}

} // extern "C"
