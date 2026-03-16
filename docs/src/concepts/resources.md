# Resources

Resources are singleton data shared across all systems. Unlike components, which are attached to individual entities, a resource exists once in the entire application and is accessed by type. Engine subsystems (input, rendering, audio, physics) are all resources, and you can define your own for game-specific global state.

```cpp
#include <drift/Resource.hpp>
```

## Defining a resource

A resource is any class that inherits from `Resource`. Use the `DRIFT_RESOURCE(Name)` macro to provide a debug name (used in logging and dev tools).

```cpp
struct GameState : public Resource {
    DRIFT_RESOURCE(GameState)

    int score = 0;
    int lives = 3;
    float elapsedTime = 0.f;
};
```

The macro expands to a `name()` override that returns the stringified type name. Without it, the resource still works but reports an empty name in debug output.

Resources can hold any data: containers, handles, configuration values, counters. They follow normal C++ lifetime rules and are owned by the `App`.

## Registering a resource

### addResource

`addResource<T>(args...)` creates a new resource of type `T`, forwarding any constructor arguments. It returns a pointer to the created resource. Calling `addResource` for a type that already exists is an error.

```cpp
App app;
app.addResource<GameState>();
```

With constructor arguments:

```cpp
struct Difficulty : public Resource {
    DRIFT_RESOURCE(Difficulty)

    float enemySpeed;
    int maxEnemies;

    Difficulty(float speed, int max)
        : enemySpeed(speed), maxEnemies(max) {}
};

app.addResource<Difficulty>(120.f, 50);
```

### initResource

`initResource<T>(args...)` is the idempotent version. If the resource already exists, it returns a pointer to the existing instance and does nothing else. If it does not exist, it creates it. This is the preferred choice inside plugins where multiple plugins might depend on the same resource.

```cpp
// Safe to call multiple times -- only the first call creates the resource
app.initResource<GameState>();
app.initResource<GameState>();  // no-op, returns existing
```

### getResource

`getResource<T>()` retrieves a pointer to an existing resource. Returns `nullptr` if the resource has not been registered.

```cpp
GameState* state = app.getResource<GameState>();
if (state) {
    Log::info("Score: {}", state->score);
}
```

## Accessing resources in systems

Systems do not call `getResource` directly. Instead, they declare resource parameters using the `Res<T>` and `ResMut<T>` wrappers, and the engine injects them automatically.

### Res\<T\> -- read-only access

```cpp
void displayScore(Res<GameState> game) {
    Log::info("Score: {}", game->score);
}
```

`Res<T>` provides `operator->()` and `operator*()` that return `const T*` and `const T&` respectively. You cannot modify the resource through a `Res<T>`.

### ResMut\<T\> -- read-write access

```cpp
void addScore(ResMut<GameState> game) {
    game->score += 10;
}
```

`ResMut<T>` provides mutable access through `operator->()` and `operator*()`.

### Combining with other parameters

Resources can be mixed freely with queries, commands, event readers/writers, and other resources in the same system signature.

```cpp
void gameplaySystem(Res<Time> time,
                    Res<InputResource> input,
                    ResMut<GameState> game,
                    QueryMut<Transform2D, With<Player>> players,
                    Commands& cmd) {
    game->elapsedTime += time->delta;

    if (input->keyPressed(Key::Space)) {
        game->score += 1;
    }

    players.iter([&](Transform2D& t) {
        t.position.y -= 100.f * time->delta;
    });
}
```

## The Time resource

`Time` is a built-in resource registered by `DefaultPlugins`. It provides frame timing information and is the standard way to get delta time in systems.

```cpp
#include <drift/resources/Time.hpp>
```

| Field | Type | Description |
|-------|------|-------------|
| `delta` | `float` | Seconds elapsed since the previous frame |
| `elapsed` | `double` | Seconds elapsed since the engine started |
| `frame` | `uint64_t` | Frame counter (increments by 1 each frame) |

Systems should **never** take `float dt` as a parameter. Use `Res<Time>` instead:

```cpp
void movePlayer(Res<Time> time, Res<InputResource> input,
                QueryMut<Transform2D, With<Player>> query) {
    float speed = 200.f;
    query.iter([&](Transform2D& t) {
        if (input->keyDown(Key::Right)) t.position.x += speed * time->delta;
        if (input->keyDown(Key::Left))  t.position.x -= speed * time->delta;
        if (input->keyDown(Key::Up))    t.position.y -= speed * time->delta;
        if (input->keyDown(Key::Down))  t.position.y += speed * time->delta;
    });
}
```

### Frame-rate-independent timers

Use `elapsed` for timers that survive frame rate fluctuations:

```cpp
void spawnWave(Res<Time> time, ResMut<GameState> game, Commands& cmd) {
    if (time->elapsed - game->lastWaveTime > 5.0) {
        game->lastWaveTime = time->elapsed;
        // spawn a new enemy wave...
    }
}
```

Use `frame` for logic that should happen every N frames:

```cpp
void periodicLog(Res<Time> time, Res<GameState> game) {
    if (time->frame % 300 == 0) {
        Log::info("Frame {}: score={}", time->frame, game->score);
    }
}
```

## Practical example: a cooldown timer

A complete example showing a custom resource used for ability cooldown management.

```cpp
struct AbilityCooldowns : public Resource {
    DRIFT_RESOURCE(AbilityCooldowns)

    float fireball = 0.f;
    float dash = 0.f;

    void tick(float dt) {
        fireball = std::max(0.f, fireball - dt);
        dash = std::max(0.f, dash - dt);
    }

    bool canFireball() const { return fireball <= 0.f; }
    bool canDash() const { return dash <= 0.f; }
};

void cooldownSystem(Res<Time> time, ResMut<AbilityCooldowns> cooldowns) {
    cooldowns->tick(time->delta);
}

void abilitySystem(Res<InputResource> input,
                   ResMut<AbilityCooldowns> cooldowns,
                   QueryMut<Transform2D, With<Player>> players,
                   Commands& cmd) {
    if (input->keyPressed(Key::Q) && cooldowns->canFireball()) {
        cooldowns->fireball = 2.5f;  // 2.5s cooldown
        players.iterWithEntity([&](EntityId id, Transform2D& t) {
            cmd.spawn()
                .insert(Transform2D{.position = t.position},
                        Velocity2D{.linear = {300.f, 0.f}},
                        Sprite{});
        });
    }
}

int main() {
    App app;
    app.addPlugins<DefaultPlugins>();
    app.addResource<AbilityCooldowns>();
    app.update<cooldownSystem>("cooldown_tick");
    app.update<abilitySystem>("ability_input");
    return app.run();
}
```

## Summary

| API | Description |
|-----|-------------|
| `app.addResource<T>(args...)` | Create a new resource (asserts it does not already exist) |
| `app.initResource<T>(args...)` | Create if absent, return existing if present (idempotent) |
| `app.getResource<T>()` | Retrieve a pointer, or `nullptr` if not registered |
| `Res<T>` | Read-only system parameter (injects `const T*`) |
| `ResMut<T>` | Read-write system parameter (injects `T*`) |
| `DRIFT_RESOURCE(Name)` | Macro that overrides `name()` for debug output |
