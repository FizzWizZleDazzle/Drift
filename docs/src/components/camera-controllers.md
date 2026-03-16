# Camera Controllers

Drift provides two components for common camera behavior: `CameraFollow` for smooth target tracking and `CameraShake` for screen shake effects. Both operate on entities that also have `Camera` and `Transform2D`.

## CameraFollow

Smoothly moves the camera to track a target entity, with optional offset and dead zone.

```cpp
struct CameraFollow {
    EntityId target = InvalidEntityId;
    float smoothing = 5.f;      // lerp speed (higher = snappier)
    Vec2 offset = {};            // offset from target position
    Vec2 deadZone = {};          // don't move if target is within this range
};
```

**Header:** `#include <drift/components/CameraController.hpp>`

### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `target` | `EntityId` | `InvalidEntityId` | The entity to follow. Must have a `Transform2D`. |
| `smoothing` | `float` | `5.f` | Interpolation speed. Higher values make the camera snap to the target faster. Lower values create a more floaty, cinematic feel. |
| `offset` | `Vec2` | `{0, 0}` | Constant offset from the target's position. Use this to look ahead of the player or keep the target off-center. |
| `deadZone` | `Vec2` | `{0, 0}` | Half-size of the dead zone rectangle. If the target is within this range of the camera center, the camera does not move. Useful for preventing jitter during small movements. |

### How it works

Each frame (in `PostUpdate`), the follow system:

1. Reads the target entity's `Transform2D` position.
2. Computes the desired camera position as `targetPosition + offset`.
3. If the difference between the current and desired position is within the dead zone on both axes, the camera stays still.
4. Otherwise, it smoothly interpolates toward the desired position using exponential smoothing: `camera += delta * (1 - exp(-smoothing * dt))`.

### Basic usage

```cpp
struct GameState : public Resource {
    EntityId player = InvalidEntityId;
};

void setup(ResMut<GameState> game, Commands& cmd) {
    // Spawn the player
    game->player = cmd.spawn()
        .insert(Transform2D{.position = {0.f, 0.f}}, Sprite{.texture = tex});

    // Spawn a camera that follows the player
    cmd.spawn().insert(
        Transform2D{},
        Camera{.zoom = 1.f, .active = true},
        CameraFollow{.target = game->player, .smoothing = 5.f}
    );
}
```

### With offset

Look slightly ahead and above the player:

```cpp
CameraFollow{
    .target = playerId,
    .smoothing = 4.f,
    .offset = {100.f, -50.f}   // 100px right, 50px up
}
```

### With dead zone

Prevent the camera from moving during small adjustments:

```cpp
CameraFollow{
    .target = playerId,
    .smoothing = 5.f,
    .deadZone = {32.f, 32.f}   // 64x64 pixel dead zone centered on camera
}
```

### Changing target at runtime

Switch which entity the camera follows:

```cpp
void switchTarget(QueryMut<CameraFollow> cameras, EntityId newTarget) {
    cameras.iter([&](CameraFollow& follow) {
        follow.target = newTarget;
    });
}
```

## CameraShake

Applies a decaying oscillation to the camera's position, creating a screen shake effect.

```cpp
struct CameraShake {
    float intensity = 0.f;      // current shake magnitude in pixels
    float decay = 5.f;          // how fast intensity decays per second
    float frequency = 20.f;     // shake oscillation speed
    float elapsed = 0.f;

    void trigger(float mag);
};
```

### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `intensity` | `float` | `0.f` | Current shake magnitude in pixels. Decays over time. |
| `decay` | `float` | `5.f` | Exponential decay rate. Higher values make the shake die out faster. |
| `frequency` | `float` | `20.f` | Oscillation speed. Higher values create a faster, more frantic shake. |
| `elapsed` | `float` | `0.f` | Internal timer for the oscillation function. |

### Methods

| Method | Description |
|--------|-------------|
| `trigger(float mag)` | Start a shake with the given pixel magnitude. Resets `elapsed` to 0. |

### How it works

Each frame (in `PostUpdate`), the shake system:

1. If `intensity` is near zero, it resets to exactly 0 and returns.
2. Advances `elapsed` by delta time.
3. Computes an offset using overlapping sine/cosine waves at the configured `frequency`.
4. Adds `offset * intensity` to the camera's `Transform2D` position.
5. Decays `intensity` exponentially: `intensity *= exp(-decay * dt)`.

Because the shake modifies position additively, it works alongside `CameraFollow` -- the follow system moves the camera to the target, and then the shake system adds displacement on top.

### Basic usage

Add `CameraShake` alongside the other camera components:

```cpp
cmd.spawn().insert(
    Transform2D{},
    Camera{.zoom = 1.f, .active = true},
    CameraFollow{.target = playerId, .smoothing = 5.f},
    CameraShake{.decay = 5.f, .frequency = 20.f}
);
```

Trigger the shake when something happens (damage, explosion, landing):

```cpp
void onDamage(QueryMut<CameraShake> cameras) {
    cameras.iter([](CameraShake& shake) {
        shake.trigger(15.f);   // 15-pixel magnitude shake
    });
}
```

### Intensity examples

| Magnitude | Use case |
|-----------|----------|
| 2-5 | Subtle rumble (footsteps, small impacts) |
| 8-15 | Medium hit (taking damage, landing) |
| 20-40 | Strong impact (explosion, boss attack) |
| 50+ | Extreme event (earthquake, screen-filling blast) |

### Tuning decay and frequency

```cpp
// Quick, snappy shake (gun recoil)
CameraShake{.decay = 10.f, .frequency = 30.f}

// Slow, rumbling shake (earthquake)
CameraShake{.decay = 2.f, .frequency = 8.f}

// Standard damage shake
CameraShake{.decay = 5.f, .frequency = 20.f}
```

## Plugin

Both systems are provided by the `CameraPlugin`, which is included in `DefaultPlugins`. If you are not using `DefaultPlugins`, add it manually:

```cpp
app.addPlugin<CameraPlugin>();
```

The plugin registers both `CameraFollow` and `CameraShake` components and adds the follow and shake systems in `PostUpdate`.

## Complete example

```cpp
#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/AssetServer.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/CameraController.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/Time.hpp>

using namespace drift;

struct PlayerTag {};

struct GameState : public Resource {
    TextureHandle tex;
    EntityId player = InvalidEntityId;
};

void setup(ResMut<GameState> game, ResMut<AssetServer> assets, Commands& cmd) {
    game->tex = assets->load<Texture>("assets/player.png");

    game->player = cmd.spawn()
        .insert(
            Transform2D{.position = {0.f, 0.f}},
            Sprite{.texture = game->tex, .origin = {16.f, 16.f}},
            PlayerTag{}
        );

    cmd.spawn().insert(
        Transform2D{},
        Camera{.zoom = 1.f, .active = true},
        CameraFollow{
            .target = game->player,
            .smoothing = 5.f,
            .offset = {0.f, -30.f},
            .deadZone = {16.f, 16.f}
        },
        CameraShake{.decay = 5.f, .frequency = 20.f}
    );
}

void movePlayer(Res<InputResource> input, Res<Time> time,
                QueryMut<Transform2D, With<PlayerTag>> players) {
    Vec2 dir{};
    if (input->keyHeld(Key::A)) dir.x -= 1.f;
    if (input->keyHeld(Key::D)) dir.x += 1.f;
    if (input->keyHeld(Key::W)) dir.y -= 1.f;
    if (input->keyHeld(Key::S)) dir.y += 1.f;

    players.iter([&](Transform2D& t) {
        t.position += dir.normalized() * 200.f * time->delta;
    });
}

void shakeOnSpace(Res<InputResource> input,
                  QueryMut<CameraShake> cameras) {
    if (input->keyPressed(Key::Space)) {
        cameras.iter([](CameraShake& shake) {
            shake.trigger(20.f);
        });
    }
}

int main() {
    App app;
    app.setConfig({.title = "Camera Controllers", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameState>();
    app.startup<setup>();
    app.update<movePlayer>();
    app.update<shakeOnSpace>();
    return app.run();
}
```

## Notes

- Both `CameraFollow` and `CameraShake` require `Camera` and `Transform2D` on the same entity.
- The follow system runs before the shake system within `PostUpdate`, so shake displacement is applied on top of the followed position.
- `CameraFollow` uses exponential smoothing rather than linear interpolation, which produces motion that feels responsive but not jerky. A `smoothing` value of about 5 is a good starting point.
- Setting `CameraFollow::target` to `InvalidEntityId` disables following without removing the component.
