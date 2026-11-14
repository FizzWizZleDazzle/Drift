<!-- Written by scripts/candela-reference.py from the doc comments in
     bindings/candela/drift/src. Edit those, not this page. -->

# Procedural Animation

Angles, easing helpers, oscillators, springs, squash and stretch, inverse kinematics, ropes
and stepping feet: the pieces for animating parts in code rather than from frames. See
[Procedural Animation](../guides/procedural-animation.md) and the
[Creature](../examples/creature.md) example.

## wrap_angle

```rust
fn wrap_angle(a: float) -> float
```

An angle brought into -PI to PI.

## angle_diff

```rust
fn angle_diff(from: float, to: float) -> float
```

The shortest turn from `from` to `to`, -PI to PI: negative turns counter-clockwise on
screen.

## lerp_angle

```rust
fn lerp_angle(a: float, b: float, t: float) -> float
```

From `a` toward `b` along the shorter arc; t = 0 gives a, 1 gives b.

## rotate_towards

```rust
fn rotate_towards(current: float, target: float, max_step: float) -> float
```

`current` turned toward `target` by at most `max_step` radians, the short way round. Use
`speed * dt` as the step for a turn rate.

## look_at

```rust
fn look_at(from: Vec2, to: Vec2) -> float
```

The rotation that points +x from `from` toward `to`.

## lerp

```rust
fn lerp(a: float, b: float, t: float) -> float
```

The number `t` of the way from `a` to `b`: 0 gives a, 1 gives b. `t` outside 0 to 1 goes
past either end.

## move_towards

```rust
fn move_towards(current: float, target: float, max_step: float) -> float
```

`current` moved toward `target` by at most `max_step`.

## damp

```rust
fn damp(current: float, target: float, rate: float, dt: float) -> float
```

From `current` toward `target`, covering the same share of the distance each second whatever
the frame rate: higher `rate` is snappier.

## damp2

```rust
fn damp2(current: Vec2, target: Vec2, rate: float, dt: float) -> Vec2
```

`damp` for a position: from `current` toward `target`, the same share of the distance each
second whatever the frame rate.

```rust
cam.position = damp2(cam.position, player_pos, 6.0, time.dt);
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

A sine wave over time `t` (seconds): `frequency` cycles a second between -amplitude and
amplitude, shifted by `phase` radians. Give each leg or tentacle its own phase to keep them
out of step.

## cosine_wave

```rust
fn cosine_wave(t: float, frequency: float, amplitude: float, phase: float) -> float
```

`sine_wave` a quarter cycle ahead: at `t` 0 with no phase it starts at `amplitude`. Pair it
with `sine_wave` for motion around a circle or an ellipse.

## wobble_hash

```rust
fn wobble_hash(i: int, seed: int) -> float
```

A repeatable random number from -1 to 1 for whole step `i` of a wobble with `seed`;
`wobble` blends between neighbouring steps.

## wobble

```rust
fn wobble(t: float, frequency: float, amplitude: float, seed: int) -> float
```

Smooth random motion between -amplitude and amplitude, about `frequency` changes a second:
idle sway, hover, trembling. Different seeds give different wobbles.

## wobble2

```rust
fn wobble2(t: float, frequency: float, amplitude: float, seed: int) -> Vec2
```

`wobble` in two directions: x and y wobble independently with the same frequency and
amplitude, for drifting hovers and shakes.

## squash

```rust
fn squash(amount: float) -> Vec2
```

A scale that keeps the area: `amount` 0.2 is 20 percent taller and thinner, -0.2 shorter and
wider. Multiply it into Transform.scale.

## stretch

```rust
fn stretch(velocity: Vec2, per_speed: float, max_stretch: float) -> Vec2
```

Stretched along a velocity: rotate the part to `velocity.angle()` and use this scale, longer
along x the faster it moves, up to `max_stretch`.

## Spring

A value that chases a target like a weight on a spring. `stiffness` pulls (try 100 to 400),
`damping` slows (2 * sqrt(stiffness) settles fastest without overshooting; less wobbles and
overshoots). `smooth` instead eases in about `seconds` with no overshoot. Keep the spring in
a component so it carries its velocity from frame to frame.

```rust
struct Spring {
    value: float = 0.0,
    velocity: float = 0.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `value` | `float` | `0.0` | Where it is now. |
| `velocity` | `float` | `0.0` | How fast it moves, in units per second. |

```rust
@component
struct Bob { y: Spring = spring(0.0) }

let y = bob.y.update(target_y, 200.0, 20.0, time.dt);
```

### Spring.update

```rust
fn update(self, target: float, stiffness: float, damping: float, dt: float) -> float
```

One step of `dt` seconds toward `target` with `stiffness` and `damping`; returns the new
value.

### Spring.smooth

```rust
fn smooth(self, target: float, seconds: float, dt: float) -> float
```

One step of `dt` seconds toward `target`, easing in over about `seconds` without
overshooting; returns the new value.

## spring

```rust
fn spring(value: float) -> Spring
```

A spring at rest at `value`.

## Spring2

A `Spring` for a position: the value chases a target point, x and y each on their own spring.

```rust
struct Spring2 {
    value: Vec2 = Vec2 { .. },
    velocity: Vec2 = Vec2 { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `value` | `Vec2` | `Vec2 { .. }` | Where it is now. |
| `velocity` | `Vec2` | `Vec2 { .. }` | How fast it moves, in units per second. |

```rust
t.position = follower.at.update(leader_pos, 150.0, 18.0, time.dt);
```

### Spring2.run

```rust
fn run(self, kind: int, target: Vec2, a: float, b: float, dt: float) -> Vec2
```

One step of kind 0, a spring with stiffness `a` and damping `b`, or kind 2, a smooth ease
over `a` seconds; returns the new value. `update` and `smooth` call it.

### Spring2.update

```rust
fn update(self, target: Vec2, stiffness: float, damping: float, dt: float) -> Vec2
```

One step of `dt` seconds toward `target` with `stiffness` and `damping`; returns the new
value.

### Spring2.smooth

```rust
fn smooth(self, target: Vec2, seconds: float, dt: float) -> Vec2
```

One step of `dt` seconds toward `target`, easing in over about `seconds` without
overshooting; returns the new value.

## spring2

```rust
fn spring2(value: Vec2) -> Spring2
```

A position spring at rest at `value`.

## AngleSpring

A `Spring` for a rotation in radians, turning the short way round.

```rust
struct AngleSpring {
    value: float = 0.0,
    velocity: float = 0.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `value` | `float` | `0.0` | The angle now, in radians. It is not wrapped, so it can pass PI. |
| `velocity` | `float` | `0.0` | How fast it turns, in radians per second. |

```rust
t.rotation = head.turn.update(look_at(t.position, target), 120.0, 15.0, time.dt);
```

### AngleSpring.update

```rust
fn update(self, target: float, stiffness: float, damping: float, dt: float) -> float
```

One step of `dt` seconds toward `target` radians the short way round, with `stiffness`
and `damping`; returns the new angle.

## angle_spring

```rust
fn angle_spring(value: float) -> AngleSpring
```

An angle spring at rest at `value` radians.

## TwoBone

The pose of a two-bone limb: where the elbow or knee goes, where the hand or foot ends up,
and the world rotation of each bone.

```rust
struct TwoBone {
    joint: Vec2 = Vec2 { .. },
    end: Vec2 = Vec2 { .. },
    upper: float = 0.0,
    lower: float = 0.0,
    reached: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `joint` | `Vec2` | `Vec2 { .. }` | Where the middle joint (elbow or knee) goes. |
| `end` | `Vec2` | `Vec2 { .. }` | Where the end (hand or foot) ends up: the target when reached. |
| `upper` | `float` | `0.0` | The world rotation of the upper bone, from the root to the joint, in radians. |
| `lower` | `float` | `0.0` | The world rotation of the lower bone, from the joint to the end, in radians. |
| `reached` | `bool` | `false` | Whether the end got to the target. |

## two_bone_ik

```rust
fn two_bone_ik(root: Vec2, target: Vec2, upper_len: float, lower_len: float, bend: float) -> TwoBone
```

Bones of `upper_len` and `lower_len` from `root` reaching for `target`. `bend` 1.0 puts the
joint clockwise of the root-to-target line on screen, -1.0 counter-clockwise: pick the side
a knee or elbow points. Out of reach, the limb points straight at the target and `reached`
is false.

## IkChain

A chain of segments whose end reaches for a target while its first joint stays put:
tentacles, necks, spines, long arms. `joints` has one more entry than `lengths`. Limits,
when set, keep each segment within `min_angles[i]`..`max_angles[i]` of the one before it
(the first one of `base_angle`).

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

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `joints` | `Vec2[]` | `[]` | The joint positions; segment i runs from joint i to joint i + 1. |
| `lengths` | `float[]` | `[]` | The length of each segment, in pixels. |
| `min_angles` | `float[]` | `[]` | The least each segment may turn from the one before it, in radians; empty for no limit. |
| `max_angles` | `float[]` | `[]` | The most each segment may turn from the one before it, in radians; empty for no limit. |
| `base_angle` | `float` | `0.0` | The rotation the first segment's limits are measured from, in radians. |
| `iterations` | `int` | `10` | The most solver passes per `solve`. |
| `tolerance` | `float` | `0.5` | How close the end must get to the target, in pixels, to stop early. |

```rust
let arm = ik_chain(shoulder, 6, 12.0, 0.0);
arm.limit_all(0.6);
arm.solve(shoulder, input.mouse_world());
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

Moves the first joint to `root`, then reaches for `target`. Returns true when the end
got within `tolerance`.

### IkChain.end

```rust
fn end(self) -> Vec2
```

Where the last joint is: the tip of the chain.

### IkChain.angle

```rust
fn angle(self, i: int) -> float
```

The world rotation of segment i, for the sprite drawn along it.

### IkChain.len

```rust
fn len(self) -> int
```

How many segments it has: one less than its joints.

## ik_chain

```rust
fn ik_chain(root: Vec2, count: int, length: float, direction: float) -> IkChain
```

A straight chain of `count` segments of `length` from `root`, pointing along `direction`
radians.

## Rope

Points joined by fixed-length links that swing under gravity, the first one held at an
anchor: tails, tentacles, hair, scarves, ropes, cloth strips. `stiffness` 0 hangs like a
rope; toward 1 it resists bending, like a tail. Points stay above `ground`.

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

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `points` | `Vec2[]` | `[]` | The point positions in the world; the first follows the anchor. |
| `previous` | `Vec2[]` | `[]` | Where each point was the step before; the difference is its velocity. |
| `segment` | `float` | `10.0` | The length of each link, in pixels. |
| `gravity` | `Vec2` | `Vec2 { x: 0.0, y: 400.0 }` | Pulls every point, in pixels per second squared; +y is down. |
| `damping` | `float` | `0.98` | The share of velocity kept per 1/60 second, 0 to 1: lower settles sooner. |
| `iterations` | `int` | `8` | Link passes per step; more holds the lengths tighter. |
| `stiffness` | `float` | `0.0` | Resistance to bending, 0 to 1: 0 hangs like a rope, toward 1 holds a tail's shape. |
| `ground` | `float` | `1000000.0` | The largest y a point may have: points stay above this line. |
| `pin_end` | `bool` | `false` | Whether the last point is held at `end_target`; `pin` and `release` set it. |
| `end_target` | `Vec2` | `Vec2 { .. }` | Where the last point is held while `pin_end` is on. |

```rust
tail.rope.update(t.position, time.dt);
for i in range(tail.rope.len() - 1) {
    draw.line(tail.rope.points[i], tail.rope.points[i + 1], 3.0, rgb(200, 120, 60));
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

Holds the last point at `target` as well, as for a rope tied at both ends; `release()`
lets it go.

### Rope.release

```rust
fn release(self)
```

Lets the last point go after `pin`.

### Rope.push

```rust
fn push(self, v: Vec2)
```

Pushes every point by `v` this step, as a gust of wind or a hit.

### Rope.len

```rust
fn len(self) -> int
```

How many points it has: one more than its links.

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

How a foot steps: once it is more than `distance` from where it should stand, it lifts
`height` along `up` and lands there over `duration` seconds, `lead` times `distance` further
on in the direction it moved.

```rust
struct Gait {
    distance: float = 30.0,
    duration: float = 0.15,
    height: float = 12.0,
    lead: float = 0.3,
    up: Vec2 = Vec2 { x: 0.0, y: -1.0 },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `distance` | `float` | `30.0` | How far, in pixels, the foot may stray from where it should stand before it steps. |
| `duration` | `float` | `0.15` | Seconds a step takes. |
| `height` | `float` | `12.0` | How high the foot lifts mid-step, in pixels. |
| `lead` | `float` | `0.3` | How far past the rest point the foot lands, as a share of `distance`. |
| `up` | `Vec2` | `Vec2 { x: 0.0, y: -1.0 }` | The direction the foot lifts; up the screen by default. |

## Foot

A foot that stays planted where it landed while the body moves on, and steps when it strays.
Draw it at `position`.

```rust
struct Foot {
    position: Vec2 = Vec2 { .. },
    from: Vec2 = Vec2 { .. },
    to: Vec2 = Vec2 { .. },
    progress: float = 1.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `position` | `Vec2` | `Vec2 { .. }` | Where the foot is now; draw it here. |
| `from` | `Vec2` | `Vec2 { .. }` | Where the current step started. |
| `to` | `Vec2` | `Vec2 { .. }` | Where the current step lands. |
| `progress` | `float` | `1.0` | How far through the step it is, 0 to 1; 1 is planted. |

```rust
let left_lands = legs.left.update(t.position + vec2(-10.0, 20.0), Gait { .. }, legs.right.planted(), time.dt);
```

### Foot.planted

```rust
fn planted(self) -> bool
```

Whether the foot is on the ground and not mid-step.

### Foot.update

```rust
fn update(self, rest: Vec2, g: Gait, can_step: bool, dt: float) -> bool
```

Advances the foot toward `rest`, where it would stand under the body now. A planted foot
only starts a step when `can_step`: pass whether the opposite foot is planted to make
legs take turns. Returns true on the frame it lands.

## foot

```rust
fn foot(at: Vec2) -> Foot
```

A foot planted at `at`.
