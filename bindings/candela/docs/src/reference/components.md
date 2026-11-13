<!-- Written by scripts/candela-reference.py from the drift module and
     bindings/candela/docs/api-notes.md. Edit those, not this page. -->

# Engine Components

Components the engine draws and simulates. Use them like your own: put them in a spawn bundle, take them as parameters, look them up, and assign to their fields to change them. Build each with the fields you need and `..` for the rest.

The drawn, moved and simulated components need a `Transform`; give the entity one with `at(x, y)` or `Transform { .. }`. Some fields are also changed by the engine as it runs (physics moves `Transform`, an emitter turns `playing` off when it finishes); a system sees the value from when it started.

## Transform

Where the entity is, relative to its parent when it has one. Physics, `Velocity`, tweens and camera followers move it.

```rust
struct Transform {
    position: Vec2 = Vec2 { .. },
    rotation: float = 0.0,
    scale: Vec2 = Vec2 { x: 1.0, y: 1.0 },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `position` | `Vec2` | `Vec2 { .. }` | Position in pixels; +Y points down |
| `rotation` | `float` | `0.0` | Turn in radians, clockwise on screen |
| `scale` | `Vec2` | `Vec2 { x: 1.0, y: 1.0 }` | Size multiplier |

```rust
cmd.spawn([Transform { position: vec2(100.0, 50.0), rotation: 0.5, .. }, Sprite { image: art.ship, .. }]);
```

## at

```rust
fn at(x: float, y: float) -> Transform
```

The `Transform` at a position, the shortest way to place an entity.

```rust
cmd.spawn([at(480.0, 270.0), Circle { radius: 10.0, .. }]);
```

## GlobalTransform

The entity's position, rotation and scale after every parent. Assigning one moves the entity there in world space.

```rust
struct GlobalTransform {
    position: Vec2 = Vec2 { .. },
    rotation: float = 0.0,
    scale: Vec2 = Vec2 { x: 1.0, y: 1.0 },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `position` | `Vec2` | `Vec2 { .. }` | Position in the world |
| `rotation` | `float` | `0.0` | Turn in radians in the world |
| `scale` | `Vec2` | `Vec2 { x: 1.0, y: 1.0 }` | Size multiplier in the world |

```rust
@system
fn aim(turret: (Turret, GlobalTransform), draw: Draw) {
    let (_, g) = turret;
    draw.line(g.position, g.position + from_angle(g.rotation) * 40.0, rgb(255, 80, 80), 2.0);
}
```

## Parent

Makes the entity's Transform relative to `entity`. With `keep_world`, the child's Transform is rewritten when the link is made so it stays where it is on screen. `Parent { entity: Entity {}, .. }` detaches it.

```rust
struct Parent {
    entity: Entity = Entity { .. },
    keep_world: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `entity` | `Entity` | `Entity { .. }` | The parent; no entity detaches |
| `keep_world` | `bool` | `false` | Rewrite the child's Transform when linking, so it stays where it is |

```rust
cmd.spawn([at(12.0, 0.0), Sprite { image: art.gun, .. }, Parent { entity: ship, .. }]);
```

## Name

A name for an entity, shown in the inspector.

```rust
struct Name {
    value: string = "",
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `value` | `string` | `""` | The name |

```rust
cmd.spawn([at(0.0, 0.0), Name { value: "Boss" }, Boss { .. }]);
```

## Sprite

Draws a texture at the entity's position. Load the texture with [`texture`](assets.md#texture).

```rust
struct Sprite {
    image: Texture = Texture { .. },
    frame: Rect = Rect { .. },
    anchor: Vec2 = Vec2 { x: 0.5, y: 0.5 },
    color: Color = Color { .. },
    flip_x: bool = false,
    flip_y: bool = false,
    z: float = 0.0,
    layer: int = 0,
    visible: bool = true,
    blend: Blend = Blend::Alpha,
    material: Material = Material { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `image` | `Texture` | `Texture { .. }` | The texture to draw |
| `frame` | `Rect` | `Rect { .. }` | The part of the image to draw, in pixels; a zero-size rect draws all of it |
| `anchor` | `Vec2` | `Vec2 { x: 0.5, y: 0.5 }` | The point of the image placed at the position, from (0, 0) top-left to (1, 1) bottom-right; it turns and scales around this point |
| `color` | `Color` | `Color { .. }` | Tint, multiplied with the image's colors |
| `flip_x` | `bool` | `false` | Mirrors the image left to right |
| `flip_y` | `bool` | `false` | Mirrors the image top to bottom |
| `z` | `float` | `0.0` | Draw order within the layer; higher draws on top |
| `layer` | `int` | `0` | Render layer, 0 to 30 for the world, `hud_layer()` for the screen |
| `visible` | `bool` | `true` | `false` hides it without removing it |
| `blend` | `Blend` | `Blend::Alpha` | How it mixes with what is behind it |
| `material` | `Material` | `Material { .. }` | a custom shader; Material {} draws as usual |

```rust
cmd.spawn([at(160.0, 90.0), Sprite { image: texture("assets/ship.png"), anchor: vec2(0.5, 1.0), .. }]);
```

## Rectangle

Draws a filled rectangle, or its outline.

```rust
struct Rectangle {
    size: Vec2 = Vec2 { x: 32.0, y: 32.0 },
    color: Color = Color { .. },
    outline: float = 0.0,
    corner_radius: float = 0.0,
    anchor: Vec2 = Vec2 { x: 0.5, y: 0.5 },
    z: float = 0.0,
    layer: int = 0,
    visible: bool = true,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `size` | `Vec2` | `Vec2 { x: 32.0, y: 32.0 }` | Width and height in pixels |
| `color` | `Color` | `Color { .. }` | Fill or line color |
| `outline` | `float` | `0.0` | Line width; 0 fills the rectangle |
| `corner_radius` | `float` | `0.0` | Rounds the corners |
| `anchor` | `Vec2` | `Vec2 { x: 0.5, y: 0.5 }` | The point placed at the position, from (0, 0) top-left to (1, 1) bottom-right |
| `z` | `float` | `0.0` | Draw order within the layer |
| `layer` | `int` | `0` | Render layer |
| `visible` | `bool` | `true` | `false` hides it |

```rust
cmd.spawn([at(320.0, 340.0), Rectangle { size: vec2(640.0, 20.0), color: rgb(70, 70, 90), .. }]);
```

## Circle

Draws a filled circle, or its outline.

```rust
struct Circle {
    radius: float = 16.0,
    color: Color = Color { .. },
    outline: float = 0.0,
    z: float = 0.0,
    layer: int = 0,
    visible: bool = true,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `radius` | `float` | `16.0` | Radius in pixels |
| `color` | `Color` | `Color { .. }` | Fill or line color |
| `outline` | `float` | `0.0` | Line width; 0 fills the circle |
| `z` | `float` | `0.0` | Draw order within the layer |
| `layer` | `int` | `0` | Render layer |
| `visible` | `bool` | `true` | `false` hides it |

```rust
cmd.spawn([at(100.0, 100.0), Circle { radius: 20.0, color: rgb(255, 200, 60), .. }]);
```

## Text

Draws a string at the entity's position.

```rust
struct Text {
    value: string = "",
    size: float = 24.0,
    color: Color = Color { .. },
    align: Align = Align::Left,
    valign: VAlign = VAlign::Top,
    wrap: float = 0.0,
    line_spacing: float = 1.0,
    outline: float = 0.0,
    outline_color: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 },
    shadow_offset: Vec2 = Vec2 { .. },
    shadow_color: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: 0.6 },
    rich_text: bool = false,
    font: Font = Font { .. },
    z: float = 0.0,
    layer: int = 0,
    visible: bool = true,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `value` | `string` | `""` | The string |
| `size` | `float` | `24.0` | Pixel height; raise it rather than scaling the Transform, so the glyphs stay sharp |
| `color` | `Color` | `Color { .. }` | Text color |
| `align` | `Align` | `Align::Left` | Which point of the text sits on the position, left to right |
| `valign` | `VAlign` | `VAlign::Top` | Which point of the text sits on the position, top to bottom |
| `wrap` | `float` | `0.0` | Wraps lines at this width in pixels; 0 does not wrap |
| `line_spacing` | `float` | `1.0` | Multiplies the distance between lines |
| `outline` | `float` | `0.0` | Outline width around each glyph; 0 for none |
| `outline_color` | `Color` | `Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 }` | Outline color |
| `shadow_offset` | `Vec2` | `Vec2 { .. }` | Drop shadow offset; zero for none |
| `shadow_color` | `Color` | `Color { r: 0.0, g: 0.0, b: 0.0, a: 0.6 }` | Drop shadow color |
| `rich_text` | `bool` | `false` | Reads `[color=#rrggbb]...[/color]` tags in `value` |
| `font` | `Font` | `Font { .. }` | The font; `Font {}` is the built-in one |
| `z` | `float` | `0.0` | Draw order within the layer |
| `layer` | `int` | `0` | Render layer; `hud_layer()` places it in screen pixels |
| `visible` | `bool` | `true` | `false` hides it |

```rust
cmd.spawn([at(16.0, 12.0), Text { value: "Score 0", size: 28.0, outline: 2.0, layer: hud_layer(), .. }, ScoreLabel {}]);
```

### Text.measure

```rust
fn measure(self) -> Vec2
```

The size this text covers as drawn, outline and shadow left out.

## Animator

Plays named clips on the entity's `Sprite`, setting its image and frame as time passes. `play` names the clip to start with. See [Movement](../guides/movement.md#animation-frames).

```rust
struct Animator {
    clips: {string: AnimationClip} = {},
    play: string = "",
    then: string = "",
    once: bool = false,
    speed: float = 1.0,
    playing: bool = true,
    frame: int = 0,
    time: float = 0.0,
    ended: string = "",
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `clips` | `{string: AnimationClip}` | `{}` | The clips, by name |
| `play` | `string` | `""` | The clip playing; writing an unknown name logs `drift_unknown_clip` |
| `then` | `string` | `""` | The clip to switch to when this one ends; empty holds the last frame |
| `once` | `bool` | `false` | Whether the clip playing ends after one pass, even a looping one |
| `speed` | `float` | `1.0` | Time multiplier: 2.0 plays twice as fast, 0.0 freezes |
| `playing` | `bool` | `true` | False pauses on the current frame |
| `frame` | `int` | `0` | Index into the playing clip's frames; setting it jumps there |
| `time` | `float` | `0.0` | Seconds into the playing clip; setting it jumps there |
| `ended` | `string` | `""` | The last clip that played to its end, until another starts |

```rust
let hero = assets.spritesheet(texture("assets/hero.png"), 32.0, 32.0);
cmd.spawn([at(100.0, 100.0), Sprite { image: hero.texture, .. }, Animator {
    clips: {
        "idle":   clip(hero, [0, 1, 2, 3], 0.15),
        "run":    clip(hero, range(8, 14), 0.08),
        "attack": clip(slash, [0, 1, 2], [0.05, 0.05, 0.2]).once()
    },
    play: "idle",
    ..
}]);
```

### Animator.play

```rust
fn play(self, name: string)
```

Switches to a clip by name, from its first frame. The clip already playing keeps going, so calling this every frame is fine; a clip that ended starts again. An unknown name raises `drift_unknown_clip`.

```rust
if v.linear.x != 0.0 {
    anim.play("run");
} else {
    anim.play("idle");
}
```

### Animator.play_once

```rust
fn play_once(self, name: string, then: string)
```

Plays a clip one time, even a looping one, then switches to `then`. An empty `then` holds the last frame.

```rust
anim.play_once("attack", "idle");
```

### Animator.finished

```rust
fn finished(self, name: string) -> bool
```

Whether the clip played to its end and no other clip has started since. To act once when it happens, take the `AnimationFinished` message instead.

```rust
if anim.finished("die") {
    cmd.despawn(e);
}
```

## AnimationClip

Frames of a sprite sheet played over time. Make one with [`clip`](#clip) or an Aseprite tag's [`Spritesheet.clip`](assets.md#spritesheetclip).

```rust
struct AnimationClip {
    sheet: Spritesheet = Spritesheet { .. },
    frames: int[] = [],
    seconds: float[] = [],
    looping: bool = true,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `sheet` | `Spritesheet` | `Spritesheet { .. }` | The sheet the frames come from; its texture shows while the clip plays |
| `frames` | `int[]` | `[]` | Frame indices into the sheet |
| `seconds` | `float[]` | `[]` | How long each frame shows: one value for every frame, or one per frame |
| `looping` | `bool` | `true` | Whether the clip starts again after its last frame |

### AnimationClip.once

```rust
fn once(self) -> AnimationClip
```

The same clip, played one time instead of looping. When it ends, the animator holds its last frame, or switches to `then`.

```rust
let die = clip(hero, range(16, 22), 0.1).once();
```

### AnimationClip.frame_seconds

```rust
fn frame_seconds(self, i: int) -> float
```

How long frame `i` of the clip shows.

```rust
let last = swing.frame_seconds(swing.frames.len() - 1);
```

### AnimationClip.length

```rust
fn length(self) -> float
```

The whole clip, in seconds.

```rust
let seconds = swing.length();
```

## clip

```rust
fn clip(sheet: Spritesheet, frames: int[], seconds: float | float[]) -> AnimationClip
```

A clip of `frames` from `sheet`, each shown for `seconds`: one number for every frame, or a list with one per frame. Raises `drift_bad_clip` for a frame outside the sheet or a list whose length does not match the frames.

```rust
let idle = clip(hero, [0, 1, 2, 3], 0.15);
let swing = clip(slash, [0, 1, 2], [0.05, 0.05, 0.2]).once();
```

## AnimationFinished

Sent when a clip plays to its end: a clip made with `once()`, or one started with `play_once`. A clip that switches to `then` sends it as it switches.

```rust
@message
struct AnimationFinished {
    entity: Entity,
    clip: string,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `entity` | `Entity` | required | The entity whose animator played the clip |
| `clip` | `string` | required | The clip's name |

```rust
@system
fn remove_fallen(done: AnimationFinished, cmd: Commands) {
    if done.clip == "die" {
        cmd.despawn(done.entity);
    }
}
```

## Camera

Without a camera, world position (0, 0) is the top-left of the screen and one world unit is one logical pixel. A camera centers its view on its entity's position. See [Cameras](../guides/cameras.md).

```rust
struct Camera {
    zoom: float = 1.0,
    rotation: float = 0.0,
    active: bool = true,
    viewport: Rect = Rect { x: 0.0, y: 0.0, w: 1.0, h: 1.0 },
    order: int = 0,
    layer_mask: int = ALL_LAYERS,
    target: Texture = Texture { .. },
    clear: bool = false,
    clear_color: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `zoom` | `float` | `1.0` | Magnification; 2.0 shows the world twice as large |
| `rotation` | `float` | `0.0` | Turn of the view in radians |
| `active` | `bool` | `true` | `false` stops this camera drawing |
| `viewport` | `Rect` | `Rect { x: 0.0, y: 0.0, w: 1.0, h: 1.0 }` | The part of the window it draws to, from (0, 0) to (1, 1) |
| `order` | `int` | `0` | Cameras draw in ascending order |
| `layer_mask` | `int` | `ALL_LAYERS` | The layers it draws; see `layer_mask()` |
| `target` | `Texture` | `Texture { .. }` | A render target to draw into; `Texture {}` draws to the window |
| `clear` | `bool` | `false` | Clears its target or viewport before drawing |
| `clear_color` | `Color` | `Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 }` | The color it clears to |

```rust
cmd.spawn([at(480.0, 270.0), Camera { zoom: 2.0, .. }]);
```

## CameraFollow

Moves the camera smoothly toward `target`, staying still while the target is within `dead_zone` (half sizes) of the point it aims at.

```rust
struct CameraFollow {
    target: Entity = Entity { .. },
    smoothing: float = 5.0,
    offset: Vec2 = Vec2 { .. },
    dead_zone: Vec2 = Vec2 { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `target` | `Entity` | `Entity { .. }` | The entity to follow |
| `smoothing` | `float` | `5.0` | How quickly it catches up; higher is snappier |
| `offset` | `Vec2` | `Vec2 { .. }` | Aims this far from the target |
| `dead_zone` | `Vec2` | `Vec2 { .. }` | Half size of the box the target moves in without moving the camera |

```rust
cmd.spawn([at(0.0, 0.0), Camera { .. }, CameraFollow { target: player, smoothing: 6.0, dead_zone: vec2(40.0, 24.0), .. }]);
```

## CameraBounds

Keeps what the camera shows inside `area`, such as the level's rect.

```rust
struct CameraBounds {
    area: Rect = Rect { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `area` | `Rect` | `Rect { .. }` | The world rect the camera's view stays inside |

```rust
cmd.insert(camera, CameraBounds { area: rect(0.0, 0.0, 3200.0, 720.0) });
```

## Tilemap

Shows a [`Map`](assets.md#map) in the world.

```rust
struct Tilemap {
    map: Map = Map { .. },
    z: float = 0.0,
    layer: int = 0,
    colliders: bool = true,
    visible: bool = true,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `map` | `Map` | `Map { .. }` | The map to show |
| `z` | `float` | `0.0` | Draw order within the layer |
| `layer` | `int` | `0` | Render layer |
| `colliders` | `bool` | `true` | Builds static colliders from the map's solid layers |
| `visible` | `bool` | `true` | `false` hides it |

```rust
let map = assets.tilemap("assets/level1.tmj");
cmd.spawn([at(0.0, 0.0), Tilemap { map: map, .. }]);
```

## Velocity

Moves the entity every frame. On an entity with a dynamic `RigidBody` it is the body's velocity: read it to see how fast the body moves, write it to set its speed.

```rust
struct Velocity {
    linear: Vec2 = Vec2 { .. },
    angular: float = 0.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `linear` | `Vec2` | `Vec2 { .. }` | Pixels per second |
| `angular` | `float` | `0.0` | Radians per second |

```rust
cmd.spawn([at(0.0, 80.0), Sprite { image: art.cloud, .. }, Velocity { linear: vec2(12.0, 0.0), .. }]);
```

## RigidBody

Makes the entity a physics body. Give it a collider, and a `Velocity`: a moving body shows its motion through the entity's `Velocity`, and without one it stays where it was drawn. The body is made at the end of the frame its components are added; changing a field later makes it again. See [Collisions and Physics](../guides/collisions.md).

```rust
struct RigidBody {
    kind: BodyKind = BodyKind::Dynamic,
    fixed_rotation: bool = false,
    gravity_scale: float = 1.0,
    linear_damping: float = 0.0,
    angular_damping: float = 0.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `kind` | `BodyKind` | `BodyKind::Dynamic` | `Dynamic` falls and is pushed, `Kinematic` moves only by its Velocity, `Static` never moves |
| `fixed_rotation` | `bool` | `false` | Keeps it upright, for characters |
| `gravity_scale` | `float` | `1.0` | Multiplies world gravity for this body |
| `linear_damping` | `float` | `0.0` | Slows its motion over time |
| `angular_damping` | `float` | `0.0` | Slows its spin over time |

```rust
cmd.spawn([at(200.0, 50.0), Rectangle { .. }, RigidBody { fixed_rotation: true, .. }, BoxCollider { .. }, Velocity { .. }]);
```

## BoxCollider

A box-shaped collider. Without a `RigidBody` it is static.

```rust
struct BoxCollider {
    half_size: Vec2 = Vec2 { x: 16.0, y: 16.0 },
    offset: Vec2 = Vec2 { .. },
    sensor: bool = false,
    density: float = 1.0,
    friction: float = 0.3,
    restitution: float = 0.0,
    category: int = 1,
    mask: int = 65535,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `half_size` | `Vec2` | `Vec2 { x: 16.0, y: 16.0 }` | Half the width and height, in pixels |
| `offset` | `Vec2` | `Vec2 { .. }` | Moves the box from the entity's position |
| `sensor` | `bool` | `false` | Detects overlaps without pushing anything |
| `density` | `float` | `1.0` | Mass per area |
| `friction` | `float` | `0.3` | 0 is ice |
| `restitution` | `float` | `0.0` | Bounciness; 1 keeps all the speed |
| `category` | `int` | `1` | The bits this collider is |
| `mask` | `int` | `65535` | The categories it collides with; two colliders touch when each one's category is in the other's mask |

```rust
cmd.spawn([at(320.0, 470.0), BoxCollider { half_size: vec2(320.0, 10.0), .. }]);   // a floor
```

## CircleCollider

A circle-shaped collider. Without a `RigidBody` it is static.

```rust
struct CircleCollider {
    radius: float = 16.0,
    offset: Vec2 = Vec2 { .. },
    sensor: bool = false,
    density: float = 1.0,
    friction: float = 0.3,
    restitution: float = 0.0,
    category: int = 1,
    mask: int = 65535,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `radius` | `float` | `16.0` | Radius in pixels |
| `offset` | `Vec2` | `Vec2 { .. }` | Moves the circle from the entity's position |
| `sensor` | `bool` | `false` | Detects overlaps without pushing anything |
| `density` | `float` | `1.0` | Mass per area |
| `friction` | `float` | `0.3` | 0 is ice |
| `restitution` | `float` | `0.0` | Bounciness; 1 keeps all the speed |
| `category` | `int` | `1` | The bits this collider is |
| `mask` | `int` | `65535` | The categories it collides with |

```rust
cmd.spawn([at(x, y), Circle { radius: 8.0, .. }, CircleCollider { radius: 8.0, sensor: true, .. }, Coin {}]);
```

## Joint

Joins this entity's body to `target`'s, or pins it to the world when `target` is no entity. Both need a RigidBody or a collider; the joint is made once both bodies exist, and made again when a field changes. Anchors are in each body's local space; without a target, `target_anchor` is a point in the world.

```rust
struct Joint {
    kind: JointKind = JointKind::Revolute,
    target: Entity = Entity { .. },
    anchor: Vec2 = Vec2 { .. },
    target_anchor: Vec2 = Vec2 { .. },
    length: float = 0.0,
    min_length: float = 0.0,
    max_length: float = 0.0,
    stiffness: float = 0.0,
    damping: float = 0.0,
    limit: bool = false,
    lower: float = 0.0,
    upper: float = 0.0,
    motor: bool = false,
    motor_speed: float = 0.0,
    max_motor_force: float = 0.0,
    axis: Vec2 = Vec2 { x: 1.0, y: 0.0 },
    collide_connected: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `kind` | `JointKind` | `JointKind::Revolute` | The kind of joint |
| `target` | `Entity` | `Entity { .. }` | The other body; no entity pins this one to the world |
| `anchor` | `Vec2` | `Vec2 { .. }` | Where it attaches, in this body's local space |
| `target_anchor` | `Vec2` | `Vec2 { .. }` | Where it attaches on the target, or a world point without a target |
| `length` | `float` | `0.0` | Distance: 0 keeps the distance the bodies start at |
| `min_length` | `float` | `0.0` | Distance: with max_length above min_length, a rope's slack |
| `max_length` | `float` | `0.0` |  |
| `stiffness` | `float` | `0.0` | Distance and Weld: spring frequency in Hz; 0 is rigid |
| `damping` | `float` | `0.0` | spring damping, 0 to 1 |
| `limit` | `bool` | `false` | Revolute: angle range; Prismatic: travel range |
| `lower` | `float` | `0.0` |  |
| `upper` | `float` | `0.0` |  |
| `motor` | `bool` | `false` |  |
| `motor_speed` | `float` | `0.0` | radians or pixels per second |
| `max_motor_force` | `float` | `0.0` | torque for Revolute |
| `axis` | `Vec2` | `Vec2 { x: 1.0, y: 0.0 }` | Prismatic: the slide direction |
| `collide_connected` | `bool` | `false` |  |

```rust
// A pendulum: a ball hanging from a point in the world.
cmd.spawn([at(300.0, 200.0), Circle { radius: 12.0, .. }, RigidBody { .. }, CircleCollider { radius: 12.0, .. },
           Velocity { .. }, Joint { kind: JointKind::Distance, target_anchor: vec2(300.0, 80.0), .. }]);
```

## Lifetime

Despawns the entity, with its children, when the time runs out. Use it for bullets, floating labels and debris.

```rust
struct Lifetime {
    seconds: float = 1.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `seconds` | `float` | `1.0` | Seconds left |

```rust
cmd.spawn([at(x, y), Text { value: "+1", .. }, Velocity { linear: vec2(0.0, -60.0), .. }, Lifetime { seconds: 0.6 }]);
```

## Tween

Animates the entity from where it is to a target, over time, along an easing curve. Build one with `tween()` and chain steps; they run one after another. An entity holds one tween; inserting a changed one starts it again.

```rust
struct Tween {
    steps: TweenStep[] = [],
    loops: int = 0,
    mirror: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `steps` | `TweenStep[]` | `[]` | The steps, built by the methods below |
| `loops` | `int` | `0` | How many times to play the steps again; -1 forever |
| `mirror` | `bool` | `false` | Plays back and forth instead of from the start |

```rust
cmd.spawn([at(x, y), Sprite { image: art.star, .. },
           tween().scale_to(vec2(1.4, 1.4), 0.1, Ease::OutQuad).scale_to(vec2(1.0, 1.0), 0.3, Ease::OutBack)]);
```

### Tween.move_to

```rust
fn move_to(self, to: Vec2, seconds: float, e: Ease) -> Tween
```

Moves to a position.

```rust
tween().move_to(vec2(400.0, 100.0), 1.0, Ease::OutCubic)
```

### Tween.move_by

```rust
fn move_by(self, by: Vec2, seconds: float, e: Ease) -> Tween
```

Moves by an offset from where the step starts.

```rust
tween().move_by(vec2(0.0, -20.0), 0.3, Ease::OutQuad)
```

### Tween.scale_to

```rust
fn scale_to(self, to: Vec2, seconds: float, e: Ease) -> Tween
```

Scales to a size multiplier.

```rust
tween().scale_to(vec2(2.0, 2.0), 0.2, Ease::OutBack)
```

### Tween.rotate_to

```rust
fn rotate_to(self, radians: float, seconds: float, e: Ease) -> Tween
```

Turns to an angle in radians.

```rust
tween().rotate_to(PI, 0.5, Ease::InOutSine)
```

### Tween.rotate_by

```rust
fn rotate_by(self, radians: float, seconds: float, e: Ease) -> Tween
```

Turns by an angle in radians.

```rust
tween().rotate_by(TAU, 1.0, Ease::Linear).repeat(-1)   // spins forever
```

### Tween.fade_to

```rust
fn fade_to(self, alpha: float, seconds: float, e: Ease) -> Tween
```

Sprite alpha, 0.0 to 1.0.

```rust
tween().delay(1.0).fade_to(0.0, 0.5, Ease::InQuad)
```

### Tween.zoom_to

```rust
fn zoom_to(self, zoom: float, seconds: float, e: Ease) -> Tween
```

Camera zoom.

```rust
cmd.insert(camera, tween().zoom_to(2.0, 0.8, Ease::InOutCubic));
```

### Tween.tint_to

```rust
fn tint_to(self, c: Color, seconds: float, e: Ease) -> Tween
```

Changes the sprite's color.

```rust
tween().tint_to(rgb(255, 80, 80), 0.1, Ease::Linear).tint_to(rgb(255, 255, 255), 0.3, Ease::OutQuad)
```

### Tween.delay

```rust
fn delay(self, seconds: float) -> Tween
```

Waits before the next step.

```rust
tween().delay(0.5).move_by(vec2(0.0, 40.0), 0.3, Ease::OutBounce)
```

### Tween.repeat

```rust
fn repeat(self, times: int) -> Tween
```

Plays the whole chain again `times` more times from the start; -1 repeats forever.

```rust
tween().rotate_by(TAU, 2.0, Ease::Linear).repeat(-1)
```

### Tween.yoyo

```rust
fn yoyo(self, times: int) -> Tween
```

Plays the chain forward, then backward, `times` more times; -1 forever.

```rust
tween().scale_to(vec2(1.2, 1.2), 0.4, Ease::InOutSine).yoyo(-1)   // a pulse
```

## tween

```rust
fn tween() -> Tween
```

An empty `Tween` to chain steps onto.

```rust
let bob = tween().move_by(vec2(0.0, -6.0), 0.5, Ease::InOutSine).yoyo(-1);
```

## ParticleEmitter

Emits particles from its entity for as long as `playing` is on. A one-off burst does not need an entity: see [`Effects.burst`](resources.md#effectsburst). See [Particles and Effects](../guides/effects.md).

```rust
struct ParticleEmitter {
    rate: float = 20.0,
    lifetime_min: float = 0.5,
    lifetime_max: float = 1.5,
    speed_min: float = 20.0,
    speed_max: float = 60.0,
    angle_min: float = 0.0,
    angle_max: float = 6.2831853,
    size_start: float = 6.0,
    size_end: float = 1.0,
    color_start: Color = Color { .. },
    color_end: Color = Color { r: 1.0, g: 1.0, b: 1.0, a: 0.0 },
    gravity: Vec2 = Vec2 { .. },
    drag: float = 0.0,
    max_particles: int = 256,
    blend: Blend = Blend::Alpha,
    texture: Texture = Texture { .. },
    shape: EmitShape = EmitShape::Point,
    shape_radius: float = 0.0,
    shape_extents: Vec2 = Vec2 { .. },
    duration: float = -1.0,
    looping: bool = true,
    z: float = 10.0,
    playing: bool = true,
    frame: Rect = Rect { .. },
    size_start_max: float = 0.0,
    size_end_max: float = 0.0,
    rotation_min: float = 0.0,
    rotation_max: float = 0.0,
    spin_min: float = 0.0,
    spin_max: float = 0.0,
    inherit_velocity: float = 0.0,
    local: bool = false,
    prewarm: float = 0.0,
    bounds: Rect = Rect { .. },
    seed: int = 0,
    bursts: Burst[] = [],
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `rate` | `float` | `20.0` | Particles per second |
| `lifetime_min` | `float` | `0.5` | Shortest life in seconds |
| `lifetime_max` | `float` | `1.5` | Longest life in seconds |
| `speed_min` | `float` | `20.0` | Slowest start speed in pixels per second |
| `speed_max` | `float` | `60.0` | Fastest start speed |
| `angle_min` | `float` | `0.0` | Smallest start direction, in radians from +x; -PI/2 is up |
| `angle_max` | `float` | `6.2831853` | Largest start direction |
| `size_start` | `float` | `6.0` | Size at birth, in pixels |
| `size_end` | `float` | `1.0` | Size at death |
| `color_start` | `Color` | `Color { .. }` | Color at birth |
| `color_end` | `Color` | `Color { r: 1.0, g: 1.0, b: 1.0, a: 0.0 }` | Color at death; an alpha of 0 fades them out |
| `gravity` | `Vec2` | `Vec2 { .. }` | Acceleration in pixels per second squared |
| `drag` | `float` | `0.0` | Slows particles; 0 for none |
| `max_particles` | `int` | `256` | How many may be alive at once |
| `blend` | `Blend` | `Blend::Alpha` | `Blend::Additive` makes overlapping particles glow |
| `texture` | `Texture` | `Texture { .. }` | An image for each particle; `Texture {}` draws soft squares |
| `shape` | `EmitShape` | `EmitShape::Point` | Where particles start |
| `shape_radius` | `float` | `0.0` | Radius for `Circle` and `Ring` shapes |
| `shape_extents` | `Vec2` | `Vec2 { .. }` | Half size for `Box`, direction for `Line` |
| `duration` | `float` | `-1.0` | Seconds the emitter runs; -1 forever |
| `looping` | `bool` | `true` | Starts over when `duration` runs out |
| `z` | `float` | `10.0` | Draw order |
| `playing` | `bool` | `true` | Whether it emits; write it to switch the emitter on and off |
| `frame` | `Rect` | `Rect { .. }` | Part of `texture` to draw |
| `size_start_max` | `float` | `0.0` | With it above `size_start`, birth sizes vary between the two |
| `size_end_max` | `float` | `0.0` | The same for the size at death |
| `rotation_min` | `float` | `0.0` | radians at birth |
| `rotation_max` | `float` | `0.0` |  |
| `spin_min` | `float` | `0.0` | radians per second |
| `spin_max` | `float` | `0.0` |  |
| `inherit_velocity` | `float` | `0.0` | share of the emitter's own motion particles keep |
| `local` | `bool` | `false` | particles move with the emitter |
| `prewarm` | `float` | `0.0` | seconds simulated before the first frame |
| `bounds` | `Rect` | `Rect { .. }` | particles stop at its edges; zero size for none |
| `seed` | `int` | `0` | 0 picks one |
| `bursts` | `Burst[]` | `[]` | Extra particles at set times; see `Burst` |

```rust
cmd.spawn([at(200.0, 300.0), ParticleEmitter {
    rate: 40.0, lifetime_min: 0.4, lifetime_max: 0.8, speed_min: 20.0, speed_max: 60.0,
    angle_min: -2.0, angle_max: -1.1, size_start: 6.0, size_end: 1.0,
    color_start: rgb(255, 200, 80), color_end: rgba(255, 60, 20, 0), blend: Blend::Additive, ..
}]);
```

## Burst

`count` particles at once, `time` seconds into the emitter's run, repeated `cycles` times (0 forever) `interval` seconds apart.

```rust
struct Burst {
    time: float = 0.0,
    count: int = 10,
    cycles: int = 1,
    interval: float = 0.0,
}
```

```rust
ParticleEmitter { rate: 0.0, bursts: [Burst { count: 30, cycles: 0, interval: 1.0, .. }], .. }   // 30 every second
```

## Trail

A fading ribbon behind a moving entity.

```rust
struct Trail {
    width: float = 4.0,
    lifetime: float = 0.5,
    color_start: Color = Color { .. },
    color_end: Color = Color { r: 1.0, g: 1.0, b: 1.0, a: 0.0 },
    min_distance: float = 2.0,
    max_points: int = 64,
    z: float = -1.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `width` | `float` | `4.0` | Width in pixels |
| `lifetime` | `float` | `0.5` | Seconds a point of the ribbon lasts |
| `color_start` | `Color` | `Color { .. }` | Color at the entity |
| `color_end` | `Color` | `Color { r: 1.0, g: 1.0, b: 1.0, a: 0.0 }` | Color at the tail |
| `min_distance` | `float` | `2.0` | The entity moves this far before the trail adds a point |
| `max_points` | `int` | `64` | The most points the ribbon keeps |
| `z` | `float` | `-1.0` | Draw order |

```rust
cmd.spawn([at(x, y), Circle { radius: 3.0, .. }, Velocity { linear: vec2(300.0, 0.0), .. },
           Trail { width: 4.0, lifetime: 0.3, color_start: rgb(120, 200, 255), .. }]);
```

## PointLight

Lights add to the ambient light of `[lighting]` in `drift.toml`; without that section nothing is lit or darkened. See [Particles and Effects](../guides/effects.md#lights).

```rust
struct PointLight {
    color: Color = Color { .. },
    intensity: float = 1.0,
    radius: float = 200.0,
    falloff: float = 2.0,
    shadows: bool = true,
    layer_mask: int = ALL_LAYERS,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `color` | `Color` | `Color { .. }` | Light color |
| `intensity` | `float` | `1.0` | Brightness |
| `radius` | `float` | `200.0` | How far it reaches, in pixels |
| `falloff` | `float` | `2.0` | 1 fades evenly to the edge; more keeps a tighter core |
| `shadows` | `bool` | `true` | Casts shadows from `LightOccluder` entities |
| `layer_mask` | `int` | `ALL_LAYERS` | The layers it lights |

```rust
cmd.spawn([at(400.0, 300.0), PointLight { color: rgb(255, 180, 90), intensity: 1.5, radius: 300.0, .. }]);
```

## SpotLight

A cone of light. `direction` adds to the entity's rotation.

```rust
struct SpotLight {
    color: Color = Color { .. },
    intensity: float = 1.0,
    radius: float = 300.0,
    falloff: float = 2.0,
    direction: float = 0.0,
    angle: float = 0.8,
    softness: float = 0.2,
    shadows: bool = true,
    layer_mask: int = ALL_LAYERS,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `color` | `Color` | `Color { .. }` | Light color |
| `intensity` | `float` | `1.0` | Brightness |
| `radius` | `float` | `300.0` | How far it reaches, in pixels |
| `falloff` | `float` | `2.0` | 1 fades evenly to the edge; more keeps a tighter core |
| `direction` | `float` | `0.0` | Radians from +x, added to the entity's rotation |
| `angle` | `float` | `0.8` | The whole cone, in radians |
| `softness` | `float` | `0.2` | 0 is a hard edge; 1 fades from the middle |
| `shadows` | `bool` | `true` | Casts shadows from `LightOccluder` entities |
| `layer_mask` | `int` | `ALL_LAYERS` | The layers it lights |

```rust
cmd.spawn([at(100.0, 100.0), SpotLight { radius: 450.0, direction: 0.8, angle: 0.6, softness: 0.3, .. }]);
```

## LightOccluder

Blocks light and casts a shadow from lights with `shadows`. The shape is in the entity's space and turns and scales with it.

```rust
struct LightOccluder {
    shape: OccluderShape = OccluderShape::Box,
    half_size: Vec2 = Vec2 { x: 16.0, y: 16.0 },
    radius: float = 16.0,
    points: Vec2[] = [],
    offset: Vec2 = Vec2 { .. },
    shadow_self: bool = false,
    layer_mask: int = ALL_LAYERS,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `shape` | `OccluderShape` | `OccluderShape::Box` | The occluder's shape; `FromCollider` takes the entity's collider |
| `half_size` | `Vec2` | `Vec2 { x: 16.0, y: 16.0 }` | Half size of a `Box` |
| `radius` | `float` | `16.0` | Radius of a `Circle` |
| `points` | `Vec2[]` | `[]` | Corners of a `Polygon`, up to 16 |
| `offset` | `Vec2` | `Vec2 { .. }` | Moves the shape from the entity's position |
| `shadow_self` | `bool` | `false` | `false` keeps the occluder's own area lit |
| `layer_mask` | `int` | `ALL_LAYERS` | The lights' layers it blocks |

```rust
cmd.spawn([at(300.0, 200.0), Rectangle { size: vec2(64.0, 64.0), .. }, BoxCollider { half_size: vec2(32.0, 32.0), .. },
           LightOccluder { shape: OccluderShape::FromCollider, .. }]);
```

## AudioSource

Plays a sound from the entity while it lives. With `spatial`, volume and pan follow its position relative to the AudioListener (or the main camera): full inside `min_distance`, silent past `max_distance`. The engine reads volume, pitch and looping every frame; set `play` to start the sound again.

```rust
struct AudioSource {
    sound: Sound = Sound { .. },
    bus: Bus = Bus { .. },
    volume: float = 1.0,
    pitch: float = 1.0,
    looping: bool = false,
    autoplay: bool = true,
    spatial: bool = false,
    min_distance: float = 64.0,
    max_distance: float = 1024.0,
    rolloff: Rolloff = Rolloff::Linear,
    priority: int = 0,
    play: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `sound` | `Sound` | `Sound { .. }` | The sound to play |
| `bus` | `Bus` | `Bus { .. }` | The bus it plays on; `Bus {}` is the sfx bus |
| `volume` | `float` | `1.0` | 0 to 1 |
| `pitch` | `float` | `1.0` | Playback speed; 2 is an octave up |
| `looping` | `bool` | `false` | Plays again when it ends |
| `autoplay` | `bool` | `true` | Starts when the entity is spawned |
| `spatial` | `bool` | `false` | Volume and pan follow the position relative to the listener |
| `min_distance` | `float` | `64.0` | Full volume within this distance |
| `max_distance` | `float` | `1024.0` | Silent past this distance |
| `rolloff` | `Rolloff` | `Rolloff::Linear` | How volume falls between the two |
| `priority` | `int` | `0` | Keeps its voice over lower priorities when every voice is busy |
| `play` | `bool` | `false` | Set it to start the sound again |

```rust
cmd.spawn([at(400.0, 200.0), AudioSource { sound: sfx.fire, looping: true, spatial: true, max_distance: 900.0, .. }]);
```

## AudioListener

The entity spatial sounds are heard from.

```rust
struct AudioListener {
    active: bool = true,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `active` | `bool` | `true` | Whether spatial sounds are heard from this entity |

```rust
cmd.spawn([at(0.0, 0.0), Camera { .. }, AudioListener { .. }]);
```

## Oscillate

Swings the entity back and forth around where it is: `amplitude` pixels each way, `frequency` times a second. Adds to whatever else moves it.

```rust
struct Oscillate {
    amplitude: Vec2 = Vec2 { x: 0.0, y: 8.0 },
    frequency: float = 1.0,
    phase: float = 0.0,
}
```

## Follow

Moves the entity toward `target` plus `offset` at `speed` pixels per second, stopping `distance` short. Nothing happens while the target is missing.

```rust
struct Follow {
    target: Entity = Entity { .. },
    speed: float = 100.0,
    distance: float = 0.0,
    offset: Vec2 = Vec2 { .. },
}
```

## Orbit

Circles the entity around `center`, or around `point` while `center` is unset or gone: `radius` pixels out, `speed` radians per second (negative turns the other way), from `angle`.

```rust
struct Orbit {
    center: Entity = Entity { .. },
    point: Vec2 = Vec2 { .. },
    radius: float = 64.0,
    speed: float = 1.0,
    angle: float = 0.0,
}
```
