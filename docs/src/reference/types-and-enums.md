# Types and Enums

All types and enums listed here are defined in `include/drift/Types.hpp` and live in the `drift` namespace.

## Phase

Controls when a system runs within a single frame. Phases execute in order from top to bottom.

```cpp
enum class Phase {
    Startup = 0,   // Runs once before the first frame
    PreUpdate,     // Runs before Update (input polling, event dispatch)
    Update,        // Main game logic
    PostUpdate,    // After Update (hierarchy propagation, physics sync)
    Extract,       // Prepare data for rendering
    Render,        // GPU draw calls
    RenderFlush,   // Swap buffers, present
};
```

Most game systems should use `Phase::Update`. The engine's built-in plugins use the other phases internally. `Phase::Startup` systems run exactly once.

## BodyType

Determines how a physics body behaves in the simulation.

```cpp
enum class BodyType {
    Static = 0,    // Does not move; infinite mass; walls, floors, platforms
    Dynamic,       // Fully simulated; affected by forces, gravity, collisions
    Kinematic,     // Moves only via velocity; not affected by forces; moving platforms
};
```

## Key

Keyboard scan codes mirroring SDL scancodes. Used with `InputResource::keyHeld()`, `keyPressed()`, and `keyReleased()`.

```cpp
enum class Key {
    Unknown = 0,

    // Letters
    A = 4, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // Numbers
    Num1 = 30, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9, Num0,

    // Special keys
    Return = 40,
    Escape = 41,
    Backspace = 42,
    Tab = 43,
    Space = 44,

    // Function keys
    F1 = 58, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // Arrow keys
    Right = 79,
    Left = 80,
    Down = 81,
    Up = 82,

    // Modifiers
    LCtrl = 224,
    LShift = 225,
    LAlt = 226,
    RCtrl = 228,
    RShift = 229,
    RAlt = 230,

    Max_ = 512,  // Internal: array size sentinel
};
```

## MouseButton

Mouse button identifiers for `InputResource::mouseButtonHeld()`, `mouseButtonPressed()`, and `mouseButtonReleased()`.

```cpp
enum class MouseButton {
    Left = 1,
    Middle = 2,
    Right = 3,
};
```

## Flip

Bitflags for sprite flipping. Combine with `|`.

```cpp
enum class Flip {
    None = 0,
    H    = 1 << 0,  // Flip horizontally
    V    = 1 << 1,  // Flip vertically
};
```

Usage:

```cpp
Sprite s;
s.flip = Flip::H;              // horizontal flip only
s.flip = Flip::H | Flip::V;    // flip both axes
```

The `&` operator returns `bool`, so you can test individual flags:

```cpp
if (s.flip & Flip::H) { /* horizontally flipped */ }
```

## Anchor

Anchor point for UI positioning, defining which corner or edge a panel is aligned to.

```cpp
enum class Anchor {
    TopLeft = 0,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};
```

## PanelFlags

Bitflags for UI panel behavior. Combine with `|`.

```cpp
enum class PanelFlags {
    None       = 0,
    NoBg       = 1 << 0,  // No background fill
    Border     = 1 << 1,  // Draw border outline
    Scrollable = 1 << 2,  // Content can scroll
};
```

Usage:

```cpp
PanelFlags flags = PanelFlags::Border | PanelFlags::Scrollable;
```

## LogLevel

Severity levels for the logging system. See the [Logging](logging.md) chapter for details.

```cpp
enum class LogLevel {
    Trace = 0,  // Verbose diagnostic output
    Debug,      // Development-time information
    Info,       // Normal operational messages
    Warn,       // Something unexpected but recoverable
    Error,      // Something failed
};
```

## ECS Identity Types

```cpp
using EntityId = uint64_t;
constexpr EntityId InvalidEntityId = 0;
using ComponentId = uint64_t;
```

- **`EntityId`** -- a 64-bit identifier for an entity in the ECS world. Internally backed by flecs entity IDs.
- **`InvalidEntityId`** -- the zero value, used as a null sentinel. Always check before using an `EntityId` stored from a previous frame.
- **`ComponentId`** -- a 64-bit identifier for a component type, also a flecs entity ID internally.
