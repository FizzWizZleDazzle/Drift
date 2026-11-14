<!-- Written by scripts/candela-reference.py from the doc comments in
     bindings/candela/drift/src. Edit those, not this page. -->

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

Which point of a `Text` sits on its position, left to right; the text also turns around this
point.

```rust
Text { value: "Title", align: Align::Center, .. }
```

```rust
enum Align { Left, Center, Right }
```

| Variant | Meaning |
|---------|---------|
| `Left` | The position is the text's left edge. |
| `Center` | The position is the text's middle. |
| `Right` | The position is the text's right edge. |

## VAlign

Which point of a `Text` sits on its position, top to bottom.

```rust
Text { value: "Paused", align: Align::Center, valign: VAlign::Middle, .. }
```

```rust
enum VAlign { Top, Middle, Bottom }
```

| Variant | Meaning |
|---------|---------|
| `Top` | The position is the text's top edge. |
| `Middle` | The position is the text's middle. |
| `Bottom` | The position is the text's bottom edge. |

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

What kind of physics body a `RigidBody` is: whether it moves, and what moves it.

A static body is level geometry, a dynamic body is moved by the physics world, and a
kinematic body is moved by your code through its `Velocity`. Two bodies collide only when
at least one of them is dynamic.

```rust
RigidBody { kind: BodyKind::Kinematic, .. }
```

```rust
enum BodyKind { Static, Dynamic, Kinematic }
```

| Variant | Meaning |
|---------|---------|
| `Static` | Never moves; collides with dynamic bodies only. Floors and walls. |
| `Dynamic` | Moved by forces, gravity and collisions. Needs a `Velocity` to show its motion: without one the entity stays where it was drawn. |
| `Kinematic` | Moves only by its `Velocity`, set by code; pushes dynamic bodies and is not pushed back. Ignores static and other kinematic bodies. Moving platforms and doors. |

## Ease

Easing curves for tweens. `In` starts slow, `Out` ends slow and `InOut` does both. `Back`
overshoots, `Elastic` wobbles, `Bounce` bounces, and `Spring` settles like a spring.

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

| Variant | Meaning |
|---------|---------|
| `Linear` | Constant speed from start to end. |
| `InSine` | Sine curve: starts slow. |
| `OutSine` | Sine curve: ends slow. |
| `InOutSine` | Sine curve: slow at both ends. |
| `InQuad` | Squared: starts slow. |
| `OutQuad` | Squared: ends slow. |
| `InOutQuad` | Squared: slow at both ends. |
| `InCubic` | Cubed: starts slower than InQuad. |
| `OutCubic` | Cubed: ends slower than OutQuad. |
| `InOutCubic` | Cubed: slow at both ends. |
| `InQuart` | Fourth power: a sharper start. |
| `OutQuart` | Fourth power: a sharper stop. |
| `InOutQuart` | Fourth power: slow at both ends. |
| `InQuint` | Fifth power: the sharpest of the power curves at the start. |
| `OutQuint` | Fifth power: the sharpest of the power curves at the end. |
| `InOutQuint` | Fifth power: slow at both ends. |
| `InExpo` | Exponential: barely moves, then rushes to the end. |
| `OutExpo` | Exponential: rushes off, then creeps to the end. |
| `InOutExpo` | Exponential: slow at both ends, fast in the middle. |
| `InCirc` | Quarter circle: starts slow and speeds up steeply. |
| `OutCirc` | Quarter circle: starts fast and stops steeply. |
| `InOutCirc` | Circular: slow at both ends. |
| `InBack` | Pulls back below the start before moving. |
| `OutBack` | Overshoots the end and comes back. |
| `InOutBack` | Pulls back at the start and overshoots the end. |
| `InElastic` | Wobbles around the start with growing swings, then snaps to the end. |
| `OutElastic` | Snaps past the end and wobbles to rest on it. |
| `InOutElastic` | Elastic wobble at both ends. |
| `InBounce` | Bounces off the start with growing bounces before leaving it. |
| `OutBounce` | Drops onto the end and bounces to rest, like a ball. |
| `InOutBounce` | Bounces at both ends. |
| `Spring` | Overshoots the end and settles on it like a damped spring. |

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

| Variant | Meaning |
|---------|---------|
| `A` | The A key. |
| `B` | The B key. |
| `C` | The C key. |
| `D` | The D key. |
| `E` | The E key. |
| `F` | The F key. |
| `G` | The G key. |
| `H` | The H key. |
| `I` | The I key. |
| `J` | The J key. |
| `K` | The K key. |
| `L` | The L key. |
| `M` | The M key. |
| `N` | The N key. |
| `O` | The O key. |
| `P` | The P key. |
| `Q` | The Q key. |
| `R` | The R key. |
| `S` | The S key. |
| `T` | The T key. |
| `U` | The U key. |
| `V` | The V key. |
| `W` | The W key. |
| `X` | The X key. |
| `Y` | The Y key. |
| `Z` | The Z key. |
| `Num1` | The 1 key in the row above the letters. |
| `Num2` | The 2 key in the row above the letters. |
| `Num3` | The 3 key in the row above the letters. |
| `Num4` | The 4 key in the row above the letters. |
| `Num5` | The 5 key in the row above the letters. |
| `Num6` | The 6 key in the row above the letters. |
| `Num7` | The 7 key in the row above the letters. |
| `Num8` | The 8 key in the row above the letters. |
| `Num9` | The 9 key in the row above the letters. |
| `Num0` | The 0 key in the row above the letters. |
| `Return` | Return, or Enter. |
| `Escape` | Escape. |
| `Backspace` | Backspace. |
| `Tab` | Tab. |
| `Space` | The space bar. |
| `F1` | The F1 function key. |
| `F2` | The F2 function key. |
| `F3` | The F3 function key. |
| `F4` | The F4 function key. |
| `F5` | The F5 function key. |
| `F6` | The F6 function key. |
| `F7` | The F7 function key. |
| `F8` | The F8 function key. |
| `F9` | The F9 function key. |
| `F10` | The F10 function key. |
| `F11` | The F11 function key. |
| `F12` | The F12 function key. |
| `Right` | The right arrow. |
| `Left` | The left arrow. |
| `Down` | The down arrow. |
| `Up` | The up arrow. |
| `LCtrl` | The left Ctrl key. |
| `LShift` | The left Shift key. |
| `LAlt` | The left Alt key, Option on a Mac. |
| `RCtrl` | The right Ctrl key. |
| `RShift` | The right Shift key. |
| `RAlt` | The right Alt key, Option on a Mac. |

## MouseButton

Mouse buttons.

```rust
if input.mouse_pressed(MouseButton::Left) { shoot(); }
```

```rust
enum MouseButton { Left, Middle, Right, X1, X2 }
```

| Variant | Meaning |
|---------|---------|
| `Left` | The left button, or the primary button on a mouse set up for the left hand. |
| `Middle` | The middle button, often the wheel pressed in. |
| `Right` | The right button. |
| `X1` | The first side button, often Back in a browser. |
| `X2` | The second side button, often Forward in a browser. |

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

| Variant | Meaning |
|---------|---------|
| `South` | The bottom face button: A on Xbox, Cross on PlayStation. |
| `East` | The right face button: B on Xbox, Circle on PlayStation. |
| `West` | The left face button: X on Xbox, Square on PlayStation. |
| `North` | The top face button: Y on Xbox, Triangle on PlayStation. |
| `Back` | Back, View or Select. |
| `Guide` | The logo button in the middle. |
| `Start` | Start, Menu or Options. |
| `LeftStick` | Pressing the left stick in. |
| `RightStick` | Pressing the right stick in. |
| `LeftShoulder` | The left bumper. |
| `RightShoulder` | The right bumper. |
| `DpadUp` | Up on the d-pad. |
| `DpadDown` | Down on the d-pad. |
| `DpadLeft` | Left on the d-pad. |
| `DpadRight` | Right on the d-pad. |

## GamepadAxis

Sticks read -1 to 1 with Y down; triggers read 0 to 1.

```rust
input.bind_stick("move", GamepadAxis::LeftX, GamepadAxis::LeftY);
```

```rust
enum GamepadAxis { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger }
```

| Variant | Meaning |
|---------|---------|
| `LeftX` | The left stick across: -1 left, 1 right. |
| `LeftY` | The left stick up and down: -1 up, 1 down. |
| `RightX` | The right stick across: -1 left, 1 right. |
| `RightY` | The right stick up and down: -1 up, 1 down. |
| `LeftTrigger` | The left trigger: 0 released, 1 pulled all the way. |
| `RightTrigger` | The right trigger: 0 released, 1 pulled all the way. |

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

## JointKind

What a `Joint` lets its two bodies do: keep a distance, turn about a point, slide along an
axis, or move as one.

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
