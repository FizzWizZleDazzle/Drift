# Camera

The `Camera` component defines the viewport through which the world is rendered. It controls zoom level, rotation, and whether the camera is active. Every scene needs at least one entity with both `Camera` and `Transform2D` for anything to be visible.

## Definition

```cpp
struct Camera {
    float zoom = 1.f;
    float rotation = 0.f;
    bool active = false;
};
```

**Header:** `#include <drift/components/Camera.hpp>`

## Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `zoom` | `float` | `1.f` | Zoom factor. `1.0` is default. Values greater than 1 zoom in; values less than 1 zoom out. |
| `rotation` | `float` | `0.f` | Camera rotation in radians. Rotates the entire view around the camera's position. |
| `active` | `bool` | `false` | Only the active camera is used for rendering. If multiple cameras have `active = true`, the last one processed wins. |

## Requirements

A `Camera` component alone does nothing. The entity must also have a `Transform2D` to define the camera's position in world space:

```cpp
cmd.spawn()
    .insert(
        Transform2D{.position = {0.f, 0.f}},
        Camera{.zoom = 1.f, .active = true}
    );
```

## Basic usage

### Spawning a camera

Most games create a camera during setup:

```cpp
void setup(Commands& cmd) {
    cmd.spawn()
        .insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});
}
```

### Storing the camera entity ID

You often need to update the camera later. Store its entity ID in a resource:

```cpp
struct GameState : public Resource {
    EntityId camera = InvalidEntityId;
};

void setup(ResMut<GameState> game, Commands& cmd) {
    game->camera = cmd.spawn()
        .insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});
}
```

### Moving the camera

Update the camera's position by writing to its `Transform2D`:

```cpp
void updateCamera(ResMut<GameState> game, Commands& cmd) {
    cmd.entity(game->camera)
        .insert<Transform2D>({.position = {newX, newY}});
}
```

Or use a query if you only have one camera:

```cpp
void updateCamera(QueryMut<Transform2D, Camera> cameras) {
    cameras.iter([](Transform2D& t, Camera&) {
        t.position.x += 1.f;  // pan right
    });
}
```

## Zoom

Zoom scales the rendered view around the camera's position. A zoom of `2.0` makes everything appear twice as large (showing half the world area). A zoom of `0.5` shows twice as much of the world.

```cpp
// Zoom with mouse wheel
void zoomControl(Res<InputResource> input,
                 QueryMut<Camera> cameras) {
    float wheel = input->mouseWheelDelta();
    if (wheel == 0.f) return;

    cameras.iter([&](Camera& cam) {
        cam.zoom += wheel * 0.1f;
        cam.zoom = fmaxf(0.1f, fminf(cam.zoom, 10.f));  // clamp
    });
}
```

## Rotation

Camera rotation tilts the entire view. This can be used for effects like screen tilt during impacts:

```cpp
void tiltCamera(QueryMut<Camera> cameras) {
    cameras.iter([](Camera& cam) {
        cam.rotation = 0.1f;  // slight tilt
    });
}
```

## Multiple cameras

You can have multiple camera entities, but only one should be active at a time. Switch between cameras by toggling the `active` flag:

```cpp
struct CameraRig : public Resource {
    EntityId mainCamera = InvalidEntityId;
    EntityId overviewCamera = InvalidEntityId;
    bool showOverview = false;
};

void switchCamera(Res<InputResource> input,
                  ResMut<CameraRig> rig,
                  Commands& cmd) {
    if (input->keyPressed(Key::Tab)) {
        rig->showOverview = !rig->showOverview;

        cmd.entity(rig->mainCamera)
            .insert<Camera>({.zoom = 1.f, .active = !rig->showOverview});
        cmd.entity(rig->overviewCamera)
            .insert<Camera>({.zoom = 0.25f, .active = rig->showOverview});
    }
}
```

## Complete example

```cpp
#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/AssetServer.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/Time.hpp>

using namespace drift;

struct GameState : public Resource {
    EntityId camera = InvalidEntityId;
    TextureHandle tex;
    float zoom = 1.f;
};

void setup(ResMut<GameState> game, ResMut<AssetServer> assets, Commands& cmd) {
    game->tex = assets->load<Texture>("assets/world.png");

    game->camera = cmd.spawn()
        .insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});

    // Spawn some world content
    cmd.spawn().insert(
        Transform2D{.position = {0.f, 0.f}},
        Sprite{.texture = game->tex}
    );
}

void update(Res<InputResource> input, Res<Time> time,
            ResMut<GameState> game,
            QueryMut<Transform2D, Camera> cameras) {
    // Pan camera with arrow keys
    Vec2 pan{};
    if (input->keyHeld(Key::Left))  pan.x -= 200.f;
    if (input->keyHeld(Key::Right)) pan.x += 200.f;
    if (input->keyHeld(Key::Up))    pan.y -= 200.f;
    if (input->keyHeld(Key::Down))  pan.y += 200.f;

    // Zoom with mouse wheel
    float wheel = input->mouseWheelDelta();
    game->zoom += wheel * 0.1f;
    game->zoom = fmaxf(0.1f, fminf(game->zoom, 5.f));

    cameras.iter([&](Transform2D& t, Camera& cam) {
        t.position += pan * time->delta;
        cam.zoom = game->zoom;
    });
}

int main() {
    App app;
    app.setConfig({.title = "Camera Example", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameState>();
    app.startup<setup>();
    app.update<update>();
    return app.run();
}
```

## See also

- [Camera Controllers](camera-controllers.md) -- `CameraFollow` and `CameraShake` components for automatic camera behavior.
- [Transform2D](transform.md) -- the position component that Camera depends on.
