# Input

The `InputResource` provides keyboard, mouse, and gamepad input. It processes SDL events each frame, tracking per-key and per-button state transitions so you can distinguish between pressed (this frame), held (still down), and released (this frame).

**Header:** `#include <drift/resources/InputResource.hpp>`

**Plugin:** `InputPlugin` (included in `DefaultPlugins`)

## Accessing Input in Systems

`InputResource` is a read-only resource in most gameplay systems. Access it via `Res<InputResource>`.

```cpp
#include <drift/resources/InputResource.hpp>

void playerInput(Res<InputResource> input, Res<Time> time,
                 QueryMut<Transform2D, With<PlayerTag>> query) {
    float speed = 200.f * time->delta;

    query.iter([&](Transform2D& t) {
        if (input->keyHeld(Key::W))     t.position.y -= speed;
        if (input->keyHeld(Key::S))     t.position.y += speed;
        if (input->keyHeld(Key::A))     t.position.x -= speed;
        if (input->keyHeld(Key::D))     t.position.x += speed;
    });
}
```

## Keyboard API

```cpp
bool keyPressed(Key key) const;   // true on the frame the key goes down
bool keyHeld(Key key) const;      // true every frame the key is down
bool keyReleased(Key key) const;  // true on the frame the key goes up
```

**`keyPressed`** fires once, the frame the key transitions from up to down. Use it for actions that should happen exactly once (jumping, shooting, toggling a menu).

**`keyHeld`** is true for every frame the key remains down, including the press frame. Use it for continuous movement or charging.

**`keyReleased`** fires once, the frame the key transitions from down to up.

### Key Enum

The `Key` enum mirrors SDL scancodes. Common values:

| Key | Value | Key | Value |
|-----|-------|-----|-------|
| `Key::A` - `Key::Z` | 4-29 | `Key::Space` | 44 |
| `Key::Num0` - `Key::Num9` | 39-30 | `Key::Return` | 40 |
| `Key::F1` - `Key::F12` | 58-69 | `Key::Escape` | 41 |
| `Key::Left` | 80 | `Key::Right` | 79 |
| `Key::Up` | 82 | `Key::Down` | 81 |
| `Key::LShift` | 225 | `Key::LCtrl` | 224 |

### Example: Quit on Escape

```cpp
void quitOnEscape(Res<InputResource> input, Commands& cmd) {
    if (input->keyPressed(Key::Escape)) {
        cmd.queue([](World& world) {
            world.app().quit();
        });
    }
}
```

## Mouse API

```cpp
Vec2  mousePosition() const;                      // position in logical coordinates
Vec2  mouseDelta() const;                          // movement since last frame
bool  mouseButtonPressed(MouseButton btn) const;   // pressed this frame
bool  mouseButtonHeld(MouseButton btn) const;      // held down
bool  mouseButtonReleased(MouseButton btn) const;  // released this frame
float mouseWheelDelta() const;                     // scroll wheel delta
```

Mouse positions are reported in logical coordinates (matching the resolution set by `setLogicalSize`). The `InputResource` handles the conversion from physical pixels automatically.

### MouseButton Enum

```cpp
enum class MouseButton {
    Left   = 1,
    Middle = 2,
    Right  = 3,
};
```

### Example: Shoot on Click

```cpp
void shootSystem(Res<InputResource> input, ResMut<RendererResource> renderer,
                 Commands& cmd) {
    if (input->mouseButtonPressed(MouseButton::Left)) {
        auto camera = renderer->getDefaultCamera();
        Vec2 worldPos = renderer->screenToWorld(camera, input->mousePosition());

        cmd.spawn().insert(
            Transform2D{.position = worldPos},
            Sprite{},
            Bullet{}
        );
    }
}
```

### Example: Zoom with Scroll Wheel

```cpp
void cameraZoom(Res<InputResource> input, ResMut<RendererResource> renderer) {
    float wheel = input->mouseWheelDelta();
    if (wheel != 0.f) {
        auto camera = renderer->getDefaultCamera();
        float zoom = renderer->getCameraZoom(camera);
        zoom += wheel * 0.1f;
        zoom = std::clamp(zoom, 0.5f, 3.0f);
        renderer->setCameraZoom(camera, zoom);
    }
}
```

## Gamepad API

```cpp
bool  gamepadConnected(int32_t index) const;
float gamepadAxis(int32_t index, int32_t axis) const;
bool  gamepadButtonPressed(int32_t index, int32_t button) const;
bool  gamepadButtonHeld(int32_t index, int32_t button) const;
bool  gamepadButtonReleased(int32_t index, int32_t button) const;
```

The `index` parameter identifies which gamepad (0 for the first connected controller). Axis values range from `-1.0` to `1.0`. Button indices follow the SDL gamepad mapping (0 = A/Cross, 1 = B/Circle, etc.).

### Example: Gamepad Movement

```cpp
void gamepadMove(Res<InputResource> input, Res<Time> time,
                 QueryMut<Transform2D, With<PlayerTag>> query) {
    if (!input->gamepadConnected(0)) return;

    float lx = input->gamepadAxis(0, 0);  // left stick X
    float ly = input->gamepadAxis(0, 1);  // left stick Y
    float speed = 200.f * time->delta;

    // Apply deadzone
    if (std::abs(lx) < 0.15f) lx = 0.f;
    if (std::abs(ly) < 0.15f) ly = 0.f;

    query.iter([&](Transform2D& t) {
        t.position.x += lx * speed;
        t.position.y += ly * speed;
    });
}
```

## Combining Input Sources

A typical pattern is to unify keyboard and gamepad input into a single direction vector:

```cpp
void playerMovement(Res<InputResource> input, Res<Time> time,
                    QueryMut<Transform2D, With<PlayerTag>> query) {
    Vec2 dir = Vec2::zero();

    // Keyboard
    if (input->keyHeld(Key::W) || input->keyHeld(Key::Up))    dir.y -= 1.f;
    if (input->keyHeld(Key::S) || input->keyHeld(Key::Down))  dir.y += 1.f;
    if (input->keyHeld(Key::A) || input->keyHeld(Key::Left))  dir.x -= 1.f;
    if (input->keyHeld(Key::D) || input->keyHeld(Key::Right)) dir.x += 1.f;

    // Gamepad (overrides if connected)
    if (input->gamepadConnected(0)) {
        float lx = input->gamepadAxis(0, 0);
        float ly = input->gamepadAxis(0, 1);
        if (std::abs(lx) > 0.15f || std::abs(ly) > 0.15f) {
            dir = {lx, ly};
        }
    }

    // Normalize to prevent diagonal speed boost
    if (dir.lengthSq() > 1.f) dir = dir.normalized();

    float speed = 300.f * time->delta;
    query.iter([&](Transform2D& t) {
        t.position += dir * speed;
    });
}
```
