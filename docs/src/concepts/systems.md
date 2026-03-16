# Systems

Systems are free functions that contain your game logic. They are registered with the `App`, assigned to a phase, and called every frame (or once, for startup systems). The engine automatically injects their parameters -- resources, queries, commands, and events -- based on the function signature.

## Typed System Functions

A system is any free function whose parameters are drawn from the supported system parameter types:

| Parameter Type | Description |
|----------------|-------------|
| `Res<T>` | Read-only access to resource `T` |
| `ResMut<T>` | Read-write access to resource `T` |
| `Query<T...>` | Read-only query over components |
| `QueryMut<T...>` | Read-write query over components |
| `Commands&` | Deferred entity/component mutations |
| `EventWriter<T>` | Send events of type `T` |
| `EventReader<T>` | Read events of type `T` from the previous frame |

The engine inspects the function signature at compile time, extracts the parameter types, builds the dependency list for the parallel scheduler, and constructs each parameter at call time.

```cpp
void movementSystem(Res<Time> time,
                    Res<InputResource> input,
                    QueryMut<Transform2D, Speed> movers) {
    float dt = time->delta;
    movers.iter([&](Transform2D& t, Speed& s) {
        if (input->keyHeld(Key::Right)) t.position.x += s.value * dt;
        if (input->keyHeld(Key::Left))  t.position.x -= s.value * dt;
    });
}
```

Register it with `addSystem`:

```cpp
app.addSystem<movementSystem>("movement", Phase::Update);
```

The template parameter is the function pointer itself (not the type). The string name is used for ordering constraints and debug output.

## Phases

Every system runs in a specific phase. Phases execute in a fixed order each frame:

| Phase | When | Parallelism |
|-------|------|-------------|
| `Startup` | Once, before the main loop | Sequential |
| `PreUpdate` | Start of frame, before input poll | Parallel |
| `Update` | Main game logic | Parallel |
| `PostUpdate` | After game logic (physics sync, hierarchy) | Parallel |
| `Extract` | Build render snapshot from ECS data | Sequential |
| `Render` | GPU draw calls | Sequential |
| `RenderFlush` | Present frame to screen | Sequential |

Commands are flushed (applied to the world) after `PreUpdate`, `Update`, and `PostUpdate`.

## Phase Shortcuts

For the most common phases, `App` provides convenience methods that default the system name:

```cpp
app.startup<setupGame>();                      // Phase::Startup, name = "startup"
app.update<gameLoop>();                        // Phase::Update,  name = "update"
app.render<drawHud>();                         // Phase::Render,  name = "render"
app.preUpdate<earlyWork>();                    // Phase::PreUpdate
app.postUpdate<lateWork>();                    // Phase::PostUpdate
```

You can also provide a custom name:

```cpp
app.startup<setupGame>("setup_game");
app.update<gameLoop>("game_loop");
```

All shortcuts return a `SystemHandle` for chaining ordering constraints (see below).

## Ordering: before() and after()

By default, systems within the same phase have no guaranteed execution order. Use `.before()` and `.after()` to establish explicit ordering:

```cpp
app.update<spawnEnemies>("spawn_enemies");
app.update<moveEnemies>("move_enemies").after("spawn_enemies");
app.update<collisionCheck>("collision_check").after("move_enemies");
app.update<renderParticles>("render_particles").before("collision_check");
```

Ordering is by name. The scheduler respects these constraints both in sequential and parallel modes.

You can chain multiple ordering constraints:

```cpp
app.update<damageSystem>("damage")
    .after("collision_check")
    .after("projectile_update")
    .before("cleanup");
```

## RunConditions

A `RunCondition` is a predicate that gates whether a system executes on a given frame. If the condition returns `false`, the system is skipped entirely.

```cpp
using RunCondition = std::function<bool(const App&)>;
```

Pass a RunCondition as the third argument to a phase shortcut or `addSystem`:

```cpp
// Only run when the game is in the Playing state
app.update<gameLoop>("game_loop", inState(GameMode::Playing));

// Custom condition: only run when score is above a threshold
app.update<bossSpawn>("boss_spawn", [](const App& app) -> bool {
    auto* state = app.getResource<GameState>();
    return state && state->score >= 1000;
});
```

### inState()

The built-in `inState<T>(value)` helper creates a RunCondition that checks whether `State<T>::current` equals `value`:

```cpp
enum class GameMode { Menu, Playing, Paused, GameOver };

app.initState(GameMode::Menu);

// These systems only run during the Playing state
app.update<playerMovement>("player_movement", inState(GameMode::Playing));
app.update<enemyAI>("enemy_ai", inState(GameMode::Playing));

// This system only runs in the Menu state
app.update<menuLogic>("menu_logic", inState(GameMode::Menu));
```

## System Sets

System sets let you group systems and control the relative order of groups within a phase. Define a set as an enum, configure the set order, then assign systems to sets.

```cpp
enum class GameSet : int {
    Input = 0,
    Physics = 1,
    Rendering = 2,
};

// Declare the order: Input runs before Physics, Physics before Rendering
app.configureSets(Phase::Update, {GameSet::Input, GameSet::Physics, GameSet::Rendering});

// Assign systems to sets
app.update<readInput>("read_input").inSet(static_cast<SystemSetId>(GameSet::Input));
app.update<applyForces>("apply_forces").inSet(static_cast<SystemSetId>(GameSet::Physics));
app.update<resolveCollisions>("resolve_collisions").inSet(static_cast<SystemSetId>(GameSet::Physics));
app.update<updateSprites>("update_sprites").inSet(static_cast<SystemSetId>(GameSet::Rendering));
```

Systems within the same set maintain their registration order (stable sort). Systems without a set assignment are placed after all ordered sets.

`configureSets()` accepts an initializer list of any enum type and casts each value to `SystemSetId` (which is `int`).

## Batch Registration

Register multiple systems in a single call using `addSystems()`:

```cpp
app.addSystems(Phase::Update,
    App::System<spawnEnemies>{"spawn_enemies"},
    App::System<moveEnemies>{"move_enemies"},
    App::System<collisionCheck>{"collision_check"},
    App::System<cleanupDead>{"cleanup_dead"});
```

Each `App::System<fn>` takes a name string. The systems are registered in order. Batch registration does not return handles, so use it when you do not need `.before()`/`.after()` chaining. If you need ordering, register systems individually.

## Lambda Systems

For internal plugin use or quick prototyping, you can register a lambda that receives `App&`:

```cpp
app.addSystem("my_lambda_system", Phase::Update, [](App& app) {
    auto* time = app.getResource<Time>();
    auto* state = app.getResource<GameState>();
    state->elapsed += time->delta;
});
```

Lambda systems are always exclusive in the parallel scheduler -- they cannot run concurrently with other systems because the engine cannot analyze their resource access at compile time. Prefer typed system functions for production code.

## Parallel Scheduler

The engine automatically parallelizes systems within the PreUpdate, Update, and PostUpdate phases. The scheduler works as follows:

1. For each phase, it builds batches of non-conflicting systems.
2. Two systems conflict if they access any overlapping type where at least one access is a write.
3. Non-conflicting systems in a batch run simultaneously on the thread pool.
4. Lambda systems and systems with no tracked dependencies are marked exclusive and always run alone.
5. Ordering constraints (`.before()`/`.after()`) are respected: a system will not be batched until its predecessors complete.

The scheduler uses the function signature to determine access patterns:

- `Res<T>` declares a read on resource `T`
- `ResMut<T>` declares a write on resource `T`
- `Query<A, B>` declares reads on components `A` and `B`
- `QueryMut<A, B>` declares writes on components `A` and `B`
- `EventReader<T>` declares a read on `Events<T>`
- `EventWriter<T>` declares a write on `Events<T>`
- `Commands&` does not participate in conflict detection (each thread gets its own command buffer)

**Example:** If system A uses `Res<Time>` and `QueryMut<Transform2D>`, and system B uses `Res<Time>` and `Query<Health>`, they do not conflict (shared read on `Time`, disjoint component access) and can run in parallel. If system C uses `QueryMut<Transform2D>` and `QueryMut<Health>`, it conflicts with both A (overlapping write on `Transform2D`) and B (overlapping write on `Health`).

Each worker thread gets its own `Commands` buffer. After a parallel batch completes, all per-thread command buffers are flushed in order.

### Controlling Thread Count

Set `Config::threadCount` to control parallelism:

```cpp
app.setConfig({
    .threadCount = 0,   // auto: hardware_concurrency - 1, capped at 7
});

app.setConfig({
    .threadCount = 1,   // sequential: no threading at all
});

app.setConfig({
    .threadCount = 4,   // exactly 4 threads (3 workers + main)
});
```

## Complete Example

```cpp
#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/Query.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/Time.hpp>

using namespace drift;

struct Velocity { Vec2 v; };
struct Gravity  { float g = 200.f; };
struct PlayerTag {};

struct GameState : public Resource {
    DRIFT_RESOURCE(GameState)
    EntityId camera = InvalidEntityId;
};

void setup(ResMut<GameState> game, Commands& cmd) {
    game->camera = cmd.spawn()
        .insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});

    cmd.spawn().insert(Transform2D{.position = {0, 0}},
                       Sprite{}, Velocity{}, Gravity{}, PlayerTag{});
}

void inputSystem(Res<InputResource> input,
                 QueryMut<Velocity, With<PlayerTag>> players) {
    players.iter([&](Velocity& vel) {
        vel.v = {};
        if (input->keyHeld(Key::Right)) vel.v.x = 200.f;
        if (input->keyHeld(Key::Left))  vel.v.x = -200.f;
        if (input->keyHeld(Key::Space)) vel.v.y = -400.f;
    });
}

void gravitySystem(Res<Time> time, QueryMut<Velocity, Gravity> bodies) {
    float dt = time->delta;
    bodies.iter([&](Velocity& vel, Gravity& g) {
        vel.v.y += g.g * dt;
    });
}

void moveSystem(Res<Time> time, QueryMut<Transform2D, Velocity> movers) {
    float dt = time->delta;
    movers.iter([&](Transform2D& t, Velocity& vel) {
        t.position += vel.v * dt;
    });
}

int main() {
    App app;
    app.setConfig({.title = "Systems Demo"});
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameState>();

    app.startup<setup>("setup");

    // Input must run before gravity and movement
    app.update<inputSystem>("input").before("gravity");
    app.update<gravitySystem>("gravity").before("movement");
    app.update<moveSystem>("movement");

    return app.run();
}
```

In this example, `inputSystem` and `gravitySystem` both write to `Velocity`, so they conflict and cannot run in parallel. The `.before()` constraints ensure they run in the correct order: input, then gravity, then movement. If you had a separate system that only read `Res<Time>` and `Query<Health>`, it could run in parallel with any of these.
