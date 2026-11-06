# Camera

The `Camera` component defines a view of the world: where it looks, how far it is zoomed, which part of the screen it fills, and which render layers it shows. Without a `Camera` entity the renderer uses a default view centered on the logical screen.

## Definition

```cpp
struct Camera {
    float zoom = 1.f;
    float rotation = 0.f;
    bool active = false;
    Rect viewport = {0.f, 0.f, 1.f, 1.f};
    int32_t order = 0;
    uint32_t layerMask = AllWorldLayers;
    TextureHandle target;
    bool clear = false;
    ColorF clearColor = {0.f, 0.f, 0.f, 1.f};
};
```

**Header:** `#include <drift/components/Camera.hpp>`

## Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `zoom` | `float` | `1.f` | Zoom factor. `1.0` is default. Values greater than 1 zoom in; values less than 1 zoom out. |
| `rotation` | `float` | `0.f` | Camera rotation in radians. Rotates the entire view around the camera's position. |
| `active` | `bool` | `false` | Only active cameras render. |
| `viewport` | `Rect` | whole screen | Normalized rect of the screen (or of `target`) to draw into, origin top-left. `{0, 0, 0.5f, 1}` is the left half. |
| `order` | `int32_t` | `0` | Cameras render in ascending order; later cameras draw over earlier ones. The lowest-ordered camera without a `target` is the main camera used by `screenToWorld`. |
| `layerMask` | `uint32_t` | all world layers | Bit N set = draw render layer N. Use it to keep a layer out of the minimap or to give a camera its own layer. |
| `target` | `TextureHandle` | none | A render target to draw into instead of the screen. |
| `clear` / `clearColor` | `bool` / `ColorF` | `false` | Clear the viewport before drawing. Useful for split screen and render targets. |

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

Every active camera renders, in ascending `order`. Combine `viewport`, `layerMask` and `target` for split screen, minimaps and picture-in-picture.

### Split screen

```cpp
cmd.spawn().insert(Transform2D{}, Camera{.active = true,
    .viewport = {0.f, 0.f, 0.5f, 1.f}, .clear = true});
cmd.spawn().insert(Transform2D{}, Camera{.active = true,
    .viewport = {0.5f, 0.f, 0.5f, 1.f}, .order = 1, .clear = true});
```

Give each camera a `CameraFollow` pointing at a different player.

### Minimap

A camera with a `target` renders into a texture, which you then draw on the HUD:

```cpp
TextureHandle minimap = renderer->createRenderTarget(256, 160, {TextureFilter::Linear});
cmd.spawn().insert(Transform2D{}, Camera{.zoom = 0.2f, .active = true, .order = -1,
                                         .target = minimap, .clear = true});
```

See `examples/render_showcase` for a complete minimap.

### Switching cameras

Toggle `active` to switch between views:

```cpp
void switchCamera(Res<InputResource> input, ResMut<CameraRig> rig, Commands& cmd) {
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

- [Camera Controllers](camera-controllers.md): `CameraFollow` and `CameraShake` components for automatic camera behavior.
- [Transform2D](transform.md): the position component that Camera depends on.
- [Rendering](../subsystems/rendering.md): layers, render targets and scale modes.
