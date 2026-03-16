# Sprite Animation

The sprite animation system drives frame-by-frame animation on entities that have both a `Sprite` and a `SpriteAnimator` component. Each frame, the `SpriteAnimationPlugin` advances the animation timer and writes the correct `srcRect` into the `Sprite` component.

## Components

### AnimationClip

A single animation sequence: a list of source rectangles and timing information.

```cpp
struct AnimationClip {
    std::vector<Rect> frames;       // srcRect per frame
    float frameDuration = 0.1f;     // seconds per frame
    bool looping = true;
};
```

**Header:** `#include <drift/components/SpriteAnimator.hpp>`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `frames` | `vector<Rect>` | `{}` | Source rectangles within the texture, one per animation frame. |
| `frameDuration` | `float` | `0.1` | Time in seconds each frame is displayed. 0.1 = 10 FPS animation. |
| `looping` | `bool` | `true` | When `true`, the clip restarts after the last frame. When `false`, it stops on the last frame. |

### SpriteAnimator

Holds one or more `AnimationClip` entries and tracks playback state.

```cpp
struct SpriteAnimator {
    std::vector<AnimationClip> clips;
    int32_t currentClip = 0;
    int32_t currentFrame = 0;
    float elapsed = 0.f;
    bool playing = true;

    void play(int32_t clipIndex);
    void stop();
    bool isFinished() const;
};
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `clips` | `vector<AnimationClip>` | `{}` | All animation clips available on this entity. |
| `currentClip` | `int32_t` | `0` | Index of the currently active clip. |
| `currentFrame` | `int32_t` | `0` | Current frame index within the active clip. |
| `elapsed` | `float` | `0` | Time accumulated since the last frame advance. |
| `playing` | `bool` | `true` | Whether the animation is advancing. |

#### Methods

| Method | Description |
|--------|-------------|
| `play(int32_t clipIndex)` | Switch to a different clip. Resets frame and elapsed time, sets `playing = true`. |
| `stop()` | Pauses the animation on the current frame. |
| `isFinished()` | Returns `true` if the current clip is non-looping and has reached its last frame. |

## Plugin

The `SpriteAnimationPlugin` is included in `DefaultPlugins`. It registers a system in the `PostUpdate` phase that iterates all entities with both `SpriteAnimator` and `Sprite`, advancing the animation and writing the current frame's `Rect` into `Sprite::srcRect`.

If you are not using `DefaultPlugins`, add it manually:

```cpp
app.addPlugin<SpriteAnimationPlugin>();
```

## Basic usage

### Single animation clip

For a spritesheet with a horizontal row of 32x32 frames:

```cpp
void setup(ResMut<AssetServer> assets, Commands& cmd) {
    TextureHandle sheet = assets->load<Texture>("assets/character.png");

    AnimationClip walkClip;
    walkClip.frameDuration = 0.1f;
    walkClip.looping = true;
    for (int i = 0; i < 6; ++i) {
        walkClip.frames.push_back(Rect{i * 32.f, 0.f, 32.f, 32.f});
    }

    cmd.spawn().insert(
        Transform2D{.position = {400.f, 300.f}},
        Sprite{.texture = sheet, .origin = {16.f, 16.f}},
        SpriteAnimator{.clips = {walkClip}}
    );
}
```

### Multiple clips with switching

Store different animation states (idle, walk, jump) as separate clips and switch between them:

```cpp
enum Anim { Idle = 0, Walk = 1, Jump = 2 };

void setup(ResMut<AssetServer> assets, Commands& cmd) {
    TextureHandle sheet = assets->load<Texture>("assets/character.png");

    // Row 0: idle (4 frames), Row 1: walk (6 frames), Row 2: jump (3 frames)
    AnimationClip idle;
    idle.frameDuration = 0.2f;
    for (int i = 0; i < 4; ++i)
        idle.frames.push_back(Rect{i * 32.f, 0.f, 32.f, 32.f});

    AnimationClip walk;
    walk.frameDuration = 0.1f;
    for (int i = 0; i < 6; ++i)
        walk.frames.push_back(Rect{i * 32.f, 32.f, 32.f, 32.f});

    AnimationClip jump;
    jump.frameDuration = 0.15f;
    jump.looping = false;
    for (int i = 0; i < 3; ++i)
        jump.frames.push_back(Rect{i * 32.f, 64.f, 32.f, 32.f});

    cmd.spawn().insert(
        Transform2D{.position = {400.f, 300.f}},
        Sprite{.texture = sheet, .origin = {16.f, 32.f}},
        SpriteAnimator{.clips = {idle, walk, jump}}
    );
}
```

Switch clips based on input:

```cpp
void updateAnimation(Res<InputResource> input,
                     QueryMut<SpriteAnimator, Sprite> animated) {
    animated.iter([&](SpriteAnimator& animator, Sprite& sprite) {
        bool moving = input->keyHeld(Key::A) || input->keyHeld(Key::D);
        bool jumping = input->keyPressed(Key::Space);

        if (jumping) {
            animator.play(Anim::Jump);
        } else if (moving && animator.currentClip != Anim::Walk) {
            animator.play(Anim::Walk);
        } else if (!moving && !jumping && animator.currentClip != Anim::Idle) {
            animator.play(Anim::Idle);
        }

        // Flip sprite based on direction
        if (input->keyHeld(Key::A))
            sprite.flip = Flip::H;
        else if (input->keyHeld(Key::D))
            sprite.flip = Flip::None;
    });
}
```

### One-shot animation

For animations that play once and stop (death, attack, explosion):

```cpp
AnimationClip deathClip;
deathClip.frameDuration = 0.08f;
deathClip.looping = false;
for (int i = 0; i < 8; ++i)
    deathClip.frames.push_back(Rect{i * 64.f, 0.f, 64.f, 64.f});
```

Check when it finishes:

```cpp
void checkAnimationDone(QueryMut<SpriteAnimator> animators, Commands& cmd) {
    animators.iterWithEntity([&](EntityId id, SpriteAnimator& animator) {
        if (animator.isFinished()) {
            cmd.entity(id).despawn();
        }
    });
}
```

### Pausing and resuming

```cpp
void toggleAnimation(Res<InputResource> input,
                     QueryMut<SpriteAnimator> animators) {
    if (input->keyPressed(Key::P)) {
        animators.iter([](SpriteAnimator& a) {
            if (a.playing)
                a.stop();
            else
                a.playing = true;
        });
    }
}
```

## How it works

Each frame, the `SpriteAnimationPlugin` system does the following for every entity with both `SpriteAnimator` and `Sprite`:

1. If `playing` is `false`, skip.
2. Add the frame's delta time to `elapsed`.
3. While `elapsed >= frameDuration`, advance `currentFrame` by 1 and subtract `frameDuration`.
4. If `currentFrame` exceeds the last frame:
   - If `looping` is `true`, wrap back to frame 0.
   - If `looping` is `false`, clamp to the last frame and set `playing = false`.
5. Write `clips[currentClip].frames[currentFrame]` into `sprite.srcRect`.

Because the system writes to `Sprite::srcRect`, you should not manually set `srcRect` on entities that also have a `SpriteAnimator` -- the animation system will overwrite it.

## Notes

- `SpriteAnimator` requires a `Sprite` on the same entity. The animation system updates `Sprite::srcRect` directly.
- The plugin runs in `PostUpdate`, so animation state changes made during `Update` take effect in the same frame.
- Frame durations are in seconds. A `frameDuration` of `0.1` gives 10 FPS animation regardless of the game's actual frame rate.
- Calling `play()` with the same clip index that is already playing will restart the animation from frame 0.
