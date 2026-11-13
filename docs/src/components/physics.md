# Physics

Drift integrates Box2D v3.0 for 2D rigid body physics. The physics system is driven entirely through ECS components: add a `RigidBody2D` and a collider to an entity and the physics engine takes over, synchronizing position back to `Transform2D` each frame.

## Components

### RigidBody2D

Defines a physics body and its simulation properties.

```cpp
struct RigidBody2D {
    BodyType type = BodyType::Dynamic;
    bool fixedRotation = false;
    float gravityScale = 1.f;
    float linearDamping = 0.f;
    float angularDamping = 0.f;
};
```

**Header:** `#include <drift/components/Physics.hpp>`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `type` | `BodyType` | `Dynamic` | `Static` (immovable), `Dynamic` (full simulation), or `Kinematic` (moved by code, no gravity). |
| `fixedRotation` | `bool` | `false` | Prevents the body from rotating due to physics forces. |
| `gravityScale` | `float` | `1.f` | Multiplier for gravity on this body. `0` disables gravity; `2` doubles it. |
| `linearDamping` | `float` | `0.f` | Reduces linear velocity over time. Higher values make the body slow down faster. |
| `angularDamping` | `float` | `0.f` | Reduces angular velocity over time. |

### BoxCollider2D

A rectangular collision shape.

```cpp
struct BoxCollider2D {
    Vec2 halfSize = {0.5f, 0.5f};
    Vec2 offset = Vec2::zero();
    bool isSensor = false;
    float density = 1.0f;
    float friction = 0.3f;
    float restitution = 0.0f;
    CollisionFilter filter = {};
};
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `halfSize` | `Vec2` | `{0.5, 0.5}` | Half-width and half-height of the box in world units. A `halfSize` of `{16, 16}` creates a 32x32 box. |
| `offset` | `Vec2` | `{0, 0}` | Offset of the collider center from the entity's position. |
| `isSensor` | `bool` | `false` | Sensors detect overlap but do not cause physical responses (no bouncing or blocking). |
| `density` | `float` | `1.0` | Mass per unit area. Higher density means heavier objects. |
| `friction` | `float` | `0.3` | Surface friction (0 = ice, 1 = rubber). |
| `restitution` | `float` | `0.0` | Bounciness (0 = no bounce, 1 = perfect bounce). |
| `filter` | `CollisionFilter` | `{0x0001, 0xFFFF}` | Category and mask bits for collision filtering. |

### CircleCollider2D

A circular collision shape.

```cpp
struct CircleCollider2D {
    float radius = 0.5f;
    Vec2 offset = Vec2::zero();
    bool isSensor = false;
    float density = 1.0f;
    float friction = 0.3f;
    float restitution = 0.0f;
    CollisionFilter filter = {};
};
```

Fields are the same as `BoxCollider2D`, except `radius` replaces `halfSize`.

### TextureCollider2D

A collision shape taken from the entity's `Sprite`: the outline of every pixel whose alpha is at least `threshold`, holes included. Use it for rocks, characters and terrain that a box or circle does not fit.

```cpp
struct TextureCollider2D {
    float threshold = 0.5f;      // alpha, 0 to 1, from which a pixel is solid
    float tolerance = 1.0f;      // how far in texture pixels the outline may stray from the pixels
    float minArea = 4.f;         // solid parts and holes smaller than this many square texture pixels are dropped
    bool followSprite = false;   // make the shapes again when the frame, origin, flip or scale changes
    bool isSensor = false;
    float density = 1.0f;
    float friction = 0.3f;
    float restitution = 0.0f;
    CollisionFilter filter = {};
};
```

The shape follows the sprite's `srcRect`, `origin` and `flip` and the `Transform2D`'s scale as they are when the body is made. Without a `RigidBody2D`, or with a static one, the body collides along the outlines as closed chain loops: cheap for large terrain, holes and concave ground included. Loops push bodies out from the empty side only, so a body that starts inside solid ground stays there. Dynamic and kinematic bodies, and sensors, get the solid area cut into convex polygons of at most 8 vertices, the most Box2D allows.

`tolerance` sets how closely the simplified outline follows the pixels; raise it for fewer vertices. Specks and holes below `minArea` are dropped. The shapes are worked out once per texture, frame and settings by `TextureShapeCache`, so many entities showing one sprite share them. With `followSprite` the body is made again whenever the frame, origin, flip or scale changes, for animated sprites whose shape changes; leave it off when one shape fits every frame.

The texture's pixels are read back from the GPU the first time a frame of it is traced. After changing a texture's pixels (carving a hole in terrain, for example), call `TextureShapeCache::rebuild` with the image you wrote into it; every collider showing the texture is made again on the next frame:

```cpp
ground.fillCircle(hit, 40.f, Color{0, 0, 0, 0});
renderer->updateTexture(groundTexture, 0, 0, ground.width(), ground.height(), ground.data());
app.getResource<TextureShapeCache>()->rebuild(groundTexture, &ground);
```

See [Physics](../subsystems/physics-resource.md#shapes-from-images) for the outline functions themselves.

### Velocity2D

Read or write the current velocity of a physics body. On an entity without a `RigidBody2D`, `PhysicsPlugin` moves its `Transform2D` by the velocity every frame instead, which is the cheapest way to move many things (see [Behaviors](behaviors.md)).

```cpp
struct Velocity2D {
    Vec2 linear = Vec2::zero();
    float angular = 0.f;
};
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `linear` | `Vec2` | `{0, 0}` | Linear velocity in pixels per second. |
| `angular` | `float` | `0.f` | Angular velocity in radians per second. |

### Joint2D

Joins the entity's body to another entity's body, or pins it to the world when `target` is `InvalidEntityId`. The joint is made once both bodies exist and made again when a field changes; removing the component or either body removes it.

```cpp
cmd.spawn().insert(
    Transform2D{.position = {560, 50}},
    RigidBody2D{},
    CircleCollider2D{.radius = 8},
    Velocity2D{},
    Joint2D{.type = JointType::Distance, .targetAnchor = {500, 50}});   // a pendulum
```

| Field | Default | Description |
|-------|---------|-------------|
| `type` | `Revolute` | `Distance` keeps the anchors `length` apart (a spring when `stiffness` > 0); `Revolute` is a hinge; `Prismatic` slides along `axis`; `Weld` glues the bodies |
| `target` | invalid | The other entity; invalid pins to the world |
| `anchor`, `targetAnchor` | `{0, 0}` | Attachment points in each body's local space; with no target, `targetAnchor` is a world point |
| `length` | `0` | Distance: 0 or less keeps the distance the bodies have when the joint is made |
| `minLength`, `maxLength` | `0` | Distance: with `maxLength` above `minLength`, the length may vary in that range, like a rope |
| `stiffness`, `damping` | `0` | Spring frequency in Hz and damping ratio for Distance, Revolute, Prismatic and Weld; 0 stiffness is rigid |
| `limit`, `lower`, `upper` | off | Revolute: angle range in radians; Prismatic: travel range |
| `motor`, `motorSpeed`, `maxMotorForce` | off | Drives the joint; torque for Revolute |
| `axis` | `{1, 0}` | Prismatic: slide direction in this body's local space |
| `collideConnected` | `false` | Whether the two joined bodies collide with each other |

## Collision events

The physics system emits events when collisions begin and end. Read them with `EventReader`:

```cpp
struct CollisionStart {
    EntityId entityA = InvalidEntityId;
    EntityId entityB = InvalidEntityId;
    Vec2 point;    // the first contact point
    Vec2 normal;   // from A to B
};

struct CollisionEnd {
    EntityId entityA = InvalidEntityId;
    EntityId entityB = InvalidEntityId;
};
```

For sensor collisions (when at least one collider has `isSensor = true`):

```cpp
struct SensorStart {
    EntityId sensorEntity = InvalidEntityId;
    EntityId visitorEntity = InvalidEntityId;
};

struct SensorEnd {
    EntityId sensorEntity = InvalidEntityId;
    EntityId visitorEntity = InvalidEntityId;
};
```

## Basic usage

### Dynamic body with box collider

A crate that falls under gravity and bounces off surfaces:

```cpp
void setup(ResMut<AssetServer> assets, Commands& cmd) {
    TextureHandle tex = assets->load<Texture>("assets/crate.png");

    cmd.spawn().insert(
        Transform2D{.position = {400.f, 100.f}},
        Sprite{.texture = tex, .origin = {16.f, 16.f}},
        RigidBody2D{.type = BodyType::Dynamic},
        BoxCollider2D{.halfSize = {16.f, 16.f}, .restitution = 0.5f}
    );
}
```

### Static platform

Static bodies do not move, but other bodies can collide with them:

```cpp
cmd.spawn().insert(
    Transform2D{.position = {400.f, 500.f}},
    Sprite{.texture = platformTex},
    RigidBody2D{.type = BodyType::Static},
    BoxCollider2D{.halfSize = {200.f, 16.f}}
);
```

### Circle collider

```cpp
cmd.spawn().insert(
    Transform2D{.position = {300.f, 200.f}},
    Sprite{.texture = ballTex, .origin = {8.f, 8.f}},
    RigidBody2D{.type = BodyType::Dynamic, .fixedRotation = true},
    CircleCollider2D{.radius = 8.f, .restitution = 0.8f}
);
```

### Kinematic body

Kinematic bodies are controlled by code. They can push dynamic bodies but are not affected by forces or gravity:

```cpp
cmd.spawn().insert(
    Transform2D{.position = {200.f, 400.f}},
    Sprite{.texture = platformTex},
    RigidBody2D{.type = BodyType::Kinematic},
    BoxCollider2D{.halfSize = {64.f, 8.f}},
    Velocity2D{.linear = {50.f, 0.f}}  // moving platform
);
```

With a `Velocity2D`, physics moves the body and its `Transform2D` follows. Without one, move the `Transform2D` from code and the body follows it.

### Setting velocity

Apply velocity to make an entity jump or move:

```cpp
void jump(Res<InputResource> input, QueryMut<Velocity2D, RigidBody2D> bodies) {
    bodies.iter([&](Velocity2D& vel, RigidBody2D&) {
        if (input->keyPressed(Key::Space)) {
            vel.linear.y = -400.f;  // jump upward
        }
    });
}
```

## Sensors

Sensors detect overlap without causing a physical response. Use them for trigger zones, collectibles, or detection areas:

```cpp
// Collectible coin
cmd.spawn().insert(
    Transform2D{.position = {500.f, 300.f}},
    Sprite{.texture = coinTex},
    RigidBody2D{.type = BodyType::Static},
    CircleCollider2D{.radius = 12.f, .isSensor = true}
);
```

## Reading collision events

Use `EventReader` to respond to collision and sensor events:

```cpp
void onCollision(EventReader<CollisionStart> collisions, Commands& cmd) {
    collisions.read([&](const CollisionStart& e) {
        LOG_INFO("Collision between {} and {}", e.entityA, e.entityB);
    });
}

void onSensorEnter(EventReader<SensorStart> sensors, Commands& cmd) {
    sensors.read([&](const SensorStart& e) {
        // Despawn the collectible when the player touches it
        cmd.entity(e.sensorEntity).despawn();
    });
}
```

Register the event-reading systems like any other system:

```cpp
app.update<onCollision>();
app.update<onSensorEnter>();
```

## Collision filtering

Use `CollisionFilter` to control which bodies can collide with each other. A collision occurs only when `(A.category & B.mask) != 0` and `(B.category & A.mask) != 0`.

```cpp
constexpr uint32_t LAYER_PLAYER = 0x0001;
constexpr uint32_t LAYER_ENEMY  = 0x0002;
constexpr uint32_t LAYER_BULLET = 0x0004;

// Player collides with enemies but not own bullets
BoxCollider2D{
    .halfSize = {12.f, 16.f},
    .filter = {.category = LAYER_PLAYER, .mask = LAYER_ENEMY}
}

// Bullet collides with enemies only
BoxCollider2D{
    .halfSize = {4.f, 4.f},
    .filter = {.category = LAYER_BULLET, .mask = LAYER_ENEMY}
}
```

## Complete example

```cpp
#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/AssetServer.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/Physics.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/Time.hpp>

using namespace drift;

struct GameState : public Resource {
    TextureHandle crateTex;
    TextureHandle platformTex;
};

void setup(ResMut<GameState> game, ResMut<AssetServer> assets, Commands& cmd) {
    game->crateTex = assets->load<Texture>("assets/crate.png");
    game->platformTex = assets->load<Texture>("assets/platform.png");

    cmd.spawn().insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});

    // Ground
    cmd.spawn().insert(
        Transform2D{.position = {400.f, 550.f}},
        Sprite{.texture = game->platformTex},
        RigidBody2D{.type = BodyType::Static},
        BoxCollider2D{.halfSize = {400.f, 16.f}}
    );

    // Falling crates
    for (int i = 0; i < 5; ++i) {
        cmd.spawn().insert(
            Transform2D{.position = {200.f + i * 80.f, 50.f + i * 40.f}},
            Sprite{.texture = game->crateTex, .origin = {16.f, 16.f}},
            RigidBody2D{.type = BodyType::Dynamic},
            BoxCollider2D{.halfSize = {16.f, 16.f}, .restitution = 0.3f}
        );
    }
}

void spawnOnClick(Res<InputResource> input, ResMut<GameState> game,
                  Commands& cmd) {
    if (input->mouseButtonPressed(MouseButton::Left)) {
        Vec2 pos = input->mousePosition();
        cmd.spawn().insert(
            Transform2D{.position = pos},
            Sprite{.texture = game->crateTex, .origin = {16.f, 16.f}},
            RigidBody2D{.type = BodyType::Dynamic},
            BoxCollider2D{.halfSize = {16.f, 16.f}, .restitution = 0.4f}
        );
    }
}

int main() {
    App app;
    app.config().window.title = "Physics Example";
    app.config().window.width = 800;
    app.config().window.height = 600;
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameState>();
    app.startup<setup>();
    app.update<spawnOnClick>();
    return app.run();
}
```

## Notes

- Physics bodies require a `Transform2D` on the same entity. The physics system reads the initial position from it and writes back every frame.
- Changing a `RigidBody2D` or collider field on an entity that already has a body makes the body again with the new settings, at its current position and velocity.
- Do not manually move a `Dynamic` body's `Transform2D` each frame: let the physics engine control it through velocity and forces. To teleport a body, set its position once via `Commands::entity().insert<Transform2D>(...)`.
- Collision events are delivered via the event system. Events from frame N are readable on frame N+1. See the [Events](../concepts/events.md) chapter for details on `EventReader` and `EventWriter`.
- Body type meanings: `Static` = never moves (walls, floor); `Dynamic` = fully simulated (players, projectiles); `Kinematic` = programmatically moved (moving platforms, elevators).
