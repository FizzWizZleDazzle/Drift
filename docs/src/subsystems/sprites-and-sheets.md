# Sprites and Sheets

The `SpriteResource` manages spritesheets and frame-based animations. It handles uniform grid sheets, Aseprite imports, and animation playback with per-frame timing.

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
int32_t frameCount(SpritesheetHandle sheet) const;
```

`getFrame` returns the source rectangle for a frame at the given column and row in the grid. Use this to set a `Sprite` component's `srcRect` manually.

```cpp
Rect frame = sprites->getFrame(sheet, 3, 0); // column 3, row 0
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

## Animations

Animations play a sequence of frames from a spritesheet with per-frame timing control.

### Creating Animations

```cpp
AnimationHandle createAnimation(SpritesheetHandle sheet, const AnimationDef& def);
AnimationHandle createAnimationFromTag(SpritesheetHandle sheet, const char* tagName);
```

`AnimationDef` specifies the frame sequence and timing:

```cpp
struct AnimationDef {
    int32_t* frames    = nullptr;   // array of frame indices
    float*   durations = nullptr;   // per-frame duration in seconds
    int32_t  frameCount = 0;
    bool     looping    = true;
};
```

For uniform frame timing, fill the durations array with the same value:

```cpp
int32_t frames[] = {0, 1, 2, 3};
float durations[] = {0.1f, 0.1f, 0.1f, 0.1f};

AnimationDef walkDef;
walkDef.frames = frames;
walkDef.durations = durations;
walkDef.frameCount = 4;
walkDef.looping = true;

AnimationHandle walkAnim = sprites->createAnimation(sheet, walkDef);
```

### Aseprite Tags

If you loaded an Aseprite file, you can create animations directly from tags defined in the Aseprite editor:

```cpp
SpritesheetHandle sheet = sprites->loadAseprite("assets/hero.aseprite");
AnimationHandle idle = sprites->createAnimationFromTag(sheet, "idle");
AnimationHandle run  = sprites->createAnimationFromTag(sheet, "run");
AnimationHandle jump = sprites->createAnimationFromTag(sheet, "jump");
```

Frame indices and per-frame durations are pulled from the Aseprite tag data automatically.

### Updating and Querying Animations

```cpp
Rect updateAnimation(AnimationHandle anim, float dt);
Rect getAnimationFrame(AnimationHandle anim) const;
void resetAnimation(AnimationHandle anim);
bool isAnimationFinished(AnimationHandle anim) const;
SpritesheetHandle getAnimationSheet(AnimationHandle anim) const;
```

`updateAnimation` advances the animation by `dt` seconds and returns the current frame's source rectangle. Use this if you are driving animation manually rather than through the `SpriteAnimator` component.

```cpp
void animateManually(ResMut<SpriteResource> sprites, Res<Time> time,
                     QueryMut<Sprite> query, Res<GameState> state) {
    Rect frame = sprites->updateAnimation(state->currentAnim, time->delta);
    TextureHandle tex = sprites->getTexture(
        sprites->getAnimationSheet(state->currentAnim));

    query.iter([&](Sprite& s) {
        s.srcRect = frame;
        s.texture = tex;
    });
}
```

### Checking Completion

For non-looping animations (death, attack, landing), check if the animation has finished:

```cpp
if (sprites->isAnimationFinished(deathAnim)) {
    cmd.entity(entityId).despawn();
}
```

### Cleanup

```cpp
void destroyAnimation(AnimationHandle anim);
```

## ECS-Driven Animation: SpriteAnimator

For most gameplay, use the `SpriteAnimator` component instead of calling `updateAnimation` manually. The `SpriteAnimationPlugin` (included in `DefaultPlugins`) updates all `SpriteAnimator` components each frame and writes the resulting `srcRect` into the entity's `Sprite` component.

```cpp
#include <drift/components/SpriteAnimator.hpp>

cmd.spawn().insert(
    Transform2D{.position = {100.f, 200.f}},
    Sprite{.texture = sprites->getTexture(sheet)},
    SpriteAnimator{.animation = walkAnim}
);
```

See the [Sprite Animation](../components/sprite-animation.md) chapter for details on the component-driven approach.

## Complete Example

```cpp
struct PlayerAnims : public Resource {
    DRIFT_RESOURCE(PlayerAnims)
    SpritesheetHandle sheet;
    AnimationHandle idle;
    AnimationHandle run;
    AnimationHandle jump;
};

void setupAnims(ResMut<SpriteResource> sprites, ResMut<PlayerAnims> anims,
                Commands& cmd) {
    anims->sheet = sprites->loadAseprite("assets/hero.aseprite");
    anims->idle  = sprites->createAnimationFromTag(anims->sheet, "idle");
    anims->run   = sprites->createAnimationFromTag(anims->sheet, "run");
    anims->jump  = sprites->createAnimationFromTag(anims->sheet, "jump");

    TextureHandle tex = sprites->getTexture(anims->sheet);
    sprites->setPivot(anims->sheet, 16.f, 16.f);

    cmd.spawn().insert(
        Transform2D{.position = {200.f, 300.f}},
        Sprite{.texture = tex},
        SpriteAnimator{.animation = anims->idle},
        PlayerTag{}
    );
}

void switchAnim(Res<InputResource> input, Res<PlayerAnims> anims,
                QueryMut<SpriteAnimator, With<PlayerTag>> query) {
    query.iter([&](SpriteAnimator& animator) {
        if (input->keyHeld(Key::A) || input->keyHeld(Key::D)) {
            animator.animation = anims->run;
        } else {
            animator.animation = anims->idle;
        }
    });
}
```
