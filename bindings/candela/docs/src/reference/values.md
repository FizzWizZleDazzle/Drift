<!-- Written by scripts/candela-reference.py from the drift module and
     bindings/candela/docs/api-notes.md. Edit those, not this page. -->

# Values and Functions

Plain values and free functions the module provides. A name you declare yourself shadows the one from the module, so a game can have its own `log` or `min`.

## Vec2

A 2D vector: a position, an offset, a direction or a velocity. `a + b`, `a - b`, `-a`, `a * s` and `a / s` (with a float `s`) work as expected.

```rust
struct Vec2 {
    x: float = 0.0,
    y: float = 0.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `x` | `float` | `0.0` | Horizontal, growing to the right |
| `y` | `float` | `0.0` | Vertical, growing downward |

```rust
let p = vec2(10.0, 20.0) + vec2(1.0, 0.0) * 5.0;
```

### Vec2.dot

```rust
fn dot(self, o: Vec2) -> float
```

```rust
let facing = dir.dot(to_player) > 0.0;
```

### Vec2.length

```rust
fn length(self) -> float
```

```rust
if v.linear.length() > 300.0 { v.linear = v.linear.normalized() * 300.0; }
```

### Vec2.distance

```rust
fn distance(self, o: Vec2) -> float
```

```rust
if t.position.distance(goal) < 8.0 { arrived = true; }
```

### Vec2.normalized

```rust
fn normalized(self) -> Vec2
```

The same direction with length 1; the zero vector stays zero.

```rust
let dir = (target - t.position).normalized();
```

### Vec2.lerp

```rust
fn lerp(self, o: Vec2, t: float) -> Vec2
```

```rust
t.position = t.position.lerp(target, 0.1);   // eases toward the target
```

### Vec2.angle

```rust
fn angle(self) -> float
```

Radians from the +x axis toward +y (down on screen).

```rust
t.rotation = (input.mouse_world() - t.position).angle();
```

### Vec2.rotated

```rust
fn rotated(self, radians: float) -> Vec2
```

Turned by `radians` about the origin.

```rust
let spread = dir.rotated(0.2);
```

### Vec2.perp

```rust
fn perp(self) -> Vec2
```

Turned a quarter turn: (x, y) -> (-y, x).

```rust
let side = dir.perp();
```

### Vec2.length_squared

```rust
fn length_squared(self) -> float
```

The length times itself, cheaper than `length` for comparisons.

```rust
if offset.length_squared() < 100.0 { close = true; }
```

### Vec2.clamp_length

```rust
fn clamp_length(self, max_length: float) -> Vec2
```

The same direction, no longer than `max_length`.

```rust
v.linear = v.linear.clamp_length(250.0);
```

### Vec2.cross

```rust
fn cross(self, o: Vec2) -> float
```

### Vec2.move_towards

```rust
fn move_towards(self, target: Vec2, max_distance: float) -> Vec2
```

Toward target by at most max_distance, never past it.

### Vec2.reflect

```rust
fn reflect(self, normal: Vec2) -> Vec2
```

Bounced off a surface with this normal.

## vec2

```rust
fn vec2(x: float, y: float) -> Vec2
```

```rust
let up = vec2(0.0, -1.0);
```

## from_angle

```rust
fn from_angle(radians: float) -> Vec2
```

The unit vector at `radians` from the +x axis.

```rust
let v = from_angle(t.rotation) * 300.0;
```

## Rect

A rectangle: top-left corner and size.

```rust
struct Rect {
    x: float = 0.0,
    y: float = 0.0,
    w: float = 0.0,
    h: float = 0.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `x` | `float` | `0.0` | Left edge |
| `y` | `float` | `0.0` | Top edge |
| `w` | `float` | `0.0` | Width |
| `h` | `float` | `0.0` | Height |

```rust
let arena = rect(0.0, 0.0, 960.0, 540.0);
```

### Rect.contains

```rust
fn contains(self, p: Vec2) -> bool
```

Whether a point is inside.

```rust
if button.contains(input.mouse_position()) { hovered = true; }
```

### Rect.overlaps

```rust
fn overlaps(self, o: Rect) -> bool
```

Whether two rects overlap.

```rust
if a.overlaps(b) { log("touching"); }
```

### Rect.center

```rust
fn center(self) -> Vec2
```

The middle point.

```rust
let mid = arena.center();
```

### Rect.clamp_point

```rust
fn clamp_point(self, p: Vec2) -> Vec2
```

The point inside the rect nearest p.

### Rect.expand

```rust
fn expand(self, by: float) -> Rect
```

Grown by `by` on every side; negative shrinks it.

### Rect.centered_at

```rust
fn centered_at(self, c: Vec2) -> Rect
```

A rect of size w x h centred on c.

## rect

```rust
fn rect(x: float, y: float, w: float, h: float) -> Rect
```

```rust
let r = rect(10.0, 10.0, 100.0, 40.0);
```

## Color

A color with red, green, blue and alpha from 0.0 to 1.0. Build one from 0 to 255 values with `rgb` and `rgba`.

```rust
struct Color {
    r: float = 1.0,
    g: float = 1.0,
    b: float = 1.0,
    a: float = 1.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `r` | `float` | `1.0` | Red |
| `g` | `float` | `1.0` | Green |
| `b` | `float` | `1.0` | Blue |
| `a` | `float` | `1.0` | Alpha: 0 is invisible, 1 is solid |

```rust
let sky = Color { r: 0.4, g: 0.6, b: 0.9, a: 1.0 };
```

### Color.alpha

```rust
fn alpha(self, a: float) -> Color
```

The same color with another alpha, 0 to 1.

```rust
let faded = gold.alpha(0.3);
```

### Color.lerp

```rust
fn lerp(self, o: Color, t: float) -> Color
```

A blend from this color to `o`; `t` from 0 to 1.

```rust
text.color = rgb(255, 255, 255).lerp(rgb(255, 60, 60), danger);
```

### Color.to_hsv

```rust
fn to_hsv(self) -> Hsv
```

### Color.lighten

```rust
fn lighten(self, amount: float) -> Color
```

Toward white (amount 0.0 to 1.0), keeping alpha.

### Color.darken

```rust
fn darken(self, amount: float) -> Color
```

Toward black, keeping alpha.

### Color.shift_hue

```rust
fn shift_hue(self, turn: float) -> Color
```

The hue turned by `turn` (1.0 is once around).

### Color.to_hex

```rust
fn to_hex(self) -> string
```

"#rrggbb", or "#rrggbbaa" when not opaque.

## rgb

```rust
fn rgb(r: int, g: int, b: int) -> Color
```

A solid color from 0 to 255 values.

```rust
let gold = rgb(255, 210, 60);
```

## rgba

```rust
fn rgba(r: int, g: int, b: int, a: int) -> Color
```

A color with alpha, all from 0 to 255.

```rust
let shade = rgba(0, 0, 0, 128);
```

## Entity

An entity's id. `Entity {}` is no entity.

```rust
struct Entity {
    id: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | The engine's id; 0 is no entity |

```rust
@resource
struct Target { entity: Entity = Entity { .. } }

if target.entity.id == 0 { log("nothing selected"); }
```

### Entity.alive

```rust
fn alive(self) -> bool
```

Whether the entity exists: false for `Entity {}` and after a despawn has been applied.

## hud_layer

```rust
fn hud_layer() -> int
```

The screen layer. It draws last, after post effects, in screen pixels without a camera: (0, 0) is the top-left of the window's logical area. Put scores, menus and health bars there.

```rust
Text { value: "Lives 3", layer: hud_layer(), .. }
```

## layer_mask

```rust
fn layer_mask(layers: int[]) -> int
```

A layer mask holding `layers`, for `Camera.layer_mask`.

```rust
Camera { layer_mask: layer_mask([0, 1, 2]), .. }
```

## ALL_LAYERS

```rust
const ALL_LAYERS: int = 2147483647
```

Every world layer, 0 to 30: the default `layer_mask` of a camera or light.

```rust
Camera { layer_mask: ALL_LAYERS, .. }
```

## ALL_CATEGORIES

```rust
const ALL_CATEGORIES: int = 65535
```

Every collider category, for queries that see everything.

```rust
let ray = physics.raycast_mask(p, dir, 100.0, ALL_CATEGORIES);
```

## PI

```rust
const PI: float = 3.14159265358979
```

Half a turn in radians.

```rust
tween().rotate_by(PI, 0.3, Ease::OutQuad)
```

## TAU

```rust
const TAU: float = 6.28318530717959
```

A whole turn in radians.

```rust
let angle = r.range_float(0.0, TAU);
```

## sin

```rust
fn sin(x: float) -> float
```

Trigonometry in radians, without importing std/math.

```rust
t.position = vec2(x, 100.0 + sin(time.elapsed * 2.0) * 20.0);
```

## cos

```rust
fn cos(x: float) -> float
```

```rust
let x = cos(angle) * radius;
```

## atan2

```rust
fn atan2(y: float, x: float) -> float
```

The angle of the point (x, y) from the +x axis, -PI to PI.

```rust
let a = atan2(dy, dx);
```

## clamp

```rust
fn clamp(x: float, lo: float, hi: float) -> float
```

Numbers held between bounds, and the smaller or larger of two.

```rust
hp = clamp(hp + 10.0, 0.0, 100.0);
```

## min

```rust
fn min(a: float, b: float) -> float
```

The smaller of two floats.

```rust
let step = min(distance, speed * time.dt);
```

## max

```rust
fn max(a: float, b: float) -> float
```

The larger of two floats.

```rust
let left = max(0.0, timer - time.dt);
```

## log

```rust
fn log(message: string)
```

Writes a line to the engine log: the terminal on the desktop, the browser console on the web.

```rust
log("wave " + string(waves.count));
```

## warn

```rust
fn warn(message: string)
```

Writes a warning to the engine log.

```rust
warn("no spawn point in this level");
```

## Timer

Counts down game time for spawn waves, cooldowns and delays. It is a plain value: keep it in a resource or component and tick it with the frame time.

```rust
struct Timer {
    seconds: float = 1.0,
    left: float = 1.0,
    repeating: bool = false,
    paused: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `seconds` | `float` | `1.0` | The period |
| `left` | `float` | `1.0` | Seconds left in the current period |
| `repeating` | `bool` | `false` | Starts over each time it finishes |
| `paused` | `bool` | `false` | A paused timer does not advance |

```rust
@resource
struct Waves { timer: Timer = repeating_timer(1.5) }

@system
fn spawn(cmd: Commands, time: Time, waves: Waves) {
    for i in range(waves.timer.tick(time.dt)) {
        cmd.spawn(enemy());
    }
}
```

### Timer.tick

```rust
fn tick(self, dt: float) -> int
```

Advances by `dt` seconds and returns how many times the timer finished: 0 or 1 for a one-shot timer, more for a repeating one in a long frame.

```rust
if bomb.fuse.tick(time.dt) > 0 { explode(); }
```

### Timer.finished

```rust
fn finished(self) -> bool
```

Whether a one-shot timer has run out. It stays finished until `reset()`.

```rust
if shield.timer.finished() { shield.on = false; }
```

### Timer.remaining

```rust
fn remaining(self) -> float
```

Seconds left in the current period.

```rust
label.value = string(int(round.left.remaining())) + " s";
```

### Timer.fraction

```rust
fn fraction(self) -> float
```

Progress through the current period, 0 to 1.

```rust
bar.size = vec2(100.0 * reload.fraction(), 6.0);
```

### Timer.reset

```rust
fn reset(self)
```

Starts the period again.

```rust
gun.reload.reset();
```

## timer

```rust
fn timer(seconds: float) -> Timer
```

A timer that finishes once after `seconds`.

```rust
let fuse = timer(3.0);
```

## repeating_timer

```rust
fn repeating_timer(seconds: float) -> Timer
```

A timer that finishes every `seconds`.

```rust
let every_second = repeating_timer(1.0);
```

## Cooldown

Gates an action: ready, used, then ready again after `seconds`.

```rust
struct Cooldown {
    seconds: float = 1.0,
    left: float = 0.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `seconds` | `float` | `1.0` | The time between uses |
| `left` | `float` | `0.0` | Seconds until it is ready |

```rust
@component
struct Gun { reload: Cooldown = cooldown(0.25) }

@system
fn fire(gun: Gun, input: Input, time: Time) {
    gun.reload.tick(time.dt);
    if input.action_held("fire") && gun.reload.try_use() {
        log("bang");
    }
}
```

### Cooldown.tick

```rust
fn tick(self, dt: float)
```

Advances it by `dt` seconds.

```rust
dash.tick(time.dt);
```

### Cooldown.ready

```rust
fn ready(self) -> bool
```

Whether it can be used.

```rust
icon.visible = dash.ready();
```

### Cooldown.try_use

```rust
fn try_use(self) -> bool
```

If ready, starts the cooldown and returns true.

```rust
if input.action_pressed("dash") && dash.try_use() { v.linear = v.linear * 3.0; }
```

### Cooldown.fraction

```rust
fn fraction(self) -> float
```

1.0 right after use, falling to 0.0 when ready.

```rust
draw.arc(t.position, 12.0, 0.0, TAU * dash.fraction(), rgb(255, 255, 255), 2.0);
```

### Cooldown.reset

```rust
fn reset(self)
```

Makes it ready at once.

```rust
dash.reset();
```

## cooldown

```rust
fn cooldown(seconds: float) -> Cooldown
```

A cooldown of `seconds`, ready at once.

```rust
let dash = cooldown(0.8);
```

## plus

```rust
fn plus(bundle: any[], more: any[]) -> any[]
```

A bundle with more components after it, for prefabs with extras: `cmd.spawn(plus(enemy(at(10.0, 20.0)), [Boss {}, Health { hp: 500.0, .. }]))`. A component given twice is the later one, so this also overrides.

```rust
cmd.spawn(plus(enemy(at(10.0, 20.0)), [Boss {}]));
```

## parent_of

```rust
fn parent_of(e: Entity) -> Entity
```

The entity's parent; `Entity {}` for a root.

```rust
if parent_of(e).id == 0 { log("a root"); }
```

## children_of

```rust
fn children_of(e: Entity) -> Entity[]
```

The entity's direct children, in the order they were added.

```rust
for c in children_of(ship) { cmd.despawn(c); }
```

## descendants_of

```rust
fn descendants_of(e: Entity) -> Entity[]
```

Everything below the entity, depth first.

```rust
let parts = descendants_of(robot);
```

## world_transform

```rust
fn world_transform(e: Entity) -> GlobalTransform
```

The entity's transform after every parent, from the engine's current Transforms (changes a running system made land when it returns).

```rust
let tip = world_transform(sword_tip).position;
```

## to_world

```rust
fn to_world(e: Entity, local: Vec2) -> Vec2
```

A point in the entity's space (where a child at `local` would be) to the world, and a world point to the entity's space.

```rust
let muzzle = to_world(gun, vec2(18.0, 0.0));
```

## to_local

```rust
fn to_local(e: Entity, world: Vec2) -> Vec2
```

```rust
let local = to_local(ship, input.mouse_world());
```

## LocalTime

The local date and time. month counts from 1, weekday from 0 (Sunday).

```rust
struct LocalTime {
    year: int = 0,
    month: int = 0,
    day: int = 0,
    hour: int = 0,
    minute: int = 0,
    second: int = 0,
    weekday: int = 0,
    unix: float = 0.0,
}
```

## Platform

What the game runs on, and the browser page around it.

```rust
struct Platform {
    id: int = 0,
}
```

### Platform.is_web

```rust
fn is_web(self) -> bool
```

True in a browser.

### Platform.is_touch

```rust
fn is_touch(self) -> bool
```

True when the device has a touch screen.

### Platform.open_url

```rust
fn open_url(self, url: string)
```

Opens a link in a new tab or the default browser. Browsers block it unless the player tapped, clicked or pressed a key just before.

### Platform.copy

```rust
fn copy(self, text: string)
```

Puts text on the clipboard.

### Platform.pasted

```rust
fn pasted(self) -> string
```

The clipboard's text. In a browser: the text the player last pasted into the page, since pages cannot read the clipboard unasked.

### Platform.query

```rust
fn query(self, name: string) -> string
```

A value from the page address: query("level") is "3" for index.html?level=3, "" when absent, and always "" on the desktop.

### Platform.local_time

```rust
fn local_time(self) -> LocalTime
```

## inverse_lerp

```rust
fn inverse_lerp(a: float, b: float, x: float) -> float
```

Where x sits between a and b: 0.0 at a, 1.0 at b.

## remap

```rust
fn remap(x: float, a: float, b: float, c: float, d: float) -> float
```

x moved from the range a..b to the range c..d.

## wrap

```rust
fn wrap(x: float, lo: float, hi: float) -> float
```

x wrapped into lo up to, not including, hi: angles, looping worlds.

## sign

```rust
fn sign(x: float) -> float
```

-1.0, 0.0 or 1.0.

## radians

```rust
fn radians(degrees: float) -> float
```

## degrees

```rust
fn degrees(radians: float) -> float
```

## circles_overlap

```rust
fn circles_overlap(a: Vec2, ra: float, b: Vec2, rb: float) -> bool
```

Whether two circles touch.

## circle_overlaps_rect

```rust
fn circle_overlaps_rect(c: Vec2, r: float, box: Rect) -> bool
```

Whether a circle touches a rect.

## segments_intersect

```rust
fn segments_intersect(a1: Vec2, a2: Vec2, b1: Vec2, b2: Vec2) -> Option<Vec2>
```

Where segments a1-a2 and b1-b2 cross, if they do.

## flat

```rust
fn flat(points: Vec2[]) -> float[]
```

## steer_call

```rust
fn steer_call(op: string, nums: float[], a: Vec2[], b: Vec2[]) -> float[]
```

## steer_vec

```rust
fn steer_vec(r: float[]) -> Vec2
```

## steer_truncate

```rust
fn steer_truncate(v: Vec2, max_length: float) -> Vec2
```

`v` shortened to at most `max_length`.

## steer_seek

```rust
fn steer_seek(pos: Vec2, vel: Vec2, target: Vec2, max_speed: float) -> Vec2
```

Full speed toward the target.

## steer_flee

```rust
fn steer_flee(pos: Vec2, vel: Vec2, threat: Vec2, max_speed: float, panic_radius: float) -> Vec2
```

Full speed away from the threat while it is within `panic_radius` (0: always).

## steer_arrive

```rust
fn steer_arrive(pos: Vec2, vel: Vec2, target: Vec2, max_speed: float, slow_radius: float) -> Vec2
```

Toward the target, slowing inside `slow_radius` to stop on it.

## steer_pursue

```rust
fn steer_pursue(pos: Vec2, vel: Vec2, target: Vec2, target_vel: Vec2, max_speed: float) -> Vec2
```

Seek where a moving target will be.

## steer_evade

```rust
fn steer_evade(pos: Vec2, vel: Vec2, threat: Vec2, threat_vel: Vec2, max_speed: float, panic_radius: float) -> Vec2
```

Flee from where a moving threat will be.

## steer_separation

```rust
fn steer_separation(pos: Vec2, neighbours: Vec2[], radius: float, max_speed: float) -> Vec2
```

Away from neighbours closer than `radius`, harder the closer they are.

## steer_cohesion

```rust
fn steer_cohesion(pos: Vec2, vel: Vec2, neighbours: Vec2[], radius: float, max_speed: float) -> Vec2
```

Toward the middle of the neighbours within `radius`.

## steer_alignment

```rust
fn steer_alignment(pos: Vec2, vel: Vec2, positions: Vec2[], velocities: Vec2[], radius: float, max_speed: float) -> Vec2
```

Toward the average heading of the neighbours within `radius`.

## Wander

An agent's wandering: keep one per agent, in a component.

```rust
struct Wander {
    angle: float = 0.0,
    distance: float = 40.0,
    radius: float = 20.0,
    jitter: float = 0.5,
}
```

## steer_wander

```rust
fn steer_wander(vel: Vec2, w: Wander, rng: Rng, max_speed: float) -> Vec2
```

Aimless but smooth: each call turns the wander angle by up to `jitter` radians and steers toward a point on a circle ahead.

## PathFollow

An agent's place along a path: keep one per agent, starting at 0.

```rust
struct PathFollow {
    index: int = 0,
    reach_radius: float = 8.0,
    slow_radius: float = 0.0,
}
```

## steer_follow_path

```rust
fn steer_follow_path(pos: Vec2, vel: Vec2, path: Vec2[], f: PathFollow, max_speed: float) -> Vec2
```

Seeks each point in turn, moving on within `reach_radius`, and arrives at the last one.
