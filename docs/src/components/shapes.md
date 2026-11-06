# Shapes

`Shape2D` draws a solid or outlined rectangle or circle at an entity. Use it for blocks, walls, markers, debug geometry and prototypes that need no texture; it follows the entity's transform like a sprite does. `ShapePlugin` draws every `Shape2D` each frame and is part of `DefaultPlugins`.

## Definition

```cpp
enum class ShapeKind : uint8_t { Rectangle, Circle };

struct Shape2D {
    ShapeKind kind = ShapeKind::Rectangle;
    Vec2 size = {32.f, 32.f};      // Rectangle: full width and height
    float radius = 16.f;           // Circle
    Color color = Color::white();
    float outline = 0.f;           // 0 = filled; otherwise the line thickness
    float cornerRadius = 0.f;      // Rectangle, filled and unrotated only
    Vec2 anchor = {0.5f, 0.5f};    // Rectangle: the point on the position, 0..1
    float zOrder = 0.f;
    uint8_t layer = 0;             // render layer 0..30, or ScreenLayer
    bool visible = true;
};
```

**Header:** `#include <drift/components/Shape.hpp>`

## Usage

```cpp
void setup(Commands& cmd) {
    // A wall, centered on its position.
    cmd.spawn().insert(Transform2D{.position = {480.f, 530.f}},
                       Shape2D{.size = {960.f, 12.f}, .color = {60, 66, 96, 255}});

    // An outlined ring.
    cmd.spawn().insert(Transform2D{.position = {200.f, 200.f}},
                       Shape2D{.kind = ShapeKind::Circle, .radius = 40.f,
                               .color = {255, 210, 60, 255}, .outline = 3.f});
}
```

The transform's rotation turns rectangles; its scale scales both kinds (circles by `scale.x`). Inside a [hierarchy](hierarchy.md) the shape draws at the entity's `GlobalTransform2D`. Shapes sort with sprites by `layer` and `zOrder`, and a shape on `ScreenLayer` is in screen coordinates.

`Shape2D` is reflected, so it saves and loads in [scenes](../subsystems/scenes.md).

## Limitations

Rounded corners apply to filled rectangles without rotation; a rotated or outlined rectangle draws square corners. For immediate shapes drawn for one frame, use the drawing calls on `RendererResource` (see [Rendering](../subsystems/rendering.md)).
