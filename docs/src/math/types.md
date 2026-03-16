# Math Types

All math types are defined in `include/drift/Types.hpp` and live in the `drift` namespace.

## Vec2

A 2D floating-point vector used throughout the engine for positions, velocities, directions, and sizes.

```cpp
struct Vec2 {
    float x = 0.f, y = 0.f;
};
```

### Construction

```cpp
Vec2 a;                    // {0, 0}
Vec2 b{3.f, 4.f};         // {3, 4}
Vec2 c = Vec2::zero();    // {0, 0}
Vec2 d = Vec2::one();     // {1, 1}
```

### Operators

| Operator | Description |
|----------|-------------|
| `a + b` | Component-wise addition, returns new `Vec2` |
| `a - b` | Component-wise subtraction, returns new `Vec2` |
| `a * s` | Scalar multiplication, returns new `Vec2` |
| `s * a` | Scalar multiplication (left-hand), returns new `Vec2` |
| `a += b` | In-place addition |
| `a -= b` | In-place subtraction |
| `a *= s` | In-place scalar multiplication |

```cpp
Vec2 pos{10.f, 20.f};
Vec2 vel{1.f, 0.f};
float dt = 0.016f;

pos += vel * dt;           // move by velocity * delta time
Vec2 offset = 2.f * vel;  // left-hand scalar multiply
```

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `length()` | `float length() const` | Euclidean length: `sqrt(x*x + y*y)` |
| `lengthSq()` | `float lengthSq() const` | Squared length (avoids the square root; useful for distance comparisons) |
| `normalized()` | `Vec2 normalized() const` | Returns a unit-length vector in the same direction. Returns `{0,0}` if the length is near zero. |
| `dot()` | `float dot(Vec2 o) const` | Dot product: `x*o.x + y*o.y` |

```cpp
Vec2 dir = (target - pos).normalized();
float dist = (target - pos).length();
float distSq = (target - pos).lengthSq();  // cheaper than length()

if (dir.dot(forward) > 0.f) {
    // target is in front of us
}
```

### Static Factories

| Factory | Value |
|---------|-------|
| `Vec2::zero()` | `{0, 0}` |
| `Vec2::one()` | `{1, 1}` |

Both are `constexpr` and can be used in compile-time contexts.

## Vec3

A minimal 3D vector used internally by the engine. Not commonly needed in game code.

```cpp
struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};
```

## Vec4

A 4D vector. Used internally for shader parameters and color operations.

```cpp
struct Vec4 {
    float x = 0.f, y = 0.f, z = 0.f, w = 0.f;
};
```

## Rect

An axis-aligned rectangle defined by its top-left corner and dimensions.

```cpp
struct Rect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};
```

### Construction

```cpp
Rect r;                           // {0, 0, 0, 0}
Rect r{10.f, 20.f, 64.f, 32.f};  // x=10, y=20, width=64, height=32
```

### Methods

| Method | Description |
|--------|-------------|
| `contains(Vec2 p)` | Returns `true` if `p` is inside the rectangle (left/top inclusive, right/bottom exclusive). |
| `overlaps(Rect o)` | Returns `true` if this rectangle and `o` overlap. |

```cpp
Rect bounds{0.f, 0.f, 100.f, 100.f};
Vec2 mouse{50.f, 50.f};

if (bounds.contains(mouse)) {
    // mouse is inside
}

Rect other{80.f, 80.f, 40.f, 40.f};
if (bounds.overlaps(other)) {
    // rectangles overlap
}
```

## Color

An 8-bit-per-channel RGBA color. This is the primary color type used for sprite tinting, UI elements, and debug drawing.

```cpp
struct Color {
    uint8_t r = 255, g = 255, b = 255, a = 255;
};
```

### Construction

```cpp
Color white;                        // {255, 255, 255, 255}
Color red{255, 0, 0, 255};          // opaque red
Color semiTransparent{0, 0, 0, 128}; // 50% transparent black
Color blue{0, 0, 255};              // alpha defaults to 255
```

### Static Factories

| Factory | Value |
|---------|-------|
| `Color::white()` | `{255, 255, 255, 255}` |
| `Color::black()` | `{0, 0, 0, 255}` |
| `Color::red()` | `{255, 0, 0, 255}` |
| `Color::green()` | `{0, 255, 0, 255}` |
| `Color::blue()` | `{0, 0, 255, 255}` |

All are `constexpr`.

```cpp
Sprite s;
s.tint = Color::red();

// Dynamic color
s.tint = Color{
    static_cast<uint8_t>(128 + 127 * sinf(time)),
    static_cast<uint8_t>(128 + 127 * cosf(time)),
    200, 255
};
```

## ColorF

A floating-point RGBA color with components in the `[0.0, 1.0]` range. Used in the particle system and UI theming where blending precision matters.

```cpp
struct ColorF {
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
};
```

### Construction

```cpp
ColorF white;                         // {1, 1, 1, 1}
ColorF orange{1.f, 0.5f, 0.f, 1.f};  // opaque orange
ColorF fade{1.f, 1.f, 1.f, 0.f};     // fully transparent
```

### Usage in Particles

Particle emitters use `ColorF` for start and end colors to allow smooth interpolation over the particle lifetime:

```cpp
EmitterConfig c;
c.colorStart = {1.f, 0.9f, 0.2f, 1.f};  // bright yellow
c.colorEnd   = {1.f, 0.3f, 0.0f, 0.f};  // orange, fading to invisible
```
