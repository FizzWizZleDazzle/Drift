# Commands

`Commands` is the deferred mutation API. All entity creation, component insertion, component removal, and entity destruction go through commands. Mutations are queued up during system execution and applied (flushed) to the `World` at defined synchronization points -- after `Startup`, `PreUpdate`, `Update`, and `PostUpdate` phases.

This deferred model is critical for thread safety. When the parallel scheduler runs multiple systems concurrently, each thread gets its own `Commands` buffer. After a parallel batch completes, all buffers are flushed to the world sequentially.

## Commands as a System Parameter

Request a `Commands&` parameter in any system function:

```cpp
void spawnSystem(Res<Time> time, Commands& cmd) {
    if (time->elapsed > 5.0) {
        cmd.spawn().insert(Transform2D{}, Sprite{});
    }
}
```

The engine injects the correct `Commands` instance automatically. On the main thread, this is the app's primary command buffer. On worker threads during parallel execution, it is a per-thread buffer.

## EntityCommands

`Commands::spawn()` and `Commands::entity()` return an `EntityCommands` builder for fluent, chainable mutations on a single entity.

### Spawning Entities

`spawn()` allocates a new entity ID and returns an `EntityCommands` builder:

```cpp
void setup(Commands& cmd) {
    EntityId id = cmd.spawn()
        .insert(Transform2D{.position = {100.f, 200.f}})
        .insert(Sprite{.zOrder = 1.f});
}
```

The `EntityCommands` object implicitly converts to `EntityId`, so you can assign the result directly:

```cpp
EntityId player = cmd.spawn()
    .insert(Transform2D{}, Sprite{}, PlayerTag{});
```

### Mutating Existing Entities

`entity(id)` returns an `EntityCommands` for an existing entity:

```cpp
void moveCamera(ResMut<GameState> game, Commands& cmd) {
    cmd.entity(game->camera)
        .insert<Transform2D>({.position = {newX, newY}});
}
```

This queues a component set operation. When flushed, the entity's `Transform2D` will be updated (or added if it didn't have one).

### Single-Component Insert

Insert one component at a time using the template form:

```cpp
cmd.spawn()
    .insert<Transform2D>({.position = {0, 0}})
    .insert<Sprite>({.zOrder = 1.f})
    .insert<PlayerTag>({});
```

### Variadic Insert

Insert multiple components in a single call. The types are deduced from the arguments:

```cpp
cmd.spawn()
    .insert(Transform2D{.position = {0, 0}},
            Sprite{.texture = tex, .zOrder = 1.f},
            Health{.current = 100.f},
            PlayerTag{});
```

This is the most common and ergonomic form. Each argument is inserted as a separate component. Variadic insert accepts any number of components.

### Bundle Insert

A `Bundle<Ts...>` groups related components into a reusable unit. Inserting a bundle inserts all of its components:

```cpp
using PhysicsBundle = Bundle<RigidBody2D, CircleCollider2D, Velocity2D>;

PhysicsBundle physics{
    RigidBody2D{.type = BodyType::Dynamic, .gravityScale = 0.f},
    CircleCollider2D{.radius = 8.f},
    Velocity2D{.linear = {100.f, 0.f}}
};

cmd.spawn()
    .insert(Transform2D{}, Sprite{})
    .insert(physics);
```

`Bundle` inherits from `std::tuple` and supports the same construction syntax:

```cpp
// Direct construction
auto bundle = Bundle{
    RigidBody2D{.type = BodyType::Dynamic},
    BoxCollider2D{.halfSize = {16.f, 16.f}},
};

cmd.spawn()
    .insert(Transform2D{}, Sprite{})
    .insert(bundle);
```

Bundles help reduce boilerplate when many entities share the same component set (for example, every physics entity needs `RigidBody2D` + a collider + `Velocity2D`).

### Removing Components

Remove a component by type:

```cpp
cmd.entity(entityId).remove<Velocity2D>();
```

The component is removed when commands are flushed. The entity continues to exist.

### Despawning Entities

Destroy an entity and all its components:

```cpp
cmd.entity(bulletId).despawn();
```

If the entity has `Children`, all children are despawned recursively (cascading despawn, handled by the `HierarchyPlugin`).

## Hierarchy: withChildren and ChildSpawner

`withChildren()` spawns child entities and establishes a parent-child relationship in a single expression. It accepts a callable that receives a `ChildSpawner&`:

```cpp
cmd.spawn()
    .insert(Transform2D{}, Sprite{}, Name{"Ship"})
    .withChildren([&](ChildSpawner& spawner) {
        // Each child automatically gets a Parent component
        spawner.spawn()
            .insert(Transform2D{.position = {-20.f, 0.f}},
                    Sprite{}, Name{"Left Wing"});

        spawner.spawn()
            .insert(Transform2D{.position = {20.f, 0.f}},
                    Sprite{}, Name{"Right Wing"});
    });
```

Under the hood, `withChildren()` does the following:

1. Creates a `ChildSpawner` bound to the parent entity.
2. Each `spawner.spawn()` creates a new entity with a `Parent{parentId}` component.
3. After the callable returns, a `Children` component is inserted on the parent with all child IDs.

The `HierarchyPlugin` propagates transforms from parent to children via `GlobalTransform2D` in the `PostUpdate` phase. When a parent is despawned, all children are despawned recursively.

### Nested Hierarchies

You can nest `withChildren` calls to create deeper hierarchies:

```cpp
cmd.spawn()
    .insert(Transform2D{}, Name{"Root"})
    .withChildren([&](ChildSpawner& s1) {
        s1.spawn()
            .insert(Transform2D{}, Name{"Child A"})
            .withChildren([&](ChildSpawner& s2) {
                s2.spawn()
                    .insert(Transform2D{}, Name{"Grandchild"});
            });

        s1.spawn()
            .insert(Transform2D{}, Name{"Child B"});
    });
```

### Hierarchy Limits

The `Children` component stores up to `MaxChildren` (16) child IDs in a fixed array. If you need more children, split them across multiple intermediate parent entities.

## queue(): Arbitrary Deferred Mutations

`queue()` enqueues a callable that receives a `World&` reference. It runs during the command flush, giving you direct world access for operations that the typed API does not cover.

```cpp
cmd.queue([](World& world) {
    // Direct world manipulation during flush
    EntityId e = world.createEntity();
    world.setComponent(e, world.transform2dId(), &someTransform, sizeof(Transform2D));
});
```

Use `queue()` sparingly -- it bypasses the typed safety of `EntityCommands` and runs with exclusive world access. It is useful for:

- Bulk entity operations that would be slow with individual commands
- Interacting with flecs features not exposed by the typed API
- Complex multi-entity mutations that need to see each other's results

## Command Flushing

Commands are flushed at specific points in the frame:

1. After all `Startup` systems run (once)
2. After the `PreUpdate` phase
3. After the `Update` phase
4. After the `PostUpdate` phase
5. During state transitions (`OnExit` and `OnEnter` callbacks each flush)

During a flush, all queued commands execute in order: spawns, inserts, removes, despawns, and custom queue entries. After flushing, the world reflects all mutations and subsequent systems see the updated state.

Between flushes, newly spawned entities exist (you can store and reference their IDs), but their components are not yet visible to queries. This is by design: it prevents systems from seeing partially-constructed entities.

## Complete Example

A system that spawns bullets, tracks them, and cleans up expired ones:

```cpp
struct Bullet {
    float lifetime;
};

struct BulletTag {};

struct GameState : public Resource {
    DRIFT_RESOURCE(GameState)
    TextureHandle bulletTex;
    EntityId player = InvalidEntityId;
};

void spawnBullets(Res<InputResource> input,
                  Res<GameState> game,
                  Query<Transform2D, With<PlayerTag>> players,
                  Commands& cmd) {
    if (!input->mouseButtonPressed(MouseButton::Left)) return;

    Vec2 playerPos{};
    players.iter([&](const Transform2D& t) {
        playerPos = t.position;
    });

    Vec2 aim = (input->mousePosition() - Vec2{640.f, 360.f}).normalized();

    cmd.spawn()
        .insert(Transform2D{.position = playerPos + aim * 20.f},
                Sprite{.texture = game->bulletTex,
                       .origin = {2.f, 2.f},
                       .tint = {255, 255, 200, 255},
                       .zOrder = 5.f},
                BulletTag{},
                Bullet{.lifetime = 3.f},
                RigidBody2D{.type = BodyType::Dynamic,
                            .fixedRotation = true,
                            .gravityScale = 0.f},
                CircleCollider2D{.radius = 2.f},
                Velocity2D{.linear = aim * 600.f},
                TrailRenderer{.width = 2.f,
                              .lifetime = 0.2f,
                              .colorStart = {1.f, 1.f, 0.8f, 0.7f},
                              .colorEnd = {1.f, 0.5f, 0.1f, 0.f}});
}

void cleanupBullets(Res<Time> time,
                    QueryMut<Bullet> bullets,
                    Commands& cmd) {
    bullets.iterWithEntity([&](EntityId id, Bullet& b) {
        b.lifetime -= time->delta;
        if (b.lifetime <= 0.f) {
            cmd.entity(id).despawn();
        }
    });
}
```

Key points:

- `cmd.spawn()` returns immediately with a valid `EntityId`. The actual entity creation happens during flush.
- Variadic insert puts all components on the entity in a single fluent call.
- `cmd.entity(id).despawn()` queues the entity for destruction. The entity remains alive until the next flush.
- `iterWithEntity` provides the `EntityId` as the first callback parameter, which is needed to target specific entities with `cmd.entity(id)`.
