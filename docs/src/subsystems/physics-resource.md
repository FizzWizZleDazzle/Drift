# Physics

The `PhysicsResource` wraps Box2D v3.0 and provides 2D rigid body physics simulation, collision shapes, raycasting, and contact events. In most cases you interact with physics through ECS components (`RigidBody2D`, `BoxCollider2D`, `CircleCollider2D`, `Velocity2D`), but the resource gives you direct control when needed.

**Header:** `#include <drift/resources/PhysicsResource.hpp>`

**Plugin:** `PhysicsPlugin` (included in `DefaultPlugins`)

## ECS Integration vs. Direct API

The `PhysicsBridge` (set up by `PhysicsPlugin`) automatically synchronizes entities that have `RigidBody2D` and a collider component with the Box2D world. For most gameplay, use components:

```cpp
cmd.spawn().insert(
    Transform2D{.position = {100.f, 300.f}},
    RigidBody2D{.type = BodyType::Dynamic},
    BoxCollider2D{.halfSize = {16.f, 16.f}, .density = 1.f, .restitution = 0.3f},
    Sprite{.texture = crateTexture}
);
```

Use the `PhysicsResource` directly for operations that go beyond component-driven behavior: raycasting, AABB queries, manual force application, or custom body management outside the ECS.

## Bodies

### Creating Bodies

```cpp
PhysicsBody createBody(const BodyDef& def);
void        destroyBody(PhysicsBody body);
```

`BodyDef` controls body properties:

```cpp
struct BodyDef {
    BodyType type = BodyType::Dynamic;   // Static, Dynamic, or Kinematic
    Vec2 position;
    float rotation = 0.f;
    bool fixedRotation = false;
    float linearDamping = 0.f;
    float angularDamping = 0.f;
    float gravityScale = 1.f;
};
```

`BodyType` options:

| Type | Description |
|------|-------------|
| `Static` | Does not move. Infinite mass. Walls, floors, platforms. |
| `Dynamic` | Fully simulated. Responds to forces, gravity, and collisions. |
| `Kinematic` | Moves by velocity only. Not affected by forces or collisions with other kinematic/static bodies. |

### Body Properties

```cpp
Vec2  getBodyPosition(PhysicsBody body) const;
float getBodyRotation(PhysicsBody body) const;
void  setBodyPosition(PhysicsBody body, Vec2 pos);
void  setBodyRotation(PhysicsBody body, float rotation);

Vec2  getBodyVelocity(PhysicsBody body) const;
void  setBodyVelocity(PhysicsBody body, Vec2 vel);

void  applyForce(PhysicsBody body, Vec2 force);
void  applyImpulse(PhysicsBody body, Vec2 impulse);
void  setGravityScale(PhysicsBody body, float scale);
```

**Forces** are continuous and accumulate over a frame. **Impulses** are instantaneous velocity changes. Use forces for engines and thrusters; use impulses for jumps and knockback.

```cpp
void jump(ResMut<PhysicsResource> physics, Res<InputResource> input,
          Query<RigidBody2D, With<PlayerTag>> query) {
    query.iterWithEntity([&](EntityId id, const RigidBody2D& rb) {
        if (input->keyPressed(Key::Space)) {
            // Apply upward impulse (negative Y is up in screen coords)
            physics->applyImpulse(rb.body, Vec2{0.f, -8.f});
        }
    });
}
```

### User Data

Each body can store a `void*` for mapping back to your own data:

```cpp
void  setBodyUserData(PhysicsBody body, void* data);
void* getBodyUserData(PhysicsBody body) const;
```

The `PhysicsBridge` uses this to store the `EntityId`, so you can retrieve which entity owns a physics body.

## Shapes

Shapes define the collision geometry attached to a body. Each body can have multiple shapes.

```cpp
PhysicsShape addBox(PhysicsBody body, float halfW, float halfH, const ShapeDef& def = {});
PhysicsShape addCircle(PhysicsBody body, Vec2 offset, float radius, const ShapeDef& def = {});
PhysicsShape addPolygon(PhysicsBody body, const Vec2* vertices, int32_t count, const ShapeDef& def = {});
PhysicsShape addCapsule(PhysicsBody body, Vec2 p1, Vec2 p2, float radius, const ShapeDef& def = {});
```

`ShapeDef` controls physical material properties:

```cpp
struct ShapeDef {
    float density     = 1.0f;   // mass per unit area
    float friction    = 0.3f;   // surface friction (0 = ice, 1 = sandpaper)
    float restitution = 0.0f;   // bounciness (0 = no bounce, 1 = full bounce)
    bool  isSensor    = false;  // sensors detect overlap but don't produce forces
    CollisionFilter filter;     // category/mask for collision filtering
};
```

### Sensors

When `isSensor = true`, the shape detects overlaps but does not generate contact forces. Use sensors for triggers, pickup areas, and detection zones.

```cpp
// Pickup zone around an item
PhysicsShape zone = physics->addCircle(body, Vec2::zero(), 32.f,
    ShapeDef{.isSensor = true});
```

### Collision Filtering

`CollisionFilter` controls which shapes can collide:

```cpp
struct CollisionFilter {
    uint32_t category = 0x0001;  // this shape's category bits
    uint32_t mask     = 0xFFFF;  // categories this shape collides with
};
```

Two shapes collide only if `(a.category & b.mask) && (b.category & a.mask)`.

```cpp
constexpr uint32_t PLAYER  = 0x0001;
constexpr uint32_t ENEMY   = 0x0002;
constexpr uint32_t BULLET  = 0x0004;
constexpr uint32_t TERRAIN = 0x0008;

// Player collides with enemies and terrain, but not own bullets
ShapeDef playerShape;
playerShape.filter = {.category = PLAYER, .mask = ENEMY | TERRAIN};

// Bullet collides with enemies and terrain
ShapeDef bulletShape;
bulletShape.filter = {.category = BULLET, .mask = ENEMY | TERRAIN};
```

## Contact Events

After each `step()`, the physics world produces contact events. The `PhysicsPlugin` calls `step()` automatically each frame. Read events via:

```cpp
const std::vector<ContactEvent>& contactBeginEvents() const;
const std::vector<ContactEvent>& contactEndEvents() const;
const std::vector<ContactEvent>& sensorBeginEvents() const;
const std::vector<ContactEvent>& sensorEndEvents() const;
const std::vector<HitEvent>&     hitEvents() const;
```

```cpp
struct ContactEvent {
    uint64_t shapeA;
    uint64_t shapeB;
};

struct HitEvent {
    uint64_t shapeA;
    uint64_t shapeB;
    Vec2 normal;
    float speed;
};
```

For ECS-driven collision handling, prefer `EventReader<CollisionStart>` and `EventReader<SensorStart>` in system parameters. These are higher-level events produced by the `PhysicsBridge` that carry `EntityId` values rather than raw shape indices.

## Raycasting

```cpp
RaycastHit raycast(Vec2 origin, Vec2 direction, float distance,
                   CollisionFilter filter = {}) const;
```

Casts a ray from `origin` in `direction` for up to `distance` units. Returns the closest hit:

```cpp
struct RaycastHit {
    Vec2 point;      // world-space hit point
    Vec2 normal;     // surface normal at hit point
    float fraction;  // 0..1 along the ray
    bool hit;        // true if something was hit
};
```

### Example: Line of Sight

```cpp
void lineOfSight(Res<PhysicsResource> physics,
                 Query<Transform2D, With<EnemyTag>> enemies,
                 Query<Transform2D, With<PlayerTag>> player) {
    Vec2 playerPos;
    player.iter([&](const Transform2D& t) { playerPos = t.position; });

    enemies.iter([&](const Transform2D& t) {
        Vec2 toPlayer = playerPos - t.position;
        float dist = toPlayer.length();
        Vec2 dir = toPlayer.normalized();

        auto hit = physics->raycast(t.position, dir, dist,
                                    CollisionFilter{.mask = 0x0008}); // terrain only
        if (!hit.hit) {
            // Clear line of sight to player
        }
    });
}
```

## AABB Query

```cpp
int32_t overlapAABB(Rect aabb, CollisionFilter filter,
                    PhysicsShape* outShapes, int32_t maxShapes) const;
```

Finds all shapes overlapping an axis-aligned bounding box. Returns the number of shapes found, writing up to `maxShapes` into the output array.

```cpp
void explosionDamage(Res<PhysicsResource> physics, Vec2 center, float radius) {
    Rect aabb{center.x - radius, center.y - radius, radius * 2.f, radius * 2.f};
    PhysicsShape shapes[64];
    int32_t count = physics->overlapAABB(aabb, CollisionFilter{}, shapes, 64);

    for (int i = 0; i < count; ++i) {
        // Apply damage to entities associated with shapes[i]
    }
}
```

## Gravity

```cpp
// Set at construction
PhysicsResource physics(Vec2{0.f, 9.8f});

// Or change at runtime -- for example, low-gravity zone
void setGravity(ResMut<PhysicsResource> physics) {
    physics->setGravity(Vec2{0.f, 4.9f});  // half gravity
}
```

The default gravity is `{0, 9.8}` (downward in screen coordinates where Y increases downward).

## Stats

```cpp
int32_t bodyCount() const;  // total active physics bodies
```
