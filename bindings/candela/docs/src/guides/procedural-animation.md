# Procedural Animation

Procedural animation poses a character from code every frame instead of playing frames someone drew: legs that reach for the ground, a head that turns toward what it watches, a tail that swings behind, a body that breathes. You build the character from parts, link the parts with `Parent`, and move them with the helpers on this page.

Everything here comes with `import "drift" { * };`. Angles are radians from the +x axis, and grow clockwise on screen, because y points down.

## Parts and pivots

Each part is an entity with a `Sprite` (or a `Rectangle` or `Circle`) whose `anchor` sits on its joint, so it turns around the joint. Parts that ride on another part get it as their `Parent`, and their `Transform` counts from it. See [Parts that move together](sprites.md) for building a rig; this page is about moving one.

Two ways to drive a part:

- **Local**: set the child's `Transform.rotation` relative to its parent. Good for joints that swing on their own: a wagging ear, a breathing chest.
- **World**: work out where the part belongs in world space (an IK solver, a rope, a foot) and assign its `GlobalTransform`, or keep the part as a root and set its `Transform`. The engine works out the local `Transform` for you.

## Angles

| Function | Returns |
|----------|---------|
| `wrap_angle(a)` | `a` brought into -PI to PI |
| `angle_diff(from, to)` | The shortest turn from `from` to `to`, -PI to PI |
| `lerp_angle(a, b, t)` | From `a` toward `b` along the shorter arc |
| `rotate_towards(current, target, max_step)` | `current` turned toward `target` by at most `max_step`, the short way round |
| `look_at(from, to)` | The rotation that points +x from `from` toward `to` |
| `lerp(a, b, t)`, `move_towards(current, target, max_step)`, `smoothstep(lo, hi, x)` | Plain number helpers |
| `damp(current, target, rate, dt)`, `damp2(...)` for `Vec2` | Eases toward the target by the same share each second at any frame rate |
| `sin`, `cos`, `atan2(y, x)`, `from_angle(a)`, `Vec2.angle()`, `Vec2.rotated(a)` | Trigonometry; see [Values](../reference/values.md) |

A head that turns toward the cursor at most 4 radians a second, so it never snaps:

```rust
@component struct Head {}

@system
fn watch(_: Head, t: Transform, input: Input, time: Time) {
    let want = look_at(t.position, input.mouse_world());
    t.rotation = rotate_towards(t.rotation, want, 4.0 * time.dt);
}
```

For a part with a parent, compare against its world rotation: `world_transform(e).rotation`, or turn the target into the parent's space first with `to_local`.

## Oscillators and wobble

| Function | Returns |
|----------|---------|
| `sine_wave(t, frequency, amplitude, phase)`, `cosine_wave(...)` | A wave between -amplitude and amplitude, `frequency` cycles a second, shifted by `phase` radians |
| `wobble(t, frequency, amplitude, seed)`, `wobble2(...)` | Smooth random motion between -amplitude and amplitude; different seeds wobble differently |

```rust
@component struct Fin { phase: float = 0.0 }

@system
fn flap(f: Fin, t: Transform, time: Time) {
    t.rotation = sine_wave(time.elapsed, 2.0, 0.4, f.phase) + wobble(time.elapsed, 3.0, 0.05, 1);
}
```

Give each repeated part its own `phase` (for example `float(i) * 0.6`) and a row of fins or legs moves as a wave instead of in unison.

## Squash and stretch

| Function | Returns |
|----------|---------|
| `squash(amount)` | A scale that keeps the area: 0.2 is taller and thinner, -0.2 shorter and wider |
| `stretch(velocity, per_speed, max_stretch)` | Longer along x the faster `velocity` is, up to `1 + max_stretch`; rotate the part to `velocity.angle()` with it |

Breathing is a slow squash:

```rust
@system
fn breathe(_: Body, t: Transform, time: Time) {
    t.scale = squash(sine_wave(time.elapsed, 0.5, 0.06, 0.0));
}
```

Scale a body's sprite, not the body entity, when its children should not breathe along: put the sprite on a child of its own.

## Springs

A spring chases a target with weight: it lags, overshoots and settles. Keep one in a component or resource so it carries its velocity between frames.

| Type | Methods |
|------|---------|
| `Spring` (`value`, `velocity`), made with `spring(value)` | `update(target, stiffness, damping, dt) -> float`, `smooth(target, seconds, dt) -> float` |
| `Spring2` for a `Vec2`, made with `spring2(value)` | the same, with `Vec2` targets |
| `AngleSpring` for a rotation, made with `angle_spring(value)` | `update(target, stiffness, damping, dt)`, turning the short way round |

`stiffness` pulls toward the target (try 100 to 400); `damping` slows it. Damping of `2 * sqrt(stiffness)` is critical: the quickest settle with no overshoot. Less wobbles, which is the point for antennae, ears and jiggle. `smooth` is critically damped in about `seconds`, for cameras and eyes. Springs stay stable at any frame time.

Secondary motion, a part that lags behind its parent, is a spring on the part's world position or rotation:

```rust
@component struct Antenna { tip: Spring2 = Spring2 { .. }, offset: Vec2 = Vec2 { .. } }

@system
fn jiggle(a: Antenna, g: GlobalTransform, p: Parent, time: Time) {
    let rest = to_world(p.entity, a.offset);         // where it would be if it were rigid
    g.position = a.tip.update(rest, 180.0, 8.0, time.dt);
}
```

## Two-bone IK

`two_bone_ik(root, target, upper_len, lower_len, bend)` poses an arm or leg of two bones from `root` toward `target` and returns a `TwoBone`: `joint` (the elbow or knee), `end` (the hand or foot), `upper` and `lower` (the world rotation of each bone), and `reached` (false when the target is out of reach; the limb then points straight at it). `bend` 1.0 puts the joint clockwise of the root-to-target line on screen, -1.0 counter-clockwise: pick the side the knee points to.

```rust
@component struct Leg { thigh: Entity = Entity { .. }, shin: Entity = Entity { .. }, foot: Foot = Foot { .. } }

@system
fn pose_leg(leg: Leg, g: GlobalTransform, cmd: Commands) {
    let pose = two_bone_ik(g.position, leg.foot.position, 40.0, 36.0, -1.0);
    cmd.insert(leg.thigh, GlobalTransform { position: g.position, rotation: pose.upper, .. });
    cmd.insert(leg.shin, GlobalTransform { position: pose.joint, rotation: pose.lower, .. });
}
```

Draw each bone with a sprite anchored at its start, `anchor: vec2(0.0, 0.5)`, pointing along +x. When the bones are children of each other, as in the creature below, set local rotations instead: the thigh's is `angle_diff(body_rotation, pose.upper)` and the shin's `angle_diff(pose.upper, pose.lower)`.

## IK chains

`IkChain` is FABRIK: a chain of any number of segments whose end reaches for a target while its first joint stays where you put it. Necks, tails that reach, tentacles, spines.

```rust
@resource
struct Tentacle { chain: IkChain = ik_chain(vec2(320.0, 300.0), 8, 18.0, -1.5707) }

@system
fn reach(t: Tentacle, input: Input) {
    t.chain.solve(vec2(320.0, 300.0), input.mouse_world());
}
```

| Member | Does |
|--------|------|
| `ik_chain(root, count, length, direction)` | A straight chain of `count` segments of `length` from `root`, pointing along `direction` |
| `joints`, `lengths` | The joint positions (one more than segments) and segment lengths; build your own for uneven segments |
| `solve(root, target) -> bool` | Moves the first joint to `root` and reaches; true when the end got within `tolerance` |
| `limit_all(max_bend)` | Keeps every joint within `max_bend` radians either way of the segment before it |
| `min_angles`, `max_angles`, `base_angle` | Per-joint limits; segment 0 is limited against `base_angle` |
| `iterations` (10), `tolerance` (0.5) | Solver effort and how close counts as reached |
| `end()`, `angle(i)`, `len()` | The tip, the world rotation of segment i, the segment count |

Joint limits make chains look like they have bones: a neck with `limit_all(0.4)` curls instead of folding back on itself.

## Ropes, tails and hair

`Rope` is a verlet chain: points joined by fixed-length links, the first held at an anchor, the rest swinging under gravity. Tails, tentacles that trail, hair, scarves, cloth strips, ropes.

```rust
@component struct Tail { rope: Rope = rope(vec2(0.0, 0.0), 10, 8.0, 3.14159) }

@system
fn swing_tail(e: Entity, tail: Tail, time: Time, draw: Draw) {
    tail.rope.update(to_world(e, vec2(-20.0, 0.0)), time.dt);   // hangs from 20 px behind the body
    draw.polyline(tail.rope.points, rgb(200, 120, 60), 6.0, false);
}
```

| Member | Does |
|--------|------|
| `rope(start, count, segment, direction)` | `count` points `segment` apart from `start` along `direction` |
| `update(anchor, dt)` | One step with the first point at `anchor` |
| `points`, `len()`, `angle(i)` | Where to draw; the rotation of link i |
| `gravity` (0, 400), `damping` (0.98), `iterations` (8) | Weight, how quickly swinging dies down, how stiff the links are |
| `stiffness` (0) | 0 hangs like a rope; toward 1 the chain resists bending, like a tail |
| `ground` | Points stay above this y |
| `pin(target)`, `release()` | Holds the last point too, as for a rope tied at both ends |
| `push(v)` | Moves every point by `v` this step: wind, a hit |

## Stepping feet

A `Foot` stays planted where it landed while the body moves on, and steps when it strays too far from where it should stand. That is the whole trick of procedural walking: the body moves smoothly, and the feet catch up in quick arcs.

```rust
@component struct Walker { left: Foot = foot(vec2(0.0, 0.0)), right: Foot = foot(vec2(0.0, 0.0)) }

@system
fn walk(w: Walker, t: Transform, time: Time) {
    let gait = Gait { distance: 28.0, duration: 0.15, height: 10.0, .. };
    let ground = t.position.y + 40.0;
    // Each foot steps only while the other is planted, so they take turns.
    w.left.update(vec2(t.position.x - 10.0, ground), gait, w.right.planted(), time.dt);
    w.right.update(vec2(t.position.x + 10.0, ground), gait, w.left.planted(), time.dt);
}
```

| Member | Does |
|--------|------|
| `foot(at)` | A foot planted at `at` |
| `update(rest, gait, can_step, dt) -> bool` | Advances toward `rest`, where the foot would stand now. A planted foot steps only when `can_step`. True on the frame it lands |
| `position`, `planted()` | Where to draw it; whether it is on the ground |
| `Gait` | `distance` (step when this far from rest), `duration` (seconds a step takes), `height` (lift mid-step), `lead` (land this share of `distance` past the rest point, toward where it moved, so a moving body does not leave it behind), `up` (the lift direction, (0, -1)) |

Feed the foot positions to `two_bone_ik` as targets and the legs bend to reach them.

## Cost

The springs, solvers, ropes and feet run in the engine, one call per spring, chain, rope or foot per frame, with the state crossing as a list of numbers. Fewer points and `iterations` make a rope cheaper.

## Walkthrough: the creature

The [Creature](../examples/creature.md) example puts all of this together: a lizard that walks after the cursor. Here is how it is built, step by step.

### 1. A rig of parts

The body is an entity with a `Transform` and a `Creature` component and nothing drawn. Everything else hangs from it with `spawn_child`, so it moves and turns with the body:

```rust
let body = cmd.spawn([Transform { position: start, .. }, Creature { .. }]);
cmd.spawn_child(body, [at(0.0, 0.0), Rectangle { size: vec2(58.0, 38.0), corner_radius: 18.0, color: SKIN, z: 2.0, .. }, Breath {}]);
let head = cmd.spawn_child(body, [at(30.0, 0.0), Circle { radius: 15.0, color: SKIN, z: 3.0, .. }, Head { .. }]);
let eye = cmd.spawn_child(head, [at(6.0, 7.0 * side), Circle { radius: 5.0, .. }]);
cmd.spawn_child(eye, [at(0.0, 0.0), Circle { radius: 2.5, color: rgb(20, 20, 30), .. }, Pupil {}]);
```

The creature faces +x in its own space: the head is 30 pixels in front, and +y is its right side. Each leg is a chain of three children, thigh, shin and paw, each placed at the end of the one before; the bones are rectangles anchored at their left middle, `anchor: vec2(0.0, 0.5)`, so they turn around the joint:

```rust
let thigh = cmd.spawn_child(body, [Transform { position: hip, .. },
    Rectangle { size: vec2(leg.upper, 8.0), anchor: vec2(0.0, 0.5), corner_radius: 4.0, .. }, leg]);
let shin = cmd.spawn_child(thigh, [Transform { position: vec2(leg.upper, 0.0), .. },
    Rectangle { size: vec2(leg.lower, 6.0), anchor: vec2(0.0, 0.5), corner_radius: 3.0, .. }, Shin { leg: thigh }]);
cmd.spawn_child(shin, [at(leg.lower, 0.0), Circle { radius: 5.0, .. }]);
```

The body's drawing is a child of its own (`Breath`) so that breathing scales only the drawing, not the legs and head hanging from the body.

### 2. Moving the body

`move_body` moves the root toward the cursor, or the way the keys point, easing its velocity with `damp2` and turning to face where it goes with `rotate_towards`, so it never snaps around:

```rust
c.velocity = damp2(c.velocity, dir * tuning.speed, 6.0, time.dt);
t.position = t.position + c.velocity * time.dt;
if c.velocity.length() > 10.0 {
    t.rotation = rotate_towards(t.rotation, c.velocity.angle(), tuning.turn_speed * time.dt);
}
```

Every other system runs `after: [move_body]`, so they see where the body is this frame: its changes land when it returns, and `to_world` and `world_transform` read them from then on.

### 3. Breathing

```rust
let breath = squash(sine_wave(time.elapsed, 0.6, 0.06, 0.0));
let s = 1.0 + min(speed * 0.0008, 0.12);
t.scale = vec2(breath.x * s, breath.y / s);
```

A slow sine through `squash` swells and narrows the body without changing its area; the second factor stretches it along its heading the faster it runs.

### 4. A head that watches

The head turns toward the cursor relative to the body, clamped to a radian either way, on an `AngleSpring` so it overshoots a little and settles:

```rust
let want = angle_diff(body.rotation, look_at(head.position, input.mouse_world()));
t.rotation = h.turn.update(clamp(want, -1.0, 1.0), 120.0, 12.0, time.dt);
```

Each pupil is a child of its eye; `to_local` turns the cursor into the eye's space, and `clamp_length` keeps the pupil inside:

```rust
let eye = parent_of(e);
t.position = to_local(eye, input.mouse_world()).clamp_length(2.2);
```

### 5. Legs that step

Each `Leg` keeps a `Foot` in world space. Its rest point, where the foot would stand now, is a fixed point in the body's space carried to the world with `to_world`; the foot stays put until the body has moved far enough from it, then steps there in an arc. Legs come in two diagonal groups, and a group may only step while the other one is standing, which gives the trot:

```rust
let standing = [true, true];
for (leg, _) in legs {
    if !leg.foot.planted() {
        standing[leg.group] = false;
    }
}
for (leg, t) in legs {
    let hip = to_world(rig.body, leg.hip);
    leg.foot.update(to_world(rig.body, leg.rest), gait, standing[1 - leg.group], time.dt);
    let pose = two_bone_ik(hip, leg.foot.position, leg.upper, leg.lower, leg.bend);
    t.rotation = angle_diff(body.rotation, pose.upper);
}
```

`two_bone_ik` bends each leg to reach its foot; `bend` points the front knees back and the back knees forward. The solver answers in world rotations, and the thigh is a child of the body, so its local rotation is the difference from the body's. The shin, a child of the thigh, takes the difference from the thigh in `bend_shins`, which reads the pose through a `Lookup<Leg>`.

### 6. A tail that drags

The tail is a `Rope` in a resource, anchored behind the body. Seen from above nothing pulls it down, so `gravity` is zero; strong `damping` acts as friction on the ground and `stiffness` keeps it from folding:

```rust
rig.tail = rope(start + vec2(-26.0, 0.0), 12, 9.0, PI);
rig.tail.gravity = vec2(0.0, 0.0);
rig.tail.damping = 0.85;
rig.tail.stiffness = 0.6;
```

Each frame `swing_tail` steps it from the body's rear, `to_world(rig.body, vec2(-26.0, 0.0))`, and `place_tail` puts one shrinking circle on each point.

### 7. Antennae that lag

The antenna tips are roots of their own, each with a `Spring2` chasing the point where it would be on a rigid head. The spring is soft, so the tips trail behind when the head turns and wobble when it stops; a line from the base on the head to the tip draws the stalk:

```rust
let base = to_world(rig.head, a.base);
t.position = a.tip.update(to_world(rig.head, a.offset), 160.0, 7.0, time.dt);
draw.line(base, t.position, rgb(60, 110, 70), 2.0);
```

### Where to go from here

- Swap the shapes for sprites: put each bone's image on the same entity with `anchor` on its joint.
- Turn a side-view character around by setting the root's `scale.x` to -1: its children mirror with it, rotations included.
- Give a spider eight legs in four groups, or a centipede a row of legs whose phases come from `sine_wave`.
- Make a neck or a tentacle that reaches with an `IkChain`, and use `limit_all` so it curls instead of folding.
