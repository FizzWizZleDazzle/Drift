# Queries

Queries are the primary way systems read and write component data. A query describes a set of component requirements and iterates over every entity that matches them.

Drift provides two query types:

| Type | Access | Callback receives |
|------|--------|-------------------|
| `Query<T1, T2, ...>` | Read-only | `const T1&, const T2&, ...` |
| `QueryMut<T1, T2, ...>` | Read-write | `T1&, T2&, ...` |

Both are used as system parameters and are automatically constructed by the engine when the system runs.

```cpp
#include <drift/Query.hpp>
```

## Basic iteration

Use `iter()` to iterate over all matching entities. The callback receives references to the requested components.

```cpp
void movementSystem(QueryMut<Transform2D, Velocity2D> query, Res<Time> time) {
    query.iter([&](Transform2D& transform, Velocity2D& vel) {
        transform.position.x += vel.linear.x * time->delta;
        transform.position.y += vel.linear.y * time->delta;
    });
}
```

Read-only queries use `const` references:

```cpp
void debugPrintPositions(Query<Transform2D> query) {
    query.iter([](const Transform2D& t) {
        Log::info("Position: {}, {}", t.position.x, t.position.y);
    });
}
```

## Iteration with entity ID

The `iter()` callback does **not** receive the entity ID. When you need it, use `iterWithEntity()` instead. The entity ID is always the first parameter.

```cpp
void tagLowHealth(QueryMut<Health> query, Commands& cmd) {
    query.iterWithEntity([&](EntityId id, Health& hp) {
        if (hp.current <= 0) {
            cmd.entity(id).despawn();
        }
    });
}
```

This distinction is intentional: `iter()` is the common case and avoids passing an unused ID through every callback invocation. Reach for `iterWithEntity()` only when you actually need the entity.

## Filters

Filters refine which entities a query matches without adding data columns to the callback. They appear alongside data components in the template argument list.

### With\<T\> -- require a component

`With<T>` requires the entity to have component `T` but does **not** yield it. This is useful for marker components or when you only need to know the component exists.

```cpp
struct Enemy {};  // marker component (empty struct)

void moveEnemies(QueryMut<Transform2D, With<Enemy>> query, Res<Time> time) {
    // Only entities that have both Transform2D and Enemy
    // Callback receives Transform2D& only -- Enemy is not yielded
    query.iter([&](Transform2D& t) {
        t.position.x -= 60.f * time->delta;
    });
}
```

### Without\<T\> -- exclude a component

`Without<T>` excludes entities that have component `T`.

```cpp
struct Invincible {};

void applyDamage(QueryMut<Health, Without<Invincible>> query) {
    // Skips any entity that has the Invincible component
    query.iter([](Health& hp) {
        hp.current -= 1;
    });
}
```

### Optional\<T\> -- maybe present

`Optional<T>` does not filter entities. Instead, the callback receives a pointer (`T*`) that is `nullptr` when the component is absent.

```cpp
void renderSprites(Query<Transform2D, Sprite, Optional<Tint>> query) {
    query.iter([](const Transform2D& t, const Sprite& sprite, const Tint* tint) {
        Color color = tint ? tint->color : Color{255, 255, 255, 255};
        // draw sprite at t.position with color...
    });
}
```

With a read-write query (`QueryMut`), optional components yield a mutable pointer:

```cpp
void healSystem(QueryMut<Health, Optional<Shield>> query) {
    query.iter([](Health& hp, Shield* shield) {
        hp.current += 1;
        if (shield) {
            shield->durability -= 0.1f;
        }
    });
}
```

### Changed\<T\> -- only changed this tick

`Changed<T>` filters to entities whose component `T` was modified during the current tick. The component is **not** yielded (it behaves like `With<T>` for data purposes).

```cpp
void onHealthChanged(Query<Health, Changed<Health>> query) {
    // Only iterates entities whose Health was written to this tick
    query.iter([](const Health& hp) {
        Log::info("Health changed to {}", hp.current);
    });
}
```

### Added\<T\> -- only added this tick

`Added<T>` filters to entities that had component `T` added during the current tick.

```cpp
void onSpawned(Query<Transform2D, Added<Transform2D>> query) {
    query.iter([](const Transform2D& t) {
        Log::info("New entity at ({}, {})", t.position.x, t.position.y);
    });
}
```

Both `Changed<T>` and `Added<T>` are evaluated per-entity during iteration, so they work correctly even when combined with other filters.

## Combining filters

Filters and data components can be freely mixed. Each template argument is either a data component (yielded to the callback) or a filter (affects matching but is not yielded).

```cpp
void updateVisibleEnemies(
    QueryMut<Transform2D, Velocity2D, With<Enemy>, Without<Hidden>, Optional<Armor>> query,
    Res<Time> time
) {
    // Matches entities with Transform2D + Velocity2D + Enemy, but NOT Hidden.
    // Armor is optional and yields Armor* (may be nullptr).
    query.iter([&](Transform2D& t, Velocity2D& v, Armor* armor) {
        float speed = armor ? 30.f : 60.f;
        t.position.x += v.linear.x * speed * time->delta;
        t.position.y += v.linear.y * speed * time->delta;
    });
}
```

The callback parameter order matches the order of the **data** components in the template list. Filters (`With`, `Without`, `Changed`, `Added`) contribute no callback parameters and can appear anywhere in the list.

## Marker components

Marker components are empty structs used purely for tagging entities. Combined with `With<T>` and `Without<T>`, they provide a lightweight filtering mechanism without allocating per-entity storage.

```cpp
struct Player {};
struct Friendly {};

void setupPlayer(Commands& cmd) {
    cmd.spawn()
        .insert(Transform2D{.position = {100, 200}},
                Sprite{},
                Player{},
                Friendly{});
}

void findPlayer(Query<Transform2D, With<Player>> query) {
    query.iter([](const Transform2D& t) {
        // Only the entity with the Player marker matches
    });
}
```

Marker components are auto-registered when they first appear in a query, so you do not need to register them manually.

## Single-entity queries

Use `single()` when you expect exactly one entity to match. It returns the component data directly (no callback needed). If zero or more than one entity matches, the engine asserts.

```cpp
void cameraFollow(Query<Transform2D, With<Player>> player,
                  QueryMut<Transform2D, With<Camera2D>> camera) {
    // single() returns const Transform2D& for a one-component read-only query
    auto playerPos = player.single();

    // single() returns Transform2D& for a one-component read-write query
    auto& camTransform = camera.single();
    camTransform.position = playerPos.position;
}
```

When a query has multiple data components, `single()` returns a `std::tuple`:

```cpp
void inspectPlayer(Query<Transform2D, Health, With<Player>> query) {
    auto [transform, health] = query.single();
    Log::info("Player at ({}, {}) with {} HP",
              transform.position.x, transform.position.y, health.current);
}
```

## Checking for entities

### contains(EntityId)

Check whether a specific entity matches the query's requirements:

```cpp
void collisionSystem(EventReader<CollisionStart> collisions,
                     Query<Asteroid> asteroids,
                     Query<Bullet> bullets,
                     Commands& cmd) {
    for (auto& event : collisions) {
        if (asteroids.contains(event.entityA) && bullets.contains(event.entityB)) {
            cmd.entity(event.entityA).despawn();
            cmd.entity(event.entityB).despawn();
        }
    }
}
```

`contains()` checks that the entity is alive and has all required components (including filter requirements). It does **not** iterate.

### isEmpty()

Check whether the query matches zero entities:

```cpp
void checkGameOver(Query<Health, With<Player>> players,
                   ResMut<NextState<GamePhase>> next) {
    if (players.isEmpty()) {
        next->set(GamePhase::GameOver);
    }
}
```

## Auto-registration

Queries automatically register any component types that have not been explicitly registered with the world. This means you can introduce a new component type simply by using it in a query or `Commands::insert()` without a separate registration step. The engine will never silently return zero results for an unregistered type.

## Summary

| Method | Description |
|--------|-------------|
| `iter(fn)` | Iterate all matches; callback gets component refs |
| `iterWithEntity(fn)` | Iterate all matches; callback gets `EntityId` then component refs |
| `single()` | Assert exactly one match and return its data |
| `contains(EntityId)` | Check if a specific entity matches |
| `isEmpty()` | Check if zero entities match |

| Filter | Effect |
|--------|--------|
| `With<T>` | Entity must have `T`; not yielded |
| `Without<T>` | Entity must not have `T` |
| `Optional<T>` | Yields `T*` (nullptr if absent); does not filter |
| `Changed<T>` | Only entities whose `T` changed this tick; not yielded |
| `Added<T>` | Only entities whose `T` was added this tick; not yielded |
