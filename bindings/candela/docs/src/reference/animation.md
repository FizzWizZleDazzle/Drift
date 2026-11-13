<!-- Written by scripts/candela-reference.py from the drift module and
     bindings/candela/docs/api-notes.md. Edit those, not this page. -->

# Procedural Animation

Angles, easing helpers, oscillators, springs, squash and stretch, inverse kinematics, ropes and stepping feet: the pieces for animating parts in code rather than from frames. See [Procedural Animation](../guides/procedural-animation.md) and the [Creature](../examples/creature.md) example.

## wrap_angle

```rust
fn wrap_angle(a: float) -> float
```

An angle brought into -PI to PI.

## angle_diff

```rust
fn angle_diff(from: float, to: float) -> float
```

The shortest turn from `from` to `to`, -PI to PI: negative turns counter-clockwise on screen.

## lerp_angle

```rust
fn lerp_angle(a: float, b: float, t: float) -> float
```

From `a` toward `b` along the shorter arc; t = 0 gives a, 1 gives b.

## rotate_towards

```rust
fn rotate_towards(current: float, target: float, max_step: float) -> float
```

`current` turned toward `target` by at most `max_step` radians, the short way round. Use `speed * dt` as the step for a turn rate.

## look_at

```rust
fn look_at(from: Vec2, to: Vec2) -> float
```

The rotation that points +x from `from` toward `to`.

## lerp

```rust
fn lerp(a: float, b: float, t: float) -> float
```

## move_towards

```rust
fn move_towards(current: float, target: float, max_step: float) -> float
```

`current` moved toward `target` by at most `max_step`.

## damp

```rust
fn damp(current: float, target: float, rate: float, dt: float) -> float
```

From `current` toward `target`, covering the same share of the distance each second whatever the frame rate: higher `rate` is snappier.

## damp2

```rust
fn damp2(current: Vec2, target: Vec2, rate: float, dt: float) -> Vec2
```

## smoothstep

```rust
fn smoothstep(lo: float, hi: float, x: float) -> float
```

0 below `lo`, 1 above `hi`, an S-curve between.

## sine_wave

```rust
fn sine_wave(t: float, frequency: float, amplitude: float, phase: float) -> float
```

A sine wave over time `t` (seconds): `frequency` cycles a second between -amplitude and amplitude, shifted by `phase` radians. Give each leg or tentacle its own phase to keep them out of step.

## cosine_wave

```rust
fn cosine_wave(t: float, frequency: float, amplitude: float, phase: float) -> float
```

## wobble_hash

```rust
fn wobble_hash(i: int, seed: int) -> float
```

## wobble

```rust
fn wobble(t: float, frequency: float, amplitude: float, seed: int) -> float
```

Smooth random motion between -amplitude and amplitude, about `frequency` changes a second: idle sway, hover, trembling. Different seeds give different wobbles.

## wobble2

```rust
fn wobble2(t: float, frequency: float, amplitude: float, seed: int) -> Vec2
```

## squash

```rust
fn squash(amount: float) -> Vec2
```

A scale that keeps the area: `amount` 0.2 is 20 percent taller and thinner, -0.2 shorter and wider. Multiply it into Transform.scale.

## stretch

```rust
fn stretch(velocity: Vec2, per_speed: float, max_stretch: float) -> Vec2
```

Stretched along a velocity: rotate the part to `velocity.angle()` and use this scale, longer along x the faster it moves, up to `max_stretch`.

## Spring

A value that chases a target like a weight on a spring. `stiffness` pulls (try 100 to 400), `damping` slows (2 * sqrt(stiffness) settles fastest without overshooting; less wobbles and overshoots). `smooth` instead eases in about `seconds` with no overshoot. Keep the spring in a component so it carries its velocity from frame to frame.

```rust
struct Spring {
    value: float = 0.0,
    velocity: float = 0.0,
}
```

### Spring.update

```rust
fn update(self, target: float, stiffness: float, damping: float, dt: float) -> float
```

### Spring.smooth

```rust
fn smooth(self, target: float, seconds: float, dt: float) -> float
```

## spring

```rust
fn spring(value: float) -> Spring
```

## Spring2

The same for a position.

```rust
struct Spring2 {
    value: Vec2 = Vec2 { .. },
    velocity: Vec2 = Vec2 { .. },
}
```

### Spring2.run

```rust
fn run(self, kind: int, target: Vec2, a: float, b: float, dt: float) -> Vec2
```

### Spring2.update

```rust
fn update(self, target: Vec2, stiffness: float, damping: float, dt: float) -> Vec2
```

### Spring2.smooth

```rust
fn smooth(self, target: Vec2, seconds: float, dt: float) -> Vec2
```

## spring2

```rust
fn spring2(value: Vec2) -> Spring2
```

## AngleSpring

The same for a rotation, turning the short way round.

```rust
struct AngleSpring {
    value: float = 0.0,
    velocity: float = 0.0,
}
```

### AngleSpring.update

```rust
fn update(self, target: float, stiffness: float, damping: float, dt: float) -> float
```

## angle_spring

```rust
fn angle_spring(value: float) -> AngleSpring
```

## TwoBone

The pose of a two-bone limb: where the elbow or knee goes, where the hand or foot ends up, and the world rotation of each bone.

```rust
struct TwoBone {
    joint: Vec2 = Vec2 { .. },
    end: Vec2 = Vec2 { .. },
    upper: float = 0.0,
    lower: float = 0.0,
    reached: bool = false,
}
```

## two_bone_ik

```rust
fn two_bone_ik(root: Vec2, target: Vec2, upper_len: float, lower_len: float, bend: float) -> TwoBone
```

Bones of `upper_len` and `lower_len` from `root` reaching for `target`. `bend` 1.0 puts the joint clockwise of the root-to-target line on screen, -1.0 counter-clockwise: pick the side a knee or elbow points. Out of reach, the limb points straight at the target and `reached` is false.

## IkChain

A chain of segments whose end reaches for a target while its first joint stays put: tentacles, necks, spines, long arms. `joints` has one more entry than `lengths`. Limits, when set, keep each segment within `min_angles[i]`..`max_angles[i]` of the one before it (the first one of `base_angle`).

```rust
struct IkChain {
    joints: Vec2[] = [],
    lengths: float[] = [],
    min_angles: float[] = [],
    max_angles: float[] = [],
    base_angle: float = 0.0,
    iterations: int = 10,
    tolerance: float = 0.5,
}
```

### IkChain.limit_all

```rust
fn limit_all(self, max_bend: float)
```

Limits every joint to `max_bend` radians either way.

### IkChain.solve

```rust
fn solve(self, root: Vec2, target: Vec2) -> bool
```

Moves the first joint to `root`, then reaches for `target`. Returns true when the end got within `tolerance`.

### IkChain.end

```rust
fn end(self) -> Vec2
```

### IkChain.angle

```rust
fn angle(self, i: int) -> float
```

The world rotation of segment i, for the sprite drawn along it.

### IkChain.len

```rust
fn len(self) -> int
```

## ik_chain

```rust
fn ik_chain(root: Vec2, count: int, length: float, direction: float) -> IkChain
```

A straight chain of `count` segments of `length` from `root`, pointing along `direction` radians.

## Rope

Points joined by fixed-length links that swing under gravity, the first one held at an anchor: tails, tentacles, hair, scarves, ropes, cloth strips. `stiffness` 0 hangs like a rope; toward 1 it resists bending, like a tail. Points stay above `ground`.

```rust
struct Rope {
    points: Vec2[] = [],
    previous: Vec2[] = [],
    segment: float = 10.0,
    gravity: Vec2 = Vec2 { x: 0.0, y: 400.0 },
    damping: float = 0.98,
    iterations: int = 8,
    stiffness: float = 0.0,
    ground: float = 1000000.0,
    pin_end: bool = false,
    end_target: Vec2 = Vec2 { .. },
}
```

### Rope.update

```rust
fn update(self, anchor: Vec2, dt: float)
```

One step with the first point at `anchor`.

### Rope.pin

```rust
fn pin(self, target: Vec2)
```

Holds the last point at `target` as well, as for a rope tied at both ends; `release()` lets it go.

### Rope.release

```rust
fn release(self)
```

### Rope.push

```rust
fn push(self, v: Vec2)
```

Pushes every point by `v` this step, as a gust of wind or a hit.

### Rope.len

```rust
fn len(self) -> int
```

### Rope.angle

```rust
fn angle(self, i: int) -> float
```

The rotation of link i, from point i to point i + 1.

## rope

```rust
fn rope(start: Vec2, count: int, segment: float, direction: float) -> Rope
```

`count` points `segment` apart from `start` along `direction` radians.

## Gait

How a foot steps: once it is more than `distance` from where it should stand, it lifts `height` along `up` and lands there over `duration` seconds, `lead` times `distance` further on in the direction it moved.

```rust
struct Gait {
    distance: float = 30.0,
    duration: float = 0.15,
    height: float = 12.0,
    lead: float = 0.3,
    up: Vec2 = Vec2 { x: 0.0, y: -1.0 },
}
```

## Foot

A foot that stays planted where it landed while the body moves on, and steps when it strays. Draw it at `position`.

```rust
struct Foot {
    position: Vec2 = Vec2 { .. },
    from: Vec2 = Vec2 { .. },
    to: Vec2 = Vec2 { .. },
    progress: float = 1.0,
}
```

### Foot.planted

```rust
fn planted(self) -> bool
```

### Foot.update

```rust
fn update(self, rest: Vec2, g: Gait, can_step: bool, dt: float) -> bool
```

Advances the foot toward `rest`, where it would stand under the body now. A planted foot only starts a step when `can_step`: pass whether the opposite foot is planted to make legs take turns. Returns true on the frame it lands.

## foot

```rust
fn foot(at: Vec2) -> Foot
```
