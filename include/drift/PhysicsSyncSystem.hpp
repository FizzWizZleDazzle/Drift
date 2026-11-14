#pragma once

#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/Events.hpp>
#include <drift/components/Physics.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/math/Vector.hpp>
#include <drift/resources/PhysicsResource.hpp>
#include <drift/resources/TextureShapeCache.hpp>
#include <drift/resources/Time.hpp>

#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <vector>

namespace drift {

// The settings a body is made from, to notice when they change.
struct PhysicsBodySettings {
    int type = -1;   // -1: no RigidBody2D
    bool fixedRotation = false;
    float gravityScale = 1.f, linearDamping = 0.f, angularDamping = 0.f;
    enum class Shape : uint8_t { Box, Circle, Texture } shape = Shape::Box;
    Vec2 size{}, offset{};
    float density = 0.f, friction = 0.f, restitution = 0.f;
    bool sensor = false;
    uint32_t category = 0, mask = 0;
    // A texture collider's sprite: its texture and the version of its
    // shapes, and when it follows the sprite, the frame, origin, flip and
    // scale.
    uint32_t texture = 0, textureVersion = 0;
    Vec2 frameAt{}, frameSize{}, origin{}, scale{};
    int flip = 0;

    bool operator==(const PhysicsBodySettings&) const = default;
};

// The settings a joint is made from, with the bodies it joins.
struct PhysicsJointSettings {
    Joint2D joint;
    PhysicsBody self{}, other{};

    bool operator==(const PhysicsJointSettings& o) const {
        const Joint2D& a = joint;
        const Joint2D& b = o.joint;
        return a.type == b.type && a.target == b.target && self.index == o.self.index &&
               self.revision == o.self.revision && other.index == o.other.index &&
               other.revision == o.other.revision && a.anchor == b.anchor && a.targetAnchor == b.targetAnchor &&
               a.length == b.length && a.minLength == b.minLength && a.maxLength == b.maxLength &&
               a.stiffness == b.stiffness && a.damping == b.damping && a.limit == b.limit &&
               a.lower == b.lower && a.upper == b.upper && a.motor == b.motor && a.motorSpeed == b.motorSpeed &&
               a.maxMotorForce == b.maxMotorForce && a.axis == b.axis && a.collideConnected == b.collideConnected;
    }
};

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

    // What the sync knows about one entity's body. Records are found by the
    // low 32 bits of the entity id (an array index, no hashing), so the
    // per-frame sync of thousands of unchanged colliders stays cheap.
    struct BodyRecord {
        EntityId entity = InvalidEntityId;
        PhysicsBody body{};
        // The body and collider settings the body was made from; a body
        // whose settings change is made again.
        PhysicsBodySettings settings;
        // Body position = Transform2D position + offset.
        Vec2 offset{};
        // The Transform2D the body was last placed at. A static body is
        // placed again only when its Transform2D changes.
        Vec2 position{};
        float rotation = 0.f;
        // The velocity the body had when it was last copied into
        // Velocity2D. Only a Velocity2D the game changed since then is
        // written to the body, so impulses and forces applied to the body
        // directly are kept.
        Velocity2D velocity{};
        bool velocityCopied = false;
        // The sync pass that last found the entity's collider.
        uint32_t seen = 0;
        // Shapes listed in shapeToEntity, to drop them with the body.
        std::vector<uint64_t> shapes;
    };
    std::vector<BodyRecord> records;
    std::vector<uint32_t> slots;   // by entity index: 1 + index into records, 0 = none
    uint32_t pass = 0;

    // Joints made from Joint2D components, with the settings they were made
    // from and the bodies they join.
    struct JointEntry {
        PhysicsJoint joint{};
        PhysicsJointSettings settings;
    };
    std::unordered_map<EntityId, JointEntry> joints;
    // A static body at the origin that joints without a target pin to.
    PhysicsBody ground{};
    bool hasGround = false;

    // The record of the entity whose id has these low 32 bits, if any.
    BodyRecord* recordAt(uint32_t index) {
        if (index >= slots.size() || slots[index] == 0) return nullptr;
        return &records[slots[index] - 1];
    }
    const BodyRecord* recordAt(uint32_t index) const {
        if (index >= slots.size() || slots[index] == 0) return nullptr;
        return &records[slots[index] - 1];
    }
    BodyRecord* record(EntityId e) {
        BodyRecord* r = recordAt(static_cast<uint32_t>(e));
        return r && r->entity == e ? r : nullptr;
    }
    const BodyRecord* record(EntityId e) const {
        const BodyRecord* r = recordAt(static_cast<uint32_t>(e));
        return r && r->entity == e ? r : nullptr;
    }

    bool hasBody(EntityId e) const { return record(e) != nullptr; }

    // Starts the record of a body just made for `e`.
    BodyRecord& add(EntityId e, PhysicsBody body) {
        forget(e);
        uint32_t index = static_cast<uint32_t>(e);
        if (BodyRecord* other = recordAt(index)) forget(other->entity);   // an older generation
        if (index >= slots.size()) slots.resize(static_cast<size_t>(index) + 1, 0);
        records.push_back({});
        slots[index] = static_cast<uint32_t>(records.size());
        BodyRecord& r = records.back();
        r.entity = e;
        r.body = body;
        entityToBody[e] = body;
        return r;
    }

    void addShape(BodyRecord& r, uint64_t shape) {
        shapeToEntity[shape] = r.entity;
        r.shapes.push_back(shape);
    }

    // Forgets an entity's body after it was destroyed.
    void forget(EntityId e) {
        entityToBody.erase(e);
        BodyRecord* r = record(e);
        if (!r) return;
        for (uint64_t s : r->shapes) {
            auto it = shapeToEntity.find(s);
            if (it != shapeToEntity.end() && it->second == e) shapeToEntity.erase(it);
        }
        uint32_t at = slots[static_cast<uint32_t>(e)] - 1;
        slots[static_cast<uint32_t>(e)] = 0;
        if (at + 1 != records.size()) {
            records[at] = std::move(records.back());
            slots[static_cast<uint32_t>(records[at].entity)] = at + 1;
        }
        records.pop_back();
    }
};

inline PhysicsBodySettings physicsBodySettings(const RigidBody2D* rb, PhysicsBodySettings::Shape shape, Vec2 size, Vec2 offset,
                                               float density, float friction, float restitution, bool sensor,
                                               CollisionFilter filter) {
    PhysicsBodySettings s;
    if (rb) {
        s.type = int(rb->type);
        s.fixedRotation = rb->fixedRotation;
        s.gravityScale = rb->gravityScale;
        s.linearDamping = rb->linearDamping;
        s.angularDamping = rb->angularDamping;
    }
    s.shape = shape;
    s.size = size;
    s.offset = offset;
    s.density = density;
    s.friction = friction;
    s.restitution = restitution;
    s.sensor = sensor;
    s.category = filter.category;
    s.mask = filter.mask;
    return s;
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

// The entity a collider shape belongs to. The bridge map first: it keeps
// the whole 64-bit id, while shape user data is pointer-sized and on wasm32
// loses the generation of a reused entity id, which the record at that index restores.
inline EntityId entityOfShape(const PhysicsBridge& bridge, const PhysicsResource& physics, uint64_t shapeU64) {
    auto it = bridge.shapeToEntity.find(shapeU64);
    if (it != bridge.shapeToEntity.end()) return it->second;
    void* userData = physics.getShapeUserData(u64_to_shape(shapeU64));
    if (!userData) return InvalidEntityId;
    auto id = static_cast<EntityId>(reinterpret_cast<uintptr_t>(userData));
    if (sizeof(uintptr_t) < sizeof(EntityId)) {
        if (const PhysicsBridge::BodyRecord* r = bridge.recordAt(static_cast<uint32_t>(id))) return r->entity;
    }
    return id;
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
            // destroys their joints; the bodies are part of the settings.
            PhysicsJointSettings settings{*j, self->second, other};
            auto existing = bridge.joints.find(e);
            if (existing != bridge.joints.end()) {
                if (existing->second.settings == settings && physics.jointValid(existing->second.joint)) continue;
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
            bridge.joints[e] = {physics.createJoint(def), settings};
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

// System 1: Create/destroy Box2D bodies to match ECS entities with collider
// components. A body is made again only when its settings change; finding a
// collider unchanged costs one array lookup and a compare.
inline void physics_sync_to_box2d(App& app) {
    auto* physics = app.getResource<PhysicsResource>();
    auto* bridge = app.getResource<PhysicsBridge>();
    if (!physics || !bridge) return;

    auto& world = app.world();
    auto& registry = world.componentRegistry();

    // Colliders found this pass are stamped with it; bodies left unstamped
    // lost their collider or their entity.
    const uint32_t pass = ++bridge->pass;

    // True when the entity's body is current. A body whose settings changed
    // is destroyed so it can be made again.
    auto current = [&](EntityId entity, const PhysicsBodySettings& settings) {
        PhysicsBridge::BodyRecord* rec = bridge->record(entity);
        if (!rec) return false;
        if (rec->settings == settings) {
            rec->seen = pass;
            return true;
        }
        physics->destroyBody(rec->body);
        bridge->forget(entity);
        return false;
    };

    // Makes the body and starts its record.
    auto make = [&](EntityId entity, const Transform2D& t, Vec2 offset, const RigidBody2D* rb,
                    const Velocity2D* vel, const PhysicsBodySettings& settings) -> PhysicsBridge::BodyRecord& {
        BodyDef bdef;
        bdef.type = BodyType::Static;
        bdef.position = t.position + offset;
        bdef.rotation = t.rotation;
        if (rb) {
            bdef.type = rb->type;
            bdef.fixedRotation = rb->fixedRotation;
            bdef.gravityScale = rb->gravityScale;
            bdef.linearDamping = rb->linearDamping;
            bdef.angularDamping = rb->angularDamping;
        }
        PhysicsBody body = physics->createBody(bdef);
        if (vel) {
            physics->setBodyVelocity(body, vel->linear);
            physics->setBodyAngularVelocity(body, vel->angular);
        }
        PhysicsBridge::BodyRecord& rec = bridge->add(entity, body);
        rec.settings = settings;
        rec.offset = offset;
        rec.position = t.position;
        rec.rotation = t.rotation;
        rec.seen = pass;
        return rec;
    };

    auto shapeDef = [](float density, float friction, float restitution, bool sensor, CollisionFilter filter) {
        ShapeDef sdef;
        sdef.density = density;
        sdef.friction = friction;
        sdef.restitution = restitution;
        sdef.isSensor = sensor;
        sdef.filter = filter;
        return sdef;
    };

    // Process entities with BoxCollider2D
    if (registry.has<BoxCollider2D>()) {
        QueryMut<Transform2D, BoxCollider2D, Optional<RigidBody2D>, Optional<Velocity2D>> boxQuery(world, registry);
        boxQuery.iterWithEntity([&](EntityId entity, Transform2D& t, BoxCollider2D& collider,
                                    RigidBody2D* rb, Velocity2D* vel) {
            PhysicsBodySettings settings = physicsBodySettings(rb, PhysicsBodySettings::Shape::Box, collider.halfSize,
                                                               collider.offset, collider.density, collider.friction,
                                                               collider.restitution, collider.isSensor, collider.filter);
            if (current(entity, settings)) return;

            // Body position = entity position + collider offset (so box is centered on visual)
            PhysicsBridge::BodyRecord& rec = make(entity, t, collider.offset, rb, vel, settings);
            PhysicsShape shape = physics->addBox(rec.body, collider.halfSize.x, collider.halfSize.y,
                                                 shapeDef(collider.density, collider.friction, collider.restitution,
                                                          collider.isSensor, collider.filter));
            bridge->addShape(rec, shape_to_u64(shape));
            // Store entity ID as shape user data for fast contact lookup
            physics->setShapeUserData(shape, reinterpret_cast<void*>(static_cast<uintptr_t>(entity)));
        });
    }

    // Process entities with CircleCollider2D
    if (registry.has<CircleCollider2D>()) {
        QueryMut<Transform2D, CircleCollider2D, Optional<RigidBody2D>, Optional<Velocity2D>> circleQuery(world, registry);
        circleQuery.iterWithEntity([&](EntityId entity, Transform2D& t, CircleCollider2D& collider,
                                       RigidBody2D* rb, Velocity2D* vel) {
            PhysicsBodySettings settings = physicsBodySettings(rb, PhysicsBodySettings::Shape::Circle, {collider.radius, 0.f},
                                                               collider.offset, collider.density, collider.friction,
                                                               collider.restitution, collider.isSensor, collider.filter);
            if (current(entity, settings)) return;

            // The circle's offset is part of its shape, not the body position.
            PhysicsBridge::BodyRecord& rec = make(entity, t, Vec2::zero(), rb, vel, settings);
            PhysicsShape shape = physics->addCircle(rec.body, collider.offset, collider.radius,
                                                    shapeDef(collider.density, collider.friction, collider.restitution,
                                                             collider.isSensor, collider.filter));
            bridge->addShape(rec, shape_to_u64(shape));
            physics->setShapeUserData(shape, reinterpret_cast<void*>(static_cast<uintptr_t>(entity)));
        });
    }

    // Process entities with TextureCollider2D: the shape of their sprite's
    // solid pixels, from TextureShapeCache.
    auto* shapeCache = app.getResource<TextureShapeCache>();
    if (registry.has<TextureCollider2D>() && registry.has<Sprite>() && shapeCache) {
        QueryMut<Transform2D, TextureCollider2D, Optional<Sprite>, Optional<RigidBody2D>, Optional<Velocity2D>>
            textureQuery(world, registry);
        textureQuery.iterWithEntity([&](EntityId entity, Transform2D& t, TextureCollider2D& collider,
                                        Sprite* sprite, RigidBody2D* rb, Velocity2D* vel) {
            if (!sprite || !sprite->texture.valid()) return;   // no body until there is a sprite to shape it
            PhysicsBodySettings settings = physicsBodySettings(
                rb, PhysicsBodySettings::Shape::Texture, {collider.threshold, collider.tolerance}, {collider.minArea, 0.f},
                collider.density, collider.friction, collider.restitution, collider.isSensor, collider.filter);
            settings.texture = sprite->texture.id;
            settings.textureVersion = shapeCache->version(sprite->texture);
            if (collider.followSprite) {
                settings.frameAt = {sprite->srcRect.x, sprite->srcRect.y};
                settings.frameSize = {sprite->srcRect.w, sprite->srcRect.h};
                settings.origin = sprite->origin;
                settings.flip = int(sprite->flip);
                settings.scale = t.scale;
            }
            if (current(entity, settings)) return;

            PhysicsBridge::BodyRecord& rec = make(entity, t, Vec2::zero(), rb, vel, settings);
            PhysicsBody body = rec.body;
            bool isStatic = !rb || rb->type == BodyType::Static;

            const TextureShapeCache::Shapes* shapes = shapeCache->get(
                sprite->texture, sprite->srcRect, {collider.threshold, collider.tolerance, collider.minArea});
            if (!shapes) return;

            // Texture pixels to body space: flipped inside the frame, then
            // about the origin and scaled, as the sprite is drawn.
            bool flipX = sprite->flip & Flip::H, flipY = sprite->flip & Flip::V;
            Vec2 size = shapes->size, origin = sprite->origin, scale = t.scale;
            bool mirrored = (flipX != flipY) != ((scale.x < 0.f) != (scale.y < 0.f));
            std::vector<Vec2> pts;
            auto place = [&](const Outline& loop) {
                pts.clear();
                for (Vec2 p : loop) {
                    if (flipX) p.x = size.x - p.x;
                    if (flipY) p.y = size.y - p.y;
                    pts.push_back({(p.x - origin.x) * scale.x, (p.y - origin.y) * scale.y});
                }
                if (mirrored) std::reverse(pts.begin(), pts.end());
            };

            ShapeDef sdef = shapeDef(collider.density, collider.friction, collider.restitution,
                                     collider.isSensor, collider.filter);
            void* userData = reinterpret_cast<void*>(static_cast<uintptr_t>(entity));

            if (isStatic && !collider.isSensor) {
                // Loops along the outlines: cheap, and holes and concave
                // ground need no cutting.
                for (const Outline& loop : shapes->outlines) {
                    place(loop);
                    physics->addLoop(body, pts.data(), static_cast<int32_t>(pts.size()), sdef, userData);
                }
                return;
            }
            float areaScale = std::fabs(scale.x * scale.y);
            for (const Outline& piece : shapes->pieces()) {
                if (std::fabs(outlineArea(piece)) * areaScale < 0.01f) continue;   // a sliver Box2D cannot hold
                place(piece);
                PhysicsShape shape = physics->addPolygon(body, pts.data(), static_cast<int32_t>(pts.size()), sdef);
                bridge->addShape(rec, shape_to_u64(shape));
                physics->setShapeUserData(shape, userData);
            }
        });
    }

    // Destroy the bodies of entities that lost their collider or died (no
    // query found them this pass), so they raise no stale collision events.
    std::vector<EntityId> toRemove;
    for (const PhysicsBridge::BodyRecord& rec : bridge->records) {
        if (rec.seen != pass) toRemove.push_back(rec.entity);
    }
    for (EntityId e : toRemove) {
        if (const PhysicsBridge::BodyRecord* rec = bridge->record(e)) physics->destroyBody(rec->body);
        bridge->forget(e);
    }

    physics_sync_joints(app, *physics, *bridge);
}

// System 2: Sync ECS Transform2D/Velocity2D -> Box2D before step (for kinematic/user-driven bodies)
inline void physics_sync_ecs_to_box2d(App& app) {
    auto* physics = app.getResource<PhysicsResource>();
    auto* bridge = app.getResource<PhysicsBridge>();
    if (!physics || !bridge || bridge->records.empty()) return;

    auto& world = app.world();
    auto& registry = world.componentRegistry();

    // Push a Velocity2D the game changed to its body.
    if (registry.has<Velocity2D>()) {
        Query<Velocity2D> velocities(world, registry);
        velocities.iterWithEntity([&](EntityId entity, const Velocity2D& vel) {
            PhysicsBridge::BodyRecord* rec = bridge->record(entity);
            if (!rec) return;
            bool changed = !rec->velocityCopied || rec->velocity.linear.x != vel.linear.x ||
                           rec->velocity.linear.y != vel.linear.y || rec->velocity.angular != vel.angular;
            if (changed) {
                physics->setBodyVelocity(rec->body, vel.linear);
                physics->setBodyAngularVelocity(rec->body, vel.angular);
            }
        });
    }

    // Place bodies at the Transform2D of entities the game moves. Entities
    // with Velocity2D are driven by Box2D: don't teleport them every frame,
    // as b2Body_SetTransform resets center0=center which disrupts velocity
    // integration. A static body is placed only when its Transform2D
    // changed, so a world of static colliders costs nothing here; any other
    // body only when it is not already there, so a resting body can sleep.
    auto place = [&](EntityId entity, const Transform2D& t) {
        PhysicsBridge::BodyRecord* rec = bridge->record(entity);
        if (!rec) return;
        bool isStatic = rec->settings.type < 0 || rec->settings.type == int(BodyType::Static);
        if (isStatic ? t.position.x == rec->position.x && t.position.y == rec->position.y &&
                           t.rotation == rec->rotation
                     : physics->bodyIsAt(rec->body, t.position + rec->offset, t.rotation)) {
            return;
        }
        physics->setBodyPosition(rec->body, t.position + rec->offset);
        physics->setBodyRotation(rec->body, t.rotation);
        rec->position = t.position;
        rec->rotation = t.rotation;
    };
    if (registry.has<BoxCollider2D>()) {
        Query<Transform2D, BoxCollider2D, Without<Velocity2D>> q(world, registry);
        q.iterWithEntity([&](EntityId e, const Transform2D& t, const BoxCollider2D&) { place(e, t); });
    }
    if (registry.has<CircleCollider2D>()) {
        Query<Transform2D, CircleCollider2D, Without<Velocity2D>> q(world, registry);
        q.iterWithEntity([&](EntityId e, const Transform2D& t, const CircleCollider2D&) { place(e, t); });
    }
    if (registry.has<TextureCollider2D>()) {
        Query<Transform2D, TextureCollider2D, Without<Velocity2D>> q(world, registry);
        q.iterWithEntity([&](EntityId e, const Transform2D& t, const TextureCollider2D&) { place(e, t); });
    }
}

// System 3: Sync Box2D positions/velocities back to ECS components after step.
// Only syncs bodies that have Velocity2D (indicating physics-driven movement):
// dynamic and kinematic ones. Bodies without Velocity2D are assumed to be
// ECS-driven (game code controls position).
inline void physics_sync_from_box2d(App& app) {
    auto* physics = app.getResource<PhysicsResource>();
    auto* bridge = app.getResource<PhysicsBridge>();
    if (!physics || !bridge || bridge->records.empty()) return;

    auto& world = app.world();
    auto& registry = world.componentRegistry();

    if (!registry.has<Velocity2D>()) return;

    QueryMut<Transform2D, Velocity2D, Optional<RigidBody2D>> movers(world, registry);
    movers.iterWithEntity([&](EntityId entity, Transform2D& t, Velocity2D& v, RigidBody2D* rb) {
        PhysicsBridge::BodyRecord* rec = bridge->record(entity);
        if (!rec) return;
        BodyType type = rb ? rb->type : BodyType::Dynamic;

        // Box2D moves dynamic bodies, and kinematic ones by their Velocity2D;
        // either way the entity follows. Only a dynamic body's velocity comes
        // back: a kinematic one keeps the velocity the game gave it.
        if (type == BodyType::Static) return;
        // Subtract collider offset to get entity position
        t.position = physics->getBodyPosition(rec->body) - rec->offset;
        t.rotation = physics->getBodyRotation(rec->body);

        if (type == BodyType::Dynamic) {
            v.linear = physics->getBodyVelocity(rec->body);
            v.angular = physics->getBodyAngularVelocity(rec->body);
            rec->velocity = v;
            rec->velocityCopied = true;
        }
    });
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

    auto lookupEntity = [&](uint64_t shapeU64) { return entityOfShape(*bridge, *physics, shapeU64); };

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
