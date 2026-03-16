# Events

Events provide a way for systems to communicate without directly referencing each other. One system sends an event, and any number of other systems can read it on the following frame. Drift uses a double-buffered model: events sent during frame N become readable during frame N+1.

```cpp
#include <drift/Events.hpp>
```

## Defining a custom event

An event is any plain struct. It does not need to inherit from anything.

```cpp
struct ScoreChanged {
    int oldScore;
    int newScore;
};
```

```cpp
struct EnemyKilled {
    EntityId enemy;
    Vec2 position;
    int pointValue;
};
```

## Sending events with EventWriter

Declare an `EventWriter<T>` parameter in your system to send events of type `T`. The `Events<T>` resource is automatically registered the first time an `EventWriter<T>` or `EventReader<T>` appears in a system signature.

```cpp
void scoringSystem(ResMut<GameState> game, EventWriter<ScoreChanged> writer) {
    int oldScore = game->score;
    game->score += 10;
    writer.send(ScoreChanged{oldScore, game->score});
}
```

You can send multiple events per frame:

```cpp
void combatSystem(QueryMut<Health, With<Enemy>> enemies,
                  EventWriter<EnemyKilled> kills,
                  Commands& cmd) {
    enemies.iterWithEntity([&](EntityId id, Health& hp) {
        if (hp.current <= 0) {
            kills.send(EnemyKilled{.enemy = id, .position = {}, .pointValue = 100});
            cmd.entity(id).despawn();
        }
    });
}
```

## Reading events with EventReader

Declare an `EventReader<T>` parameter in your system to consume events. Events are available the frame **after** they are sent (double-buffered).

```cpp
void scoreDisplay(EventReader<ScoreChanged> events) {
    for (auto& e : events) {
        Log::info("Score: {} -> {}", e.oldScore, e.newScore);
    }
}
```

`EventReader<T>` supports range-based for loops and provides these methods:

| Method | Description |
|--------|-------------|
| `events()` | Returns `const std::vector<T>&` of buffered events |
| `empty()` | Returns `true` if no events are available this frame |
| `begin()` / `end()` | Iterators for range-based for loops |

### Checking before iterating

```cpp
void onKill(EventReader<EnemyKilled> kills, ResMut<GameState> game) {
    if (kills.empty()) return;

    for (auto& e : kills) {
        game->score += e.pointValue;
    }
}
```

## Double-buffering explained

Events use two internal buffers:

1. **Current buffer** -- receives `send()` calls during the current frame.
2. **Previous buffer** -- holds events from the last frame, readable via `EventReader`.

At the start of each frame, the engine swaps the buffers: the current buffer becomes the previous buffer (now readable), and the current buffer is cleared for new events.

This means:

- Events sent in frame 5 are readable in frame 6.
- Events last exactly one frame of readability. If no system reads them in frame 6, they are discarded when frame 7 starts.
- Multiple writers can send to the same event type in the same frame. All their events are combined into one buffer.

The one-frame delay is a deliberate design choice. It decouples the sending and reading systems, making execution order within a frame irrelevant for event delivery.

## Explicit registration with initEvent

`EventWriter<T>` and `EventReader<T>` auto-register `Events<T>` on first use. If you need the event resource to exist before any system runs (for example, during plugin setup), call `initEvent<T>()` explicitly:

```cpp
app.initEvent<ScoreChanged>();
app.initEvent<EnemyKilled>();
```

`initEvent` is idempotent -- calling it multiple times for the same type is safe.

## Built-in collision events

The physics subsystem sends four event types automatically when collisions occur. These are available when `DefaultPlugins` (which includes the physics plugin) is registered.

### CollisionStart / CollisionEnd

Sent when two rigid bodies begin or cease contact.

```cpp
struct CollisionStart {
    EntityId entityA;
    EntityId entityB;
};

struct CollisionEnd {
    EntityId entityA;
    EntityId entityB;
};
```

```cpp
void onCollision(EventReader<CollisionStart> collisions,
                 Query<Bullet> bullets,
                 Query<Wall> walls,
                 Commands& cmd) {
    for (auto& event : collisions) {
        if (bullets.contains(event.entityA) && walls.contains(event.entityB)) {
            cmd.entity(event.entityA).despawn();
        } else if (bullets.contains(event.entityB) && walls.contains(event.entityA)) {
            cmd.entity(event.entityB).despawn();
        }
    }
}
```

### SensorStart / SensorEnd

Sent when an entity enters or exits a sensor volume. Sensors are colliders with `isSensor = true` -- they detect overlap but do not produce physical contact forces.

```cpp
struct SensorStart {
    EntityId sensorEntity;
    EntityId visitorEntity;
};

struct SensorEnd {
    EntityId sensorEntity;
    EntityId visitorEntity;
};
```

```cpp
void pickupSystem(EventReader<SensorStart> sensors,
                  Query<Coin> coins,
                  Query<Transform2D, With<Player>> players,
                  ResMut<GameState> game,
                  Commands& cmd) {
    for (auto& event : sensors) {
        // Check if a player entered a coin's sensor
        if (coins.contains(event.sensorEntity) &&
            players.contains(event.visitorEntity)) {
            game->score += 1;
            cmd.entity(event.sensorEntity).despawn();
        }
    }
}
```

### Identifying collision participants

A common pattern is to use `Query::contains()` to identify which entity is which in a collision pair:

```cpp
void collisionHandler(EventReader<CollisionStart> collisions,
                      Query<Asteroid> asteroids,
                      Query<Bullet> bullets,
                      Commands& cmd) {
    for (auto& event : collisions) {
        EntityId asteroidId = InvalidEntityId;
        EntityId bulletId = InvalidEntityId;

        if (asteroids.contains(event.entityA) && bullets.contains(event.entityB)) {
            asteroidId = event.entityA;
            bulletId = event.entityB;
        } else if (asteroids.contains(event.entityB) && bullets.contains(event.entityA)) {
            asteroidId = event.entityB;
            bulletId = event.entityA;
        }

        if (asteroidId != InvalidEntityId) {
            cmd.entity(asteroidId).despawn();
            cmd.entity(bulletId).despawn();
        }
    }
}
```

## Complete example: damage numbers

A full example showing custom events flowing from a combat system to a visual feedback system.

```cpp
// --- Event definition ---
struct DamageDealt {
    EntityId target;
    Vec2 position;
    int amount;
};

// --- Sending system ---
void attackSystem(QueryMut<Health> enemies,
                  Query<Transform2D> transforms,
                  EventWriter<DamageDealt> damageEvents) {
    enemies.iterWithEntity([&](EntityId id, Health& hp) {
        int damage = 10;
        hp.current -= damage;

        // Look up position for the visual effect
        transforms.iterWithEntity([&](EntityId tid, const Transform2D& t) {
            if (tid == id) {
                damageEvents.send(DamageDealt{
                    .target = id,
                    .position = t.position,
                    .amount = damage
                });
            }
        });
    });
}

// --- Receiving system (runs next frame) ---
void damageNumberSystem(EventReader<DamageDealt> damages, Commands& cmd) {
    for (auto& e : damages) {
        // Spawn floating damage number at the impact position
        cmd.spawn()
            .insert(Transform2D{.position = {e.position.x, e.position.y - 20.f}},
                    Sprite{},
                    DamageNumber{.value = e.amount, .lifetime = 1.f});
    }
}

// --- Registration ---
int main() {
    App app;
    app.addPlugins<DefaultPlugins>();
    // Events<DamageDealt> is auto-registered by EventWriter/EventReader,
    // but you can be explicit if you prefer:
    app.initEvent<DamageDealt>();

    app.update<attackSystem>("attack");
    app.update<damageNumberSystem>("damage_numbers");
    return app.run();
}
```

## Summary

| API | Description |
|-----|-------------|
| `EventWriter<T>` | System parameter for sending events |
| `EventReader<T>` | System parameter for reading events from the previous frame |
| `writer.send(event)` | Queue an event for the next frame |
| `reader.events()` | Get the vector of events from last frame |
| `reader.empty()` | Check if there are no events to read |
| `app.initEvent<T>()` | Explicitly register an event type (idempotent) |

| Built-in Event | Fields | Description |
|----------------|--------|-------------|
| `CollisionStart` | `entityA`, `entityB` | Two rigid bodies began contact |
| `CollisionEnd` | `entityA`, `entityB` | Two rigid bodies stopped contact |
| `SensorStart` | `sensorEntity`, `visitorEntity` | An entity entered a sensor volume |
| `SensorEnd` | `sensorEntity`, `visitorEntity` | An entity left a sensor volume |
