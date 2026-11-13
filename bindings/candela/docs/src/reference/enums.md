<!-- Written by scripts/candela-reference.py from the drift module and
     bindings/candela/docs/api-notes.md. Edit those, not this page. -->

# Enums

Every enum the `drift` module defines, with its variants.

## Schedule

When a system runs; see [`@system`](attributes.md#system).

```rust
@system(schedule: Schedule::Startup)
fn setup(cmd: Commands) { }
```

```rust
enum Schedule { Startup, Update }
```

| Variant | Meaning |
|---------|---------|
| `Startup` | Once, before the first frame |
| `Update` | Every frame |

## Align

Which point of a `Text` sits on its position, left to right; the text also turns around this point.

```rust
Text { value: "Title", align: Align::Center, .. }
```

```rust
enum Align { Left, Center, Right }
```

## VAlign

Which point of a `Text` sits on its position, top to bottom.

```rust
Text { value: "Paused", align: Align::Center, valign: VAlign::Middle, .. }
```

```rust
enum VAlign { Top, Middle, Bottom }
```

## Blend

How a drawable mixes with what is behind it.

```rust
Sprite { image: art.glow, blend: Blend::Additive, .. }
```

```rust
enum Blend { Alpha, Additive, Multiply, Screen, Premultiplied, Opaque }
```

| Variant | Meaning |
|---------|---------|
| `Alpha` | Ordinary transparency |
| `Additive` | Adds light: glows, fire, sparks |
| `Multiply` | Darkens: shadows, tints |
| `Screen` | Lightens softly |
| `Premultiplied` | For images whose colors are already multiplied by their alpha |
| `Opaque` | Ignores alpha |

## Filter

How a texture looks scaled; see [`Texture.set_filter`](assets.md#textureset_filter).

```rust
art.background.set_filter(Filter::Linear);
```

```rust
enum Filter { Nearest, Linear }
```

| Variant | Meaning |
|---------|---------|
| `Nearest` | Crisp pixels |
| `Linear` | Smooth |

## BodyKind

What kind of physics body a `RigidBody` is.

```rust
RigidBody { kind: BodyKind::Kinematic, .. }
```

```rust
enum BodyKind { Static, Dynamic, Kinematic }
```

| Variant | Meaning |
|---------|---------|
| `Static` | Never moves |
| `Dynamic` | Falls, collides and is pushed |
| `Kinematic` | Moves only by its Velocity; pushes dynamic bodies and is not pushed |

## JointKind

```rust
Joint { kind: JointKind::Revolute, target: wheel, .. }
```

```rust
enum JointKind { Distance, Revolute, Prismatic, Weld }
```

| Variant | Meaning |
|---------|---------|
| `Distance` | Keeps the anchors a distance apart, or within `min_length` and `max_length`, as a rod, spring or rope |
| `Revolute` | A hinge: the bodies turn about the anchor |
| `Prismatic` | A slider along `axis` |
| `Weld` | Glues the bodies together, or holds them with a spring |

## Ease

Easing curves for tweens. `In` starts slow, `Out` ends slow and `InOut` does both. `Back` overshoots, `Elastic` wobbles, `Bounce` bounces, and `Spring` settles like a spring.

```rust
tween().move_to(vec2(400.0, 300.0), 0.6, Ease::OutBack)
```

```rust
enum Ease {
    Linear, InSine, OutSine, InOutSine, InQuad, OutQuad, InOutQuad, InCubic,
    OutCubic, InOutCubic, InQuart, OutQuart, InOutQuart, InQuint, OutQuint,
    InOutQuint, InExpo, OutExpo, InOutExpo, InCirc, OutCirc, InOutCirc,
    InBack, OutBack, InOutBack, InElastic, OutElastic, InOutElastic, InBounce,
    OutBounce, InOutBounce, Spring,
}
```

## EmitShape

Where a `ParticleEmitter`'s particles start.

```rust
ParticleEmitter { shape: EmitShape::Ring, shape_radius: 40.0, .. }
```

```rust
enum EmitShape { Point, Circle, Ring, Box, Line }
```

| Variant | Meaning |
|---------|---------|
| `Point` | At the entity's position |
| `Circle` | Inside a circle of `shape_radius` |
| `Ring` | On a circle of `shape_radius` |
| `Box` | Inside a box of half size `shape_extents` |
| `Line` | Along the line from `-shape_extents` to `shape_extents` |

## NoiseType

The kind of [`Noise`](procgen.md#noise).

```rust
Noise { kind: NoiseType::Perlin, frequency: 0.05, .. }
```

```rust
enum NoiseType { Value, Perlin, Simplex }
```

| Variant | Meaning |
|---------|---------|
| `Value` | Blocky |
| `Perlin` | Smooth |
| `Simplex` | Smooth, with fewer grid patterns |

## OccluderShape

The shape of a [`LightOccluder`](components.md#lightoccluder).

```rust
LightOccluder { shape: OccluderShape::FromCollider, .. }
```

```rust
enum OccluderShape { Box, Circle, Polygon, FromCollider }
```

| Variant | Meaning |
|---------|---------|
| `Box` | A box of `half_size` |
| `Circle` | A circle of `radius` |
| `Polygon` | The corners in `points` |
| `FromCollider` | The entity's collider |

## Rolloff

How an [`AudioSource`](components.md#audiosource) gets quieter with distance.

```rust
AudioSource { spatial: true, rolloff: Rolloff::Inverse, .. }
```

```rust
enum Rolloff { Linear, Inverse }
```

| Variant | Meaning |
|---------|---------|
| `Linear` | Evenly from `min_distance` to `max_distance` |
| `Inverse` | Quickly near, slowly far |

## Key

Keyboard keys, by position on a US layout.

```rust
if input.key_pressed(Key::Escape) { phase.set(Phase::Paused); }
```

```rust
enum Key {
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y,
    Z, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9, Num0, Return,
    Escape, Backspace, Tab, Space, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10,
    F11, F12, Right, Left, Down, Up, LCtrl, LShift, LAlt, RCtrl, RShift, RAlt,
}
```

## MouseButton

```rust
if input.mouse_pressed(MouseButton::Left) { shoot(); }
```

```rust
enum MouseButton { Left, Middle, Right, X1, X2 }
```

## GamepadButton

Gamepad buttons in the Xbox layout: `South` is A, `East` is B, `West` is X and `North` is Y.

```rust
input.bind_button("jump", GamepadButton::South);
```

```rust
enum GamepadButton {
    South, East, West, North, Back, Guide, Start, LeftStick, RightStick,
    LeftShoulder, RightShoulder, DpadUp, DpadDown, DpadLeft, DpadRight,
}
```

## GamepadAxis

Sticks read -1 to 1 with Y down; triggers read 0 to 1.

```rust
input.bind_stick("move", GamepadAxis::LeftX, GamepadAxis::LeftY);
```

```rust
enum GamepadAxis { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger }
```
