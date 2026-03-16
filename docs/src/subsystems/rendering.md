# Rendering

The `RendererResource` manages the GPU, textures, sprite drawing, primitives, and cameras. It uses SDL3's GPU API under the hood and provides a sorted, batched 2D rendering pipeline.

**Header:** `#include <drift/resources/RendererResource.hpp>`

**Plugin:** `RendererPlugin` (included in `DefaultPlugins`)

## Automatic Sprite Rendering

When `DefaultPlugins` is enabled, the `SpritePlugin` runs a render system that draws every entity with a `Sprite` and `Transform2D` component. You usually do not need to call `drawSprite` manually for regular game objects -- just attach components and they appear on screen.

```cpp
void setup(Commands& cmd) {
    cmd.spawn().insert(
        Transform2D{.position = {400.f, 300.f}},
        Sprite{.texture = myTexture, .zOrder = 1.f}
    );
}
```

The `RendererResource` is for cases where you need direct draw control, custom rendering, debug visuals, or bulk operations like particles.

## Textures

```cpp
TextureHandle loadTexture(const char* path);
TextureHandle createTextureFromPixels(const uint8_t* pixels, int w, int h);
void          destroyTexture(TextureHandle texture);
void          getTextureSize(TextureHandle texture, int32_t* w, int32_t* h) const;
```

`loadTexture` loads an image file (PNG, JPG, BMP) and uploads it to the GPU. It returns a generational `TextureHandle` -- a lightweight 32-bit identifier.

```cpp
void setup(ResMut<RendererResource> renderer) {
    TextureHandle tex = renderer->loadTexture("assets/player.png");

    int32_t w, h;
    renderer->getTextureSize(tex, &w, &h);
    // w = 64, h = 64
}
```

`createTextureFromPixels` creates a texture from raw RGBA pixel data. Useful for procedural generation:

```cpp
uint8_t pixels[4 * 4 * 4]; // 4x4 RGBA
// ... fill pixel data ...
TextureHandle tex = renderer->createTextureFromPixels(pixels, 4, 4);
```

## Sprite Drawing

The `drawSprite` function has three overloads, from simple to full-featured:

```cpp
// Simple: texture at a position with z-order
void drawSprite(TextureHandle texture, Vec2 position, float zOrder);

// With source rectangle (for spritesheets)
void drawSprite(TextureHandle texture, Vec2 position, Rect srcRect, float zOrder);

// Full control
void drawSprite(TextureHandle texture, Vec2 position, Rect srcRect = {},
                Vec2 scale = Vec2::one(), float rotation = 0.f,
                Vec2 origin = {}, Color tint = Color::white(),
                Flip flip = Flip::None, float zOrder = 0.f,
                bool additive = false);
```

Parameters:

| Parameter | Description |
|-----------|-------------|
| `texture` | The texture handle to draw |
| `position` | World-space position |
| `srcRect` | Sub-region of the texture to draw (empty = full texture) |
| `scale` | Scale multiplier (default `{1, 1}`) |
| `rotation` | Rotation in radians |
| `origin` | Rotation/scale origin offset from top-left |
| `tint` | Color tint multiplied with texture color |
| `flip` | `Flip::H`, `Flip::V`, or `Flip::H | Flip::V` |
| `zOrder` | Draw order (lower values draw first, higher on top) |
| `additive` | Use additive blending (bright, glowing effects) |

### Example: Drawing a Rotated Sprite

```cpp
void drawPlayer(ResMut<RendererResource> renderer, Res<Time> time,
                Query<Transform2D, With<PlayerTag>> query) {
    query.iter([&](const Transform2D& t) {
        renderer->drawSprite(playerTexture, t.position, Rect{},
                             t.scale, t.rotation,
                             Vec2{16.f, 16.f},   // center origin on 32x32 sprite
                             Color::white(), Flip::None, 10.f);
    });
}
```

## Batch Drawing

For high-volume rendering (particle systems, bullet hell), use `drawSpriteBatch` to avoid per-sprite function call overhead:

```cpp
void drawSpriteBatch(TextureHandle texture, Rect srcRect,
                     const Vec2* positions, const float* sizes,
                     const float* rotations, const Color* tints,
                     int32_t count, float zOrder, bool additive = false);
```

All sprites in a batch share the same texture and source rectangle. Pass arrays of positions, sizes, rotations, and tint colors.

```cpp
void renderBullets(ResMut<RendererResource> renderer) {
    std::vector<Vec2> positions;
    std::vector<float> sizes;
    std::vector<float> rotations;
    std::vector<Color> tints;
    // ... fill from your bullet pool ...

    renderer->drawSpriteBatch(bulletTexture, Rect{0, 0, 8, 8},
                              positions.data(), sizes.data(),
                              rotations.data(), tints.data(),
                              (int32_t)positions.size(), 5.f);
}
```

## Primitive Drawing

For debug visuals, guides, or simple shapes:

```cpp
void drawRect(Rect rect, Color color);                               // outline
void drawRectFilled(Rect rect, Color color);                         // filled
void drawLine(Vec2 start, Vec2 end, Color color, float thickness);   // line
void drawCircle(Vec2 center, float radius, Color color, int segments); // outline
```

These are drawn in world space and respect the active camera. They draw on top of sprites at their default z-order.

```cpp
void debugDraw(ResMut<RendererResource> renderer,
               Query<Transform2D, BoxCollider2D> query) {
    query.iter([&](const Transform2D& t, const BoxCollider2D& box) {
        Rect r{t.position.x - box.halfSize.x, t.position.y - box.halfSize.y,
               box.halfSize.x * 2.f, box.halfSize.y * 2.f};
        renderer->drawRect(r, Color::green());
    });
}
```

## Camera

The renderer manages one or more cameras. `DefaultPlugins` creates a default camera automatically.

```cpp
CameraHandle getDefaultCamera() const;
CameraHandle createCamera();
void         destroyCamera(CameraHandle camera);
void         setActiveCamera(CameraHandle camera);

void  setCameraPosition(CameraHandle camera, Vec2 position);
void  setCameraZoom(CameraHandle camera, float zoom);
void  setCameraRotation(CameraHandle camera, float rotation);
Vec2  getCameraPosition(CameraHandle camera) const;
float getCameraZoom(CameraHandle camera) const;

Vec2 screenToWorld(CameraHandle camera, Vec2 screenPos) const;
Vec2 worldToScreen(CameraHandle camera, Vec2 worldPos) const;
```

### Example: Camera Follow

If you are not using the `CameraFollow` component, you can follow a target manually:

```cpp
void cameraFollow(ResMut<RendererResource> renderer,
                  Query<Transform2D, With<PlayerTag>> query) {
    auto camera = renderer->getDefaultCamera();

    query.iter([&](const Transform2D& t) {
        Vec2 current = renderer->getCameraPosition(camera);
        Vec2 target = t.position;
        Vec2 lerped = current + (target - current) * 0.05f;
        renderer->setCameraPosition(camera, lerped);
    });
}
```

### Coordinate Conversion

`screenToWorld` converts a screen-space position (like mouse coordinates) to world space, accounting for camera position, zoom, and rotation.

```cpp
Vec2 worldPos = renderer->screenToWorld(camera, input->mousePosition());
```

`worldToScreen` does the reverse -- useful for placing UI elements at an entity's screen position.

## Clear Color

Set the background color that fills the screen each frame before any drawing:

```cpp
renderer->setClearColor(ColorF{0.1f, 0.1f, 0.15f, 1.0f});
```

## Logical Resolution

The renderer reports the logical resolution of the window. This matches the window size unless overridden:

```cpp
float w = renderer->logicalWidth();
float h = renderer->logicalHeight();
```

## Stats

For profiling and debug overlays:

```cpp
int32_t draws   = renderer->drawCalls();   // GPU draw calls this frame
int32_t sprites = renderer->spriteCount(); // sprites submitted this frame
```
