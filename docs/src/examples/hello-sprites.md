# Hello Sprites

**Source:** `examples/hello_sprites/main.cpp`

This example is the recommended starting point for learning Drift. It demonstrates entity spawning, sprite rendering, keyboard input, camera control, parent-child hierarchies, marker components, and the query system.

## What It Does

A player sprite moves with WASD/arrow keys. Eight orbiter sprites circle the player as children, changing color over time. A checkerboard grid of background tiles fills the scene. The camera follows the player and can be zoomed with the mouse wheel.

## Walkthrough

### Resource

The example defines a `GameState` resource to hold shared state across systems:

```cpp
struct GameState : public Resource {
    TextureHandle texture;
    Vec2 playerPos = {0.f, 0.f};
    float playerSpeed = 200.f;
    float time = 0.f;
    EntityId camera = InvalidEntityId;
};
```

The resource is registered with `initResource` in `main()`, which is idempotent (safe to call multiple times).

### Startup System

The `setupGame` function runs once at startup. It loads a texture, spawns the camera, the player with child orbiters, and a grid of background tiles.

**Loading assets:**

```cpp
game->texture = assets->load<Texture>("assets/sprites/player.png");
```

**Spawning a camera:**

```cpp
game->camera = cmd.spawn()
    .insert<Transform2D>({.position = {0, 0}})
    .insert<Camera>({.zoom = 1.f, .active = true});
```

**Spawning the player with children:**

```cpp
cmd.spawn()
    .insert(Transform2D{}, Sprite{.texture = game->texture, .zOrder = 1.f},
            PlayerTag{}, Name{"Player"})
    .withChildren([&](ChildSpawner& spawner) {
        for (int i = 0; i < 8; ++i) {
            spawner.spawn()
                .insert(Transform2D{}, Sprite{.texture = game->texture, .zOrder = 0.5f},
                        OrbiterTag{}, Name{"Orbiter"});
        }
    });
```

Key points:
- **Variadic insert** -- multiple components passed in a single `insert()` call.
- **`withChildren`** -- spawns child entities that are automatically linked to the parent via the hierarchy system.
- **Marker components** -- `PlayerTag{}` and `OrbiterTag{}` are empty structs used solely for query filtering.
- **`Name`** -- a debug label component (64-char fixed-size string).

**Spawning grid tiles:**

```cpp
for (int y = -5; y <= 5; ++y) {
    for (int x = -5; x <= 5; ++x) {
        cmd.spawn()
            .insert(Transform2D{.position = {x * 64.f, y * 64.f}}, s);
    }
}
```

### Update System

The `updateGame` function runs every frame. Its signature demonstrates system parameter injection:

```cpp
void updateGame(Res<InputResource> input, Res<Time> time,
                ResMut<GameState> game,
                QueryMut<Transform2D, With<PlayerTag>> players,
                QueryMut<Transform2D, Sprite, With<OrbiterTag>> orbiters,
                QueryMut<Transform2D, Optional<Name>> named,
                Commands& cmd);
```

**WASD movement:**

```cpp
Vec2 dir{};
if (input->keyHeld(Key::W) || input->keyHeld(Key::Up))    dir.y -= 1.f;
if (input->keyHeld(Key::S) || input->keyHeld(Key::Down))  dir.y += 1.f;
if (input->keyHeld(Key::A) || input->keyHeld(Key::Left))  dir.x -= 1.f;
if (input->keyHeld(Key::D) || input->keyHeld(Key::Right)) dir.x += 1.f;
game->playerPos += dir.normalized() * game->playerSpeed * time->delta;
```

**Updating the player via query:**

```cpp
players.iter([&](Transform2D& t) {
    t.position = game->playerPos;
});
```

The `With<PlayerTag>` filter means only entities that have both `Transform2D` and `PlayerTag` are matched.

**Camera follow via Commands:**

```cpp
cmd.entity(game->camera)
    .insert<Transform2D>({.position = game->playerPos});
```

This overwrites the camera transform each frame. Commands are deferred and flushed after the phase completes.

**Zoom with mouse wheel:**

```cpp
float wheel = input->mouseWheelDelta();
if (wheel != 0.f) {
    zoom += wheel * 0.1f;
    cmd.entity(game->camera)
        .insert<Camera>({.zoom = zoom, .active = true});
}
```

**Orbiter animation:**

```cpp
int orbiterIdx = 0;
orbiters.iter([&](Transform2D& t, Sprite& s) {
    float angle = game->time + orbiterIdx * 0.785f;
    t.position = {
        game->playerPos.x + cosf(angle) * 150.f,
        game->playerPos.y + sinf(angle) * 150.f
    };
    t.rotation = angle;
    t.scale = {0.5f, 0.5f};
    orbiterIdx++;
});
```

**Optional query parameter:**

```cpp
QueryMut<Transform2D, Optional<Name>> named
```

`Optional<Name>` means the query matches all entities with `Transform2D`, regardless of whether they have a `Name` component. In the callback, the `Name*` parameter will be `nullptr` for entities without it.

### Main

```cpp
int main(int, char*[]) {
    App app;
    app.setConfig({.title = "Drift - Hello Sprites", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameState>();
    app.startup<setupGame>();
    app.update<updateGame>();
    return app.run();
}
```

## Concepts Demonstrated

| Concept | Where |
|---------|-------|
| Marker components (`PlayerTag`, `OrbiterTag`) | Startup + query filters |
| `With<T>` filter | `QueryMut<Transform2D, With<PlayerTag>>` |
| `Optional<T>` | `QueryMut<Transform2D, Optional<Name>>` |
| Variadic `insert()` | Player spawn with four components |
| `withChildren` / `ChildSpawner` | Orbiter children |
| `initResource<T>()` | Idempotent resource registration |
| `Res<T>` / `ResMut<T>` | Read-only vs. mutable resource access |
| `Commands::entity(id)` | Deferred mutation of specific entities |
| `Name` component | Debug labels on player and orbiters |
| `AssetServer::load<T>()` | Texture loading |
