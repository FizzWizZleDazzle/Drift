# Sprites and Sheets

The `SpriteResource` manages spritesheets: uniform grids cut from a texture, and Aseprite files with their frame durations and tags. The `SpriteAnimator` component plays a sheet's frames over time; see [Sprite Animation](../components/sprite-animation.md).

**Header:** `#include <drift/resources/SpriteResource.hpp>`

**Plugin:** `SpritePlugin` (included in `DefaultPlugins`)

## Spritesheets

A spritesheet is a texture divided into a uniform grid of frames. The `SpriteResource` tracks the frame layout so you can refer to frames by index.

### Creating from a Grid

```cpp
SpritesheetHandle createSpritesheet(TextureHandle texture, int32_t frameW, int32_t frameH);
```

Given a texture and frame dimensions, this computes a grid of frames. For example, a 256x128 texture with 32x32 frames produces an 8x4 grid (32 frames).

```cpp
void setup(ResMut<RendererResource> renderer, ResMut<SpriteResource> sprites) {
    TextureHandle tex = renderer->loadTexture("assets/characters.png");
    SpritesheetHandle sheet = sprites->createSpritesheet(tex, 32, 32);
}
```

### Loading from Aseprite

```cpp
SpritesheetHandle loadAseprite(const char* path);
```

Loads an Aseprite file (`.ase` / `.aseprite`) directly. Frame sizes, durations, and tags are parsed from the file. The texture is created automatically from the embedded pixel data.

```cpp
SpritesheetHandle sheet = sprites->loadAseprite("assets/hero.aseprite");
```

### Querying Frames

```cpp
Rect    getFrame(SpritesheetHandle sheet, int32_t col, int32_t row) const;
Rect    frameAt(SpritesheetHandle sheet, int32_t index) const;
int32_t frameCount(SpritesheetHandle sheet) const;
```

`getFrame` returns the source rectangle for a frame at the given column and row in the grid, and `frameAt` the one at an index, counted left to right, top to bottom from 0; animation clips use these indices. Use either to set a `Sprite` component's `srcRect` for a single frame.

```cpp
Rect frame = sprites->getFrame(sheet, 3, 0); // column 3, row 0
Rect same  = sprites->frameAt(sheet, 3);     // index 3 on a sheet 4 or more frames wide
```

### Pivot

The pivot defines the origin point for rendering relative to the frame's top-left corner. Setting it to the center of the frame causes sprites to rotate and scale around their center.

```cpp
void setPivot(SpritesheetHandle sheet, float x, float y);
Vec2 getPivot(SpritesheetHandle sheet) const;
```

```cpp
sprites->setPivot(sheet, 16.f, 16.f); // center of a 32x32 frame
```

### Other Accessors

```cpp
TextureHandle getTexture(SpritesheetHandle sheet) const;
void          destroySpritesheet(SpritesheetHandle sheet);
```

## Aseprite Tags

```cpp
AnimationClip clipFromTag(SpritesheetHandle sheet, const char* tagName) const;
std::vector<std::string> tagNames(SpritesheetHandle sheet) const;
```

`clipFromTag` turns a tag of a loaded Aseprite file into an `AnimationClip` for the `SpriteAnimator`: the tag's frames in the order it plays them (forward, reverse or ping-pong), each with its own duration from the file. A tag set to repeat once plays once; any other loops. A tag the file does not have logs an error naming the tags it has and returns an empty clip. `tagNames` lists the tags in file order.

```cpp
SpritesheetHandle knight = sprites->loadAseprite("assets/knight.aseprite");
SpriteAnimator anim;
anim.clips = {
    {"idle", sprites->clipFromTag(knight, "idle")},
    {"run", sprites->clipFromTag(knight, "run")},
    {"die", sprites->clipFromTag(knight, "die")},
};
anim.current = "idle";

cmd.spawn().insert(
    Transform2D{.position = {200.f, 300.f}},
    Sprite{.texture = sprites->getTexture(knight)},
    anim
);
```
