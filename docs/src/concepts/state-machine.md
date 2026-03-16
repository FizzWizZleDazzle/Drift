# State Machine

The state machine lets you organize your game into distinct phases -- menus, gameplay, pause screens, cutscenes -- and run different systems depending on which state is active. States are enums. The engine tracks the current value, handles transitions, and fires enter/exit callbacks automatically.

```cpp
#include <drift/App.hpp>   // includes State.hpp
```

## Defining a state enum

A state is any C++ enum (or `enum class`). Each enumerator represents one possible state.

```cpp
enum class GamePhase {
    Menu,
    Playing,
    Paused,
    GameOver,
};
```

## Initializing state

Call `initState<T>(initial)` during setup to register the state type with an initial value. This creates two resources internally:

- `State<T>` -- holds the current state value (`state->current`).
- `NextState<T>` -- a write handle for requesting transitions (`next->set(value)`).

```cpp
int main() {
    App app;
    app.addPlugins<DefaultPlugins>();
    app.initState<GamePhase>(GamePhase::Menu);
    // ...
    return app.run();
}
```

The initial state's `onEnter` callbacks run before the first frame.

## Conditional systems with inState

The `inState(value)` function returns a `RunCondition` -- a predicate checked before every frame. If the condition is false, the system is skipped entirely. Use it to bind systems to specific states.

```cpp
void menuUpdate(Res<InputResource> input, ResMut<NextState<GamePhase>> next) {
    if (input->keyPressed(Key::Return)) {
        next->set(GamePhase::Playing);
    }
}

void gameplayUpdate(Res<Time> time, ResMut<GameState> game,
                    QueryMut<Transform2D, Velocity2D> movers) {
    movers.iter([&](Transform2D& t, Velocity2D& v) {
        t.position.x += v.linear.x * time->delta;
        t.position.y += v.linear.y * time->delta;
    });
    game->elapsed += time->delta;
}

void pauseUpdate(Res<InputResource> input, ResMut<NextState<GamePhase>> next) {
    if (input->keyPressed(Key::Escape)) {
        next->set(GamePhase::Playing);  // unpause
    }
}

// Registration -- each system only runs in its associated state
app.update<menuUpdate>("menu_update", inState(GamePhase::Menu));
app.update<gameplayUpdate>("gameplay_update", inState(GamePhase::Playing));
app.update<pauseUpdate>("pause_update", inState(GamePhase::Paused));
```

Systems without a `RunCondition` run every frame regardless of the current state. This is useful for systems that should always be active (rendering, audio mixing, etc.).

## Triggering state transitions

To change state, write to `ResMut<NextState<T>>` from any system:

```cpp
void gameplayInput(Res<InputResource> input, ResMut<NextState<GamePhase>> next) {
    if (input->keyPressed(Key::Escape)) {
        next->set(GamePhase::Paused);
    }
}
```

Transitions are **not** immediate. The engine processes them between the event-poll phase and the Update phase:

1. The system calls `next->set(GamePhase::Paused)`.
2. The current frame's remaining systems finish execution.
3. Before the next Update phase, the engine detects the pending transition.
4. `onExit` callbacks for the old state run.
5. Commands are flushed.
6. `State<T>::current` is updated to the new value.
7. `onEnter` callbacks for the new state run.
8. Commands are flushed again.
9. The Update phase begins with the new state active.

This guarantees that all enter/exit logic and deferred commands are fully resolved before normal systems see the new state.

## onEnter callbacks

Register a system to run once when a specific state becomes active.

```cpp
void onEnterMenu(ResMut<GameState> game, ResMut<AudioResource> audio) {
    game->score = 0;
    game->lives = 3;
    audio->playMusic("menu_theme.ogg");
}

void onEnterPlaying(ResMut<AudioResource> audio) {
    audio->playMusic("gameplay_theme.ogg");
}

app.onEnter<onEnterMenu>(GamePhase::Menu, "enter_menu");
app.onEnter<onEnterPlaying>(GamePhase::Playing, "enter_playing");
```

`onEnter` systems are full typed systems -- they can declare any combination of `Res<T>`, `ResMut<T>`, `Query<T>`, `Commands&`, and other system parameters.

The `onEnter` callback for the initial state (the value passed to `initState`) runs before the first frame, so you can use it to set up the initial scene.

## onExit callbacks

Register a system to run once when leaving a specific state.

```cpp
void onExitPlaying(QueryMut<Transform2D, With<Enemy>> enemies, Commands& cmd) {
    // Despawn all enemies when leaving gameplay
    enemies.iterWithEntity([&](EntityId id, Transform2D&) {
        cmd.entity(id).despawn();
    });
}

void onExitPaused(ResMut<AudioResource> audio) {
    audio->resumeMusic();
}

app.onExit<onExitPlaying>(GamePhase::Playing, "exit_playing");
app.onExit<onExitPaused>(GamePhase::Paused, "exit_paused");
```

## Reading the current state

Any system can read the current state through `Res<State<T>>`:

```cpp
void renderUI(Res<State<GamePhase>> state, Res<GameState> game) {
    if (state->current == GamePhase::Menu) {
        // draw "Press Enter to Start"
    } else if (state->current == GamePhase::Playing) {
        // draw score HUD
    } else if (state->current == GamePhase::GameOver) {
        // draw "Game Over" with final score
    }
}
```

This is useful for systems that run unconditionally (such as a render system) but need to vary their behavior based on the active state.

## Multiple state types

You can register multiple independent state enums. Each one gets its own `State<T>` / `NextState<T>` pair and its own set of enter/exit callbacks.

```cpp
enum class GamePhase { Menu, Playing, GameOver };
enum class AudioMode { Normal, Muted, LowVolume };

app.initState<GamePhase>(GamePhase::Menu);
app.initState<AudioMode>(AudioMode::Normal);

app.update<muteToggle>("mute_toggle", inState(AudioMode::Muted));
app.onEnter<onMuted>(AudioMode::Muted, "enter_muted");
```

## Complete example: game with menu, play, and game-over

```cpp
#include <drift/App.hpp>
#include <drift/plugins/DefaultPlugins.hpp>

using namespace drift;

// --- State enum ---
enum class GamePhase { Menu, Playing, GameOver };

// --- Game resource ---
struct GameData : public Resource {
    DRIFT_RESOURCE(GameData)
    int score = 0;
    EntityId playerId = InvalidEntityId;
};

// --- Components ---
struct Player {};
struct Enemy {};

// --- OnEnter systems ---
void enterMenu(ResMut<GameData> game) {
    game->score = 0;
    game->playerId = InvalidEntityId;
    Log::info("=== MAIN MENU ===");
}

void enterPlaying(ResMut<GameData> game, Commands& cmd) {
    auto player = cmd.spawn()
        .insert(Transform2D{.position = {400, 300}},
                Sprite{},
                Player{},
                RigidBody2D{.type = BodyType::Dynamic},
                BoxCollider2D{.halfSize = {16, 16}});
    // Store the player entity for later reference
    Log::info("=== GAME START ===");
}

void enterGameOver(Res<GameData> game) {
    Log::info("=== GAME OVER === Score: {}", game->score);
}

// --- OnExit systems ---
void exitPlaying(QueryMut<Transform2D, With<Enemy>> enemies, Commands& cmd) {
    enemies.iterWithEntity([&](EntityId id, Transform2D&) {
        cmd.entity(id).despawn();
    });
}

// --- Update systems (state-conditional) ---
void menuUpdate(Res<InputResource> input, ResMut<NextState<GamePhase>> next) {
    if (input->keyPressed(Key::Return)) {
        next->set(GamePhase::Playing);
    }
}

void playingUpdate(Res<Time> time,
                   Res<InputResource> input,
                   ResMut<GameData> game,
                   ResMut<NextState<GamePhase>> next,
                   QueryMut<Transform2D, With<Player>> players) {
    float speed = 200.f;
    players.iter([&](Transform2D& t) {
        if (input->keyDown(Key::Right)) t.position.x += speed * time->delta;
        if (input->keyDown(Key::Left))  t.position.x -= speed * time->delta;
        if (input->keyDown(Key::Up))    t.position.y -= speed * time->delta;
        if (input->keyDown(Key::Down))  t.position.y += speed * time->delta;
    });

    if (input->keyPressed(Key::Escape)) {
        next->set(GamePhase::GameOver);
    }
}

void collisionCheck(EventReader<CollisionStart> collisions,
                    Query<Player> players,
                    Query<Enemy> enemies,
                    ResMut<NextState<GamePhase>> next) {
    for (auto& event : collisions) {
        bool playerHit =
            (players.contains(event.entityA) && enemies.contains(event.entityB)) ||
            (players.contains(event.entityB) && enemies.contains(event.entityA));
        if (playerHit) {
            next->set(GamePhase::GameOver);
            return;
        }
    }
}

void gameOverUpdate(Res<InputResource> input, ResMut<NextState<GamePhase>> next) {
    if (input->keyPressed(Key::Return)) {
        next->set(GamePhase::Menu);
    }
}

// --- Main ---
int main() {
    App app;
    app.setConfig({.title = "State Machine Example", .width = 800, .height = 600});
    app.addPlugins<DefaultPlugins>();
    app.addResource<GameData>();

    // Initialize state
    app.initState<GamePhase>(GamePhase::Menu);

    // State-conditional update systems
    app.update<menuUpdate>("menu_update", inState(GamePhase::Menu));
    app.update<playingUpdate>("playing_update", inState(GamePhase::Playing));
    app.update<collisionCheck>("collision_check", inState(GamePhase::Playing));
    app.update<gameOverUpdate>("gameover_update", inState(GamePhase::GameOver));

    // Enter/exit callbacks
    app.onEnter<enterMenu>(GamePhase::Menu, "enter_menu");
    app.onEnter<enterPlaying>(GamePhase::Playing, "enter_playing");
    app.onEnter<enterGameOver>(GamePhase::GameOver, "enter_gameover");
    app.onExit<exitPlaying>(GamePhase::Playing, "exit_playing");

    return app.run();
}
```

## Transition flow diagram

```
                  +-----------+
                  |   Menu    |
                  +-----------+
                    |       ^
       Enter pressed|       |Enter pressed
                    v       |
                  +-----------+
                  |  Playing  |
                  +-----------+
                    |
          Collision |
          or Escape |
                    v
                  +-----------+
                  | Game Over |
                  +-----------+
```

Each arrow triggers:
1. `onExit` for the old state
2. State value update
3. `onEnter` for the new state

## Summary

| API | Description |
|-----|-------------|
| `app.initState<T>(initial)` | Register a state enum with its initial value |
| `app.onEnter<fn>(value, "name")` | Run `fn` once when entering `value` |
| `app.onExit<fn>(value, "name")` | Run `fn` once when leaving `value` |
| `app.update<fn>("name", inState(value))` | Run `fn` every frame only while in `value` |
| `ResMut<NextState<T>>` | System parameter for requesting transitions |
| `next->set(value)` | Request a transition to `value` (processed next frame boundary) |
| `Res<State<T>>` | System parameter for reading the current state |
| `state->current` | The current state value |
