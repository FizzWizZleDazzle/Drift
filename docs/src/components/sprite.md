# Sprite

The `Sprite` component makes an entity visible by drawing a textured quad at the entity's `Transform2D` position. It controls which texture (or sub-region of a texture) to draw, the origin point, tint color, flip mode, draw order, and visibility.

## Definition

```cpp
struct Sprite {
    TextureHandle texture;
    Rect srcRect = {};              // {} = full texture
    Vec2 origin = {};               // pivot point in pixels (0,0 = top-left)
    Color tint = Color::white();
    Flip flip = Flip::None;
    float zOrder = 0.f;
    bool visible = true;
};
```

**Header:** `#include <drift/components/Sprite.hpp>`

## Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `texture` | `TextureHandle` | (invalid) | Handle returned by `AssetServer::load<Texture>()` or `RendererResource::createTextureFromPixels()` |
| `srcRect` | `Rect` | `{0,0,0,0}` | Source rectangle within the texture. All zeros means the entire texture is drawn. |
| `origin` | `Vec2` | `{0,0}` | Pivot point in pixel coordinates relative to the sprite's top-left corner. `{0,0}` is the top-left; for a 32x32 sprite, `{16,16}` is the center. |
| `tint` | `Color` | `Color::white()` | Multiplicative color tint. White (`255,255,255,255`) means no tint. |
| `flip` | `Flip` | `Flip::None` | Horizontal and/or vertical flip. Values: `Flip::None`, `Flip::H`, `Flip::V`, or combined with `Flip::H | Flip::V`. |
| `zOrder` | `float` | `0.f` | Draw order. Higher values are drawn on top of lower values. |
| `visible` | `bool` | `true` | When `false`, the sprite is skipped during rendering. |

## Basic usage

Load a texture and spawn a sprite entity:

```cpp
void setup(ResMut<AssetServer> assets, Commands& cmd) {
    TextureHandle tex = assets->load<Texture>("assets/player.png");

    cmd.spawn()
        .insert(
            Transform2D{.position = {400.f, 300.f}},
            Sprite{.texture = tex}
        );
}
```

## Source rectangles (spritesheets)

Use `srcRect` to draw a sub-region of a larger texture. This is how you display individual frames from a spritesheet:

```cpp
// Draw the 3rd 32x32 tile from a horizontal strip
Sprite sprite;
sprite.texture = sheetTexture;
sprite.srcRect = {64.f, 0.f, 32.f, 32.f};  // x=64, y=0, w=32, h=32
```

When `srcRect` is `{0, 0, 0, 0}` (the default), the entire texture is used.

## Origin (pivot point)

The origin determines which pixel of the sprite sits at the entity's `Transform2D` position. It also acts as the center for rotation and scaling.

```cpp
// Center pivot for a 32x32 sprite
Sprite{.texture = tex, .origin = {16.f, 16.f}}

// Bottom-center pivot (feet of a character)
Sprite{.texture = tex, .origin = {16.f, 32.f}}
```

If origin is left as `{0, 0}`, the top-left corner of the sprite will be placed at the entity's position.

## Tinting

The tint color is multiplied with the texture color. Use it for damage flashes, team colors, or fading:

```cpp
// Red damage flash
sprite.tint = Color::red();

// Semi-transparent
sprite.tint = Color{255, 255, 255, 128};

// Custom color
sprite.tint = Color{100, 200, 255, 255};
```

## Flipping

Flip a sprite horizontally, vertically, or both:

```cpp
// Face left
sprite.flip = Flip::H;

// Upside down
sprite.flip = Flip::V;

// Both axes
sprite.flip = Flip::H | Flip::V;
```

A common pattern is flipping the sprite based on movement direction:

```cpp
void updateFacing(QueryMut<Sprite, Velocity2D> entities) {
    entities.iter([](Sprite& sprite, Velocity2D& vel) {
        if (vel.linear.x > 0.f)
            sprite.flip = Flip::None;
        else if (vel.linear.x < 0.f)
            sprite.flip = Flip::H;
    });
}
```

## Draw order

The `zOrder` field controls the layering of sprites. Higher values appear on top:

```cpp
// Background behind everything
Sprite{.texture = bgTex, .zOrder = -10.f}

// Player on top of background
Sprite{.texture = playerTex, .zOrder = 1.f}

// UI overlay on top of everything
Sprite{.texture = uiTex, .zOrder = 100.f}
```

Sprites with the same `zOrder` have no guaranteed relative order.

## Visibility

Toggle visibility without removing the component:

```cpp
void toggleVisibility(QueryMut<Sprite> sprites) {
    sprites.iter([](Sprite& s) {
        s.visible = !s.visible;
    });
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

using namespace drift;

struct GameState : public Resource {
    TextureHandle playerTex;
    TextureHandle bgTex;
};

void setup(ResMut<GameState> game, ResMut<AssetServer> assets, Commands& cmd) {
    game->playerTex = assets->load<Texture>("assets/player.png");
    game->bgTex = assets->load<Texture>("assets/background.png");

    // Camera
    cmd.spawn().insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});

    // Background
    cmd.spawn().insert(
        Transform2D{},
        Sprite{.texture = game->bgTex, .zOrder = -1.f}
    );

    // Player
    cmd.spawn().insert(
        Transform2D{.position = {400.f, 300.f}},
        Sprite{
            .texture = game->playerTex,
            .origin = {16.f, 16.f},
            .tint = Color::white(),
            .zOrder = 1.f
        }
    );
}

int main() {
    App app;
    app.setConfig({.title = "Sprite Example", .width = 800, .height = 600});
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameState>();
    app.startup<setup>();
    return app.run();
}
```

## Notes

- A `Sprite` without a valid `TextureHandle` will not be drawn. Always load the texture first.
- The rendering system requires both `Transform2D` and `Sprite` on an entity for it to appear.
- For animated sprites, pair `Sprite` with a `SpriteAnimator` component. The animation system automatically updates `srcRect` each frame. See [Sprite Animation](sprite-animation.md).
