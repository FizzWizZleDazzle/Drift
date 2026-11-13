# Sprite Animation

A `SpriteAnimator` plays named clips on an entity's `Sprite`. A clip is a spritesheet, the frames to show from it, and how long each one shows. Each frame, the `SpriteAnimationPlugin` moves every animator on and puts the current frame's texture and region on the sprite.

**Header:** `#include <drift/components/SpriteAnimator.hpp>`

## Clips

```cpp
struct AnimationClip {
    SpritesheetHandle sheet;
    std::vector<int32_t> frames;    // frame indices into the sheet
    std::vector<float> seconds;     // per frame, or one value for all of them
    bool looping = true;

    float frameSeconds(size_t i) const;
    float length() const;
    AnimationClip once() const;
};

AnimationClip clip(SpritesheetHandle sheet, std::vector<int32_t> frames, float seconds);
AnimationClip clip(SpritesheetHandle sheet, std::vector<int32_t> frames, std::vector<float> seconds);
```

`clip` makes a clip with one duration for every frame, or with one duration per frame; a list whose length matches neither logs an error. Frame indices count from 0 at the sheet's top-left, left to right, then down, as `SpriteResource::frameAt` does. `once()` returns the clip set to play one time instead of looping. `SpriteResource::clipFromTag` makes a clip from an Aseprite tag with the file's own durations; see [Sprites and Sheets](../subsystems/sprites-and-sheets.md#aseprite-tags).

Each clip carries its sheet, so clips from different sheets can share one animator: switching clips switches the sprite's texture as well as its region.

## SpriteAnimator

```cpp
struct NamedClip {
    std::string name;
    AnimationClip clip;
};

struct SpriteAnimator {
    std::vector<NamedClip> clips;
    std::string current;       // the clip playing
    std::string then;          // played when a one-shot ends; empty holds the last frame
    bool once = false;         // the current clip plays one time, even a looping one
    float speed = 1.f;
    bool playing = true;
    int32_t frame = 0;         // index into the current clip's frames
    float frameTime = 0.f;     // seconds into the current frame
    std::string ended;         // the last clip that played to its end

    bool play(const std::string& name);
    bool playOnce(const std::string& name, const std::string& then = {});
    void pause();
    void resume();
    bool finished(std::string_view name) const;
    float time() const;
    void seek(float t);
    const AnimationClip* find(std::string_view name) const;
    void advance(float dt, std::vector<std::string>* endedOut = nullptr);
};
```

| Method | Description |
|--------|-------------|
| `play(name)` | Switches to a clip from its first frame. The clip already playing keeps going, so calling it every frame is fine; a clip that ended starts again. |
| `playOnce(name, then)` | Plays a clip one time, even a looping one, then switches to `then`. An empty `then` holds the last frame. |
| `pause()`, `resume()` | Stop and continue time on the current frame. |
| `finished(name)` | Whether that clip played to its end and no other clip has started since. |
| `time()` | Seconds into the current clip. |
| `seek(t)` | Moves to `t` seconds into the current clip: wrapped for a looping clip, clamped for one that plays once. |
| `advance(dt, endedOut)` | Moves the clip on by `dt` seconds times `speed`. The plugin calls it; tests and custom loops can too. |

A name the animator does not have is an error: `play` and `playOnce` log it with the names the animator has, return `false` and change nothing.

`speed` scales time: `2.f` plays twice as fast and `0.f` freezes the clip.

## AnimationFinished

When a clip plays to its end (a clip made with `once()`, or one started with `playOnce`), the plugin sends an `AnimationFinished` event naming the entity and the clip. Read it with an `EventReader` instead of checking `finished` every frame:

```cpp
struct AnimationFinished {
    EntityId entity;
    std::string clip;
};
```

```cpp
void removeFallen(EventReader<AnimationFinished> done, Commands& cmd) {
    for (const auto& e : done) {
        if (e.clip == "die") cmd.entity(e.entity).despawn();
    }
}
```

## Plugin

The `SpriteAnimationPlugin` is included in `DefaultPlugins`. It registers a system in the `PostUpdate` phase that advances every entity with both `SpriteAnimator` and `Sprite`, sends `AnimationFinished`, and writes the current frame's texture and `srcRect` into the `Sprite`. When the new frame has a different size from the old one, the sprite's `origin` scales with it, so the sprite keeps its anchor.

If you are not using `DefaultPlugins`, add it after `SpritePlugin`:

```cpp
app.addPlugin<SpriteAnimationPlugin>();
```

## Usage

### Clips from two sheets

```cpp
void setup(ResMut<SpriteResource> sprites, ResMut<RendererResource> renderer, Commands& cmd) {
    SpritesheetHandle hero = sprites->createSpritesheet(renderer->loadTexture("assets/hero.png"), 32, 32);
    SpritesheetHandle slash = sprites->createSpritesheet(renderer->loadTexture("assets/slash.png"), 48, 32);

    SpriteAnimator anim;
    anim.clips = {
        {"idle", clip(hero, {0, 1, 2, 3}, 0.15f)},
        {"run", clip(hero, {8, 9, 10, 11, 12, 13}, 0.08f)},
        {"attack", clip(slash, {0, 1, 2}, {0.05f, 0.05f, 0.2f}).once()},
    };
    anim.current = "idle";

    cmd.spawn().insert(
        Transform2D{.position = {400.f, 300.f}},
        Sprite{.texture = sprites->getTexture(hero), .origin = {16.f, 16.f}},
        anim
    );
}
```

### Switching by name

```cpp
void heroAnimation(Res<InputResource> input, QueryMut<SpriteAnimator, Sprite, With<Player>> heroes) {
    heroes.iter([&](SpriteAnimator& anim, Sprite& sprite) {
        bool left = input->keyHeld(Key::A), right = input->keyHeld(Key::D);
        if (input->keyPressed(Key::Space)) {
            anim.playOnce("attack", "idle");
        } else if (!anim.once) {
            anim.play(left || right ? "run" : "idle");
        }
        if (left) sprite.flip = Flip::H;
        else if (right) sprite.flip = Flip::None;
    });
}
```

`anim.once` is true while a one-shot plays, so the check above lets the attack finish before running or idling takes over.

## Notes

- The plugin writes `Sprite::texture` and `Sprite::srcRect` on animated entities; set `Sprite::srcRect` yourself only on sprites without a `SpriteAnimator`.
- The plugin runs in `PostUpdate`, so a clip switched during `Update` shows in the same frame.
- A clip's sheet is made at run time, so scenes do not save it; give a loaded animator its clips again.

## Updating code written with numbered clips

Clips used to be lists of source rectangles with one `frameDuration`, held in a list and picked by index with `currentClip`. They are now sheet frames with per-frame `seconds`, held by name.

Before:

```cpp
AnimationClip walk;
walk.frameDuration = 0.1f;
for (int i = 0; i < 6; ++i) walk.frames.push_back(Rect{i * 32.f, 0.f, 32.f, 32.f});
SpriteAnimator anim{.clips = {walk}};
anim.play(0);
if (anim.isFinished()) { /* ... */ }
```

After:

```cpp
SpriteAnimator anim{.clips = {{"walk", clip(sheet, {0, 1, 2, 3, 4, 5}, 0.1f)}}, .current = "walk"};
anim.play("walk");
if (anim.finished("walk")) { /* ... */ }
```

The `AnimationHandle` animations of `SpriteResource` (`createAnimation`, `createAnimationFromTag`, `updateAnimation` and the rest) are replaced by `SpriteAnimator` and `clipFromTag`.
