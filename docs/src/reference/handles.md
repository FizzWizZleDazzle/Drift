# Handles

**Header:** `include/drift/Handle.hpp`

Drift uses generational handles for safe, lightweight references to engine-managed resources such as textures, sounds, and fonts. A handle can detect when the resource it pointed to has been freed and replaced.

## Handle\<T\>

```cpp
template<typename T>
struct Handle {
    uint32_t id = 0;
};
```

The `T` parameter is a **phantom type** (tag struct) -- it exists only for compile-time type safety. A `TextureHandle` and a `SoundHandle` are distinct types even though both wrap a `uint32_t`.

### Bit Layout

A handle packs a 20-bit index and a 12-bit generation into a single `uint32_t`:

```
┌──────────────────┬────────────┐
│   generation(12) │ index(20)  │
└──────────────────┴────────────┘
         MSB             LSB
```

- **Index** (bits 0--19): slot position in the resource array. Supports up to 1,048,575 slots.
- **Generation** (bits 20--31): incremented each time a slot is reused. Supports up to 4,095 generations before wrapping.

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `valid()` | `bool valid() const` | Returns `true` if the handle is non-zero. A zero handle is the null/invalid sentinel. |
| `operator bool()` | `explicit operator bool() const` | Same as `valid()`. Allows `if (handle) { ... }`. |
| `index()` | `uint32_t index() const` | Extracts the 20-bit slot index. |
| `generation()` | `uint32_t generation() const` | Extracts the 12-bit generation counter. |
| `make()` | `static Handle make(uint32_t index, uint32_t gen)` | Constructs a handle from raw index and generation values. Used internally by resource pools. |
| `operator==` | `bool operator==(Handle o) const` | Equality comparison. |
| `operator!=` | `bool operator!=(Handle o) const` | Inequality comparison. |

### Usage

```cpp
TextureHandle tex = assets->load<Texture>("player.png");

if (tex.valid()) {
    sprite.texture = tex;
}

// Handles are lightweight value types -- copy freely
TextureHandle copy = tex;
assert(copy == tex);
```

### Stale Handle Detection

When a resource is freed and its slot is reused, the generation counter increments. Any old handle pointing to that slot will have a mismatched generation, so the resource system can detect and reject stale references.

## Concrete Handle Types

Each resource category has a tag struct and a corresponding type alias:

| Type Alias | Tag | Used For |
|------------|-----|----------|
| `TextureHandle` | `TextureTag` | GPU textures loaded from images or created from pixels |
| `SpritesheetHandle` | `SpritesheetTag` | Sprite sheet metadata (frame rects) |
| `AnimationHandle` | `AnimationTag` | Sprite animation clip data |
| `SoundHandle` | `SoundTag` | Loaded audio samples |
| `PlayingSoundHandle` | `PlayingSoundTag` | A currently-playing sound instance (for stopping/querying) |
| `FontHandle` | `FontTag` | Loaded bitmap or TTF fonts |
| `TilemapHandle` | `TilemapTag` | Tile map data |
| `EmitterHandle` | `EmitterTag` | Legacy particle emitter instances (handle-based API) |
| `CameraHandle` | `CameraTag` | Legacy camera references |

### Common Patterns

**Loading and storing a handle:**

```cpp
struct GameState : public Resource {
    TextureHandle playerTex;
    SoundHandle jumpSound;
};

void startup(ResMut<GameState> game, ResMut<AssetServer> assets) {
    game->playerTex = assets->load<Texture>("assets/player.png");
    game->jumpSound = assets->load<Sound>("assets/jump.wav");
}
```

**Checking validity before use:**

```cpp
if (game->jumpSound.valid()) {
    audio->playSound(game->jumpSound, 0.8f);
}
```

**Procedural texture creation:**

```cpp
uint8_t pixels[16 * 16 * 4];
// ... fill pixel data ...
TextureHandle tex = renderer->createTextureFromPixels(pixels, 16, 16);
```
