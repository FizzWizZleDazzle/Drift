# Flappy Bird

**Source:** `examples/flappy_bird/main.cpp`

A complete Flappy Bird clone demonstrating the game state machine, physics-based collision detection, sprite animation, sound effects, and the full game loop pattern.

## What It Does

The classic Flappy Bird game. Press Space or click to flap. Pipes scroll from right to left with randomized gaps. Sensor-based collision detection ends the game when the bird hits a pipe or the floor. A score counter tracks how many pipe gaps the bird has passed through.

## Game State Machine

The example uses Drift's built-in `State<T>` system with three phases:

```cpp
enum class GamePhase { Menu, Playing, Dead };
```

State is initialized in `main()`:

```cpp
app.initState<GamePhase>(GamePhase::Menu);
```

State-specific systems use the `inState` run condition:

```cpp
app.update<menuUpdate>("menu_update", inState(GamePhase::Menu));
app.update<playingUpdate>("playing_update", inState(GamePhase::Playing));
app.update<deadUpdate>("dead_update", inState(GamePhase::Dead));
```

Transitions happen through `NextState<GamePhase>`:

```cpp
void menuUpdate(..., ResMut<NextState<GamePhase>> next) {
    if (action) {
        next->set(GamePhase::Playing);
    }
}
```

### OnEnter / OnExit

Each state can have entry callbacks that run once on transition:

```cpp
app.onEnter<onEnterMenu>(GamePhase::Menu, "enter_menu");
app.onEnter<onEnterPlaying>(GamePhase::Playing, "enter_playing");
app.onEnter<onEnterDead>(GamePhase::Dead, "enter_dead");
```

The `onEnterMenu` callback resets the game, `onEnterPlaying` triggers the first flap, and `onEnterDead` plays the hit sound.

## Physics and Collision

Pipes use kinematic rigid bodies with box colliders. The bird uses a dynamic body (with `gravityScale = 0` since the game handles gravity manually) and a sensor collider:

```cpp
// Bird
.insert<RigidBody2D>({.type = BodyType::Dynamic, .fixedRotation = true, .gravityScale = 0.f})
.insert<BoxCollider2D>({.halfSize = {BIRD_W * 0.4f, BIRD_H * 0.4f}, .isSensor = true})

// Pipe
.insert<RigidBody2D>({.type = BodyType::Kinematic, .fixedRotation = true})
.insert<BoxCollider2D>({.halfSize = {PIPE_WIDTH * 0.5f, PIPE_HEIGHT * 0.5f}, ...})
```

Collision detection reads `SensorStart` and `CollisionStart` events and checks whether the involved entities are a bird and a pipe using `Query::contains()`:

```cpp
void checkCollision(EventReader<SensorStart> sensors,
                    EventReader<CollisionStart> collisions,
                    Query<Bird> birds, Query<Pipe> pipes,
                    ResMut<NextState<GamePhase>> next, ...) {
    for (auto& event : sensors) {
        if ((birds.contains(event.sensorEntity) && pipes.contains(event.visitorEntity)) ||
            (birds.contains(event.visitorEntity) && pipes.contains(event.sensorEntity))) {
            next->set(GamePhase::Dead);
            return;
        }
    }
}
```

## Sprite Animation

The bird cycles through three wing textures (down, mid, up) on a timer:

```cpp
game->birdAnimTimer += dt;
if (game->birdAnimTimer >= BIRD_ANIM_SPEED) {
    game->birdAnimTimer = 0.f;
    game->birdFrame = (game->birdFrame + 1) % 3;
}
```

The active texture is applied during entity sync:

```cpp
s.texture = game->texBird[game->birdFrame];
```

## Sound Effects

Sounds are loaded through the `AssetServer` and played through `AudioResource`:

```cpp
game->sndWing = assets->load<Sound>("assets/wing.wav");
// ...
if (game->sndWing.valid() && audio) audio->playSound(game->sndWing, 0.6f);
```

The game uses five sounds: wing flap, hit, point scored, die, and swoosh (menu transition).

## System Ordering

Systems are organized into ordered sets to ensure correct execution:

```cpp
enum class FlappySets { Input, Physics, Rendering };
app.configureSets(Phase::Update, {FlappySets::Input, FlappySets::Physics, FlappySets::Rendering});
```

Input handling runs first, then collision detection, then entity sync for rendering.

## Concepts Demonstrated

| Concept | Where |
|---------|-------|
| `initState` / `State<T>` / `NextState<T>` | Game phase management |
| `inState()` run condition | State-specific systems |
| `onEnter` callbacks | Transition side effects |
| `EventReader<SensorStart>` | Physics collision events |
| `Query::contains()` | Entity membership checks |
| `BoxCollider2D` with `isSensor` | Trigger-based collision |
| `BodyType::Kinematic` | Moving obstacles without physics forces |
| `AudioResource::playSound()` | Sound effects |
| `configureSets` / `.inSet()` | System execution ordering |
| `Flip::V` | Vertically flipping the top pipe sprite |
| Pixel-perfect camera | `Camera{.zoom = 2.f}` for 2x scaling |
