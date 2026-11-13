<!--
Notes for the Candela reference. scripts/candela-reference.py reads this
file and the drift module (bindings/candela/drift/src/main.cdl and the
core/ files it passes on) and writes the pages in src/reference/.

- `# Title {#slug}` starts a page, written to src/reference/<slug>.md. The
  text under it, before the first entry, is the page's introduction.
- `## Name` is the note for a type, enum, function, constant or attribute;
  `## Type.method` is the note for a method. Entries appear on their page
  in the order written here; methods follow their type.
- In a note, a line `- name: text` describes a struct field or an enum
  variant. The rest of the note is the description and the example.
- Signatures, fields, defaults and variants come from the module, never
  from here. A name the module does not have is an error. A name with no
  note still appears, described by its comment in the module.
- `# Internal {#internal}` lists helpers the module needs but games do not
  call; they stay off the pages.
-->

# Attributes {#attributes}

Attributes tell Drift what your structs, enums and functions are. Write one in front of a top-level declaration. They come with `import "drift" { * };`; a game that imports the module by name writes `@drift::component` instead. The compiler checks each attribute and its arguments, so a misspelt attribute, an unknown type in `requires:` or an unknown function in `after:` is an error with its line, before the game starts.

A struct or function with type parameters cannot carry an attribute, and neither can a method.

## @component

The struct is a component: data attached to entities. Drift stores it with the entity, passes it to the systems that ask for it, and shows it in the inspector. `requires:` lists components added, with their defaults, to an entity that gets this one without them. See [Components](../concepts/components.md).

```rust
@component(requires: [Velocity])
struct Bird { flap: float = 420.0 }

@component
struct Enemy {}          // a tag: no data, it only marks entities
```

## @resource

The struct is a resource: one value for the whole game. A resource whose fields all have defaults exists from the start; one with a required field exists once a system inserts it with `cmd.insert_resource`. A system that takes a resource that does not exist is skipped. See [Resources](../concepts/resources.md).

```rust
@resource
struct Score { value: int = 0, best: int = 0 }
```

## @settings

A resource filled from one section of `drift.toml`. Field names are the keys, and every field needs a default for when the file leaves the key out. See [Settings](../concepts/settings.md).

```rust
@settings(section: "rules")
struct Rules { lives: int = 3, enemy_speed: float = 120.0 }
```

```toml
[rules]
lives = 5
```

## @message

The struct is a message: something that happened this frame. Systems send it with `cmd.send(...)` and read it as a parameter. See [Events](../concepts/events.md).

```rust
@message
struct Damage { target: Entity, amount: int }
```

## @state

The enum is a game state. Its first variant is the starting state. Systems read and change it through `State<Phase>` and run in some variants with `in_state:`, `on_enter:` and `on_exit:`. See [States](../concepts/states.md).

```rust
@state
enum Phase { Title, Playing, GameOver }
```

## @query

The struct is one row of a query. Each field is a component, or `Entity` for the entity's id. A parameter of type `Mover[]` receives one row per entity that has every field's component, every type in `with:` and none in `without:`. See [Queries](../concepts/queries.md).

```rust
@query(with: [Enemy], without: [Frozen])
struct Mover { entity: Entity, transform: Transform, velocity: Velocity }

@system
fn slow_down(movers: Mover[]) {
    for m in movers {
        m.velocity.linear = m.velocity.linear * 0.9;
    }
}
```

## @system

The function is a system. Drift calls it every frame (once, for `Schedule::Startup`) and fills its parameters with the data their types name. See [Systems](../concepts/systems.md) and [System Parameters](system-parameters.md).

| Argument | Meaning |
|----------|---------|
| `after: [a, b]` | Runs after these systems in the same frame |
| `before: [c]` | Runs before these systems |
| `run_if: [f]` | Runs only in frames where every function returns `true` |
| `in_state: [Phase::Playing]` | Runs only while a `@state` enum is one of these variants |
| `on_enter: Phase::Over` | Runs once when the state becomes this variant, and not otherwise |
| `on_exit: Phase::Over` | Runs once when the state stops being this variant, and not otherwise |
| `schedule: Schedule::Startup` | Runs once before the first frame instead of every frame |
| `enabled: false` | Registers the system without running it |

Systems with no order between them run in the order they appear in the source.

```rust
fn jumped(input: Input) -> bool => input.action_pressed("jump");

@system(in_state: [Phase::Playing], run_if: [jumped], after: [land])
fn jump(row: (Player, Velocity)) {
    let (player, v) = row;
    v.linear = vec2(v.linear.x, -player.jump_speed);
}
```

# System Parameters {#system-parameters}

A system's parameter types say what it receives. Drift reads them when the game loads and fills each one, in any order. Every parameter needs its type written out.

| Parameter type | Receives |
|----------------|----------|
| `(A, B)`, a tuple of components (a row) | One entity's components; the system runs once per entity that has every component in it. `Entity` in a place gives the entity. Take it apart with `let (a, b) = row;`, `_` skipping a place. At most one per system |
| `(A, B)[]`, a list of tuples | One tuple per entity with every component; `Entity` in a position gives the entity's id |
| `Row[]`, where `Row` is a `@query` struct | One row per matching entity |
| [`Lookup<T>`](#lookup) | Every `T`, found by entity |
| A `@resource` or `@settings` struct | The resource. The system is skipped while it does not exist |
| [`State<S>`](#state) | The current variant of a `@state` enum, to read and change |
| `M` or `M[]`, where `M` is a `@message` struct | One message per call, or every message sent since the system last ran |
| [`Contact<A, B>`](#contact), [`ContactEnd`](#contactend), [`Overlap`](#overlap), [`OverlapEnd`](#overlapend), alone or as a list | Collisions between an entity with `A` and one with `B`; alone, the system runs once per collision |
| [`Commands`](#commands) | Spawning, inserting, removing and despawning, applied when the call returns |
| An engine resource: `Time`, `Input`, `Window`, `Assets`, `Effects`, `Save`, `Random`, `Camera2D`, `Draw`, `Physics` | See [Engine Resources](resources.md) |

A component or an `Entity` on its own is not a parameter: a system that runs once per entity takes them together as one row, and a row of one component pairs it with `Entity`, as in `(Entity, Transform)`. A system takes at most one row, and not both a row and a single event: it runs once per entity or once per event. Each of these is an error when the game loads; see [Errors](errors.md).

Drift keeps every change a system makes to the components and resources it receives, through a row, a list of rows, a resource or a lookup. The names a row is taken apart into share its values, so setting their fields changes the entity. The lists are built when the call starts, so spawning and despawning while looping over one is safe. Rows come in the order entities were spawned, the same on every run.

## Commands

Changes to the world's shape: making and removing entities, adding and removing components, inserting resources and sending messages. Drift applies them when the system returns, in the order recorded, before the next system starts. See [Components](../concepts/components.md#making-entities).

```rust
@system(schedule: Schedule::Startup)
fn setup(cmd: Commands) {
    let player = cmd.spawn([at(100.0, 100.0), Rectangle { .. }, Player { .. }]);
    cmd.insert(player, Velocity { linear: vec2(50.0, 0.0), .. });
}
```

## Commands.spawn

Makes an entity with the components in the list, at most one of each type, and returns its id. The id is valid at once: use it in later commands of the same system.

```rust
let e = cmd.spawn([at(320.0, 180.0), Circle { radius: 8.0, .. }, Coin {}]);
```

## Commands.insert

Adds a component to an entity, or replaces the one of its type.

```rust
cmd.insert(hit.b, Stunned { seconds: 2.0 });
```

## Commands.remove

Takes a component off an entity. Name the type in angle brackets. Nothing happens if the entity does not have one.

```rust
cmd.remove<Stunned>(e);
```

## Commands.despawn

Removes an entity, and its children. Despawning an entity that is already gone does nothing.

```rust
cmd.despawn(hit.b);
```

## Commands.insert_resource

Adds a resource, or replaces the one of its type. Systems that take it start running.

```rust
cmd.insert_resource(Level { map: map, number: 1 });
```

## Commands.remove_resource

Removes a resource. Systems that take it stop running until it is inserted again.

```rust
cmd.remove_resource<Boss>();
```

## Commands.send

Sends a message to the systems that read its type. See [Events](../concepts/events.md).

```rust
cmd.send(Damage { target: hit.b, amount: 10 });
```

## Commands.spawn_child

```rust
let ship = cmd.spawn([at(200.0, 200.0), Sprite { image: art.ship, .. }]);
cmd.spawn_child(ship, [at(0.0, -20.0), Sprite { image: art.turret, .. }]);
```

## Commands.set_parent

```rust
cmd.set_parent(sword, hand);
```

## Commands.set_parent_in_place

```rust
cmd.set_parent_in_place(crate_entity, platform);   // rides along from where it is
```

## Commands.detach

```rust
cmd.detach(sword);   // drops it where it is
```


## Commands.restart

```rust
if input.key_pressed(Key::F5) { cmd.restart(); }
```

## Lookup

Every component of type `T`, found by entity id. A query walks every match; a lookup answers for one entity, such as the other entity in a collision.

```rust
@system
fn hurt(hit: Contact<Bullet, Enemy>, health: Lookup<Health>) {
    if health.get(hit.b) is Some(h) {
        h.hp -= 1;            // a change through a lookup is kept
    }
}
```

## Lookup.get

`Some(component)` when the entity has a `T`, `None` otherwise.

```rust
if places.get(hit.b) is Some(t) {
    effects.burst(t.position, ParticleEmitter { .. }, 20);
}
```

## Lookup.has

Whether the entity has a `T`.

```rust
if shields.has(hit.a) { return; }
```

## Lookup.len

How many entities have a `T`.

```rust
log(string(enemies.len()) + " enemies left");
```

## State

The current variant of a `@state` enum `S`. `now` is the variant and `since` the seconds since it last changed. `next` and `changing` belong to `set`. See [States](../concepts/states.md).

- now: The current variant
- since: Seconds since the state last changed
- next: The variant `set` asked for
- changing: Whether `set` was called this frame

```rust
@system(in_state: [Phase::Over])
fn restart(input: Input, phase: State<Phase>) {
    if phase.since > 1.0 && input.action_pressed("jump") {
        phase.set(Phase::Playing);
    }
}
```

## State.set

Asks for a change of state. It takes effect after this frame's `Update` systems: the `on_exit` systems of the old variant run, then the `on_enter` systems of the new one. When several systems ask in one frame, the last one to run wins; asking for the current variant changes nothing.

```rust
phase.set(Phase::GameOver);
```

## Contact

Two solid colliders started touching: one entity has an `A`, the other a `B`. `a` is the entity with `A` and `b` the one with `B`. Collisions reach systems the frame after the physics step that found them.

- a: The entity with an `A`
- b: The entity with a `B`
- point: The first contact point, in the world
- normal: The contact direction, pointing from `a` to `b`

```rust
@system
fn land(hit: Contact<Player, Ground>, players: Lookup<Player>) {
    if hit.normal.y > 0.5 {
        if players.get(hit.a) is Some(p) {
            p.on_ground = true;
        }
    }
}
```

## ContactEnd

Two solid colliders stopped touching.

- a: The entity with an `A`
- b: The entity with a `B`

```rust
@system
fn leave(hit: ContactEnd<Player, Ground>, players: Lookup<Player>) {
    if players.get(hit.a) is Some(p) {
        p.on_ground = false;
    }
}
```

## Overlap

An entity entered a sensor collider (one with `sensor: true`), whichever of the two is the sensor. Sensors detect without pushing: pickups, trigger zones, checkpoints.

- a: The entity with an `A`
- b: The entity with a `B`

```rust
@system
fn collect(hit: Overlap<Player, Coin>, cmd: Commands, score: Score) {
    score.value += 1;
    cmd.despawn(hit.b);
}
```

## OverlapEnd

An entity left a sensor collider.

- a: The entity with an `A`
- b: The entity with a `B`

```rust
@system
fn leave_water(hit: OverlapEnd<Player, Water>, players: Lookup<Player>) {
    if players.get(hit.a) is Some(p) {
        p.swimming = false;
    }
}
```

# Engine Components {#components}

Components the engine draws and simulates. Use them like your own: put them in a spawn bundle, take them as parameters, look them up, and assign to their fields to change them. Build each with the fields you need and `..` for the rest.

The drawn, moved and simulated components need a `Transform`; give the entity one with `at(x, y)` or `Transform { .. }`. Some fields are also changed by the engine as it runs (physics moves `Transform`, an emitter turns `playing` off when it finishes); a system sees the value from when it started.

## Transform

Where the entity is, relative to its parent when it has one. Physics, `Velocity`, tweens and camera followers move it.

- position: Position in pixels; +Y points down
- rotation: Turn in radians, clockwise on screen
- scale: Size multiplier

```rust
cmd.spawn([Transform { position: vec2(100.0, 50.0), rotation: 0.5, .. }, Sprite { image: art.ship, .. }]);
```

## at

The `Transform` at a position, the shortest way to place an entity.

```rust
cmd.spawn([at(480.0, 270.0), Circle { radius: 10.0, .. }]);
```

## GlobalTransform

- position: Position in the world
- rotation: Turn in radians in the world
- scale: Size multiplier in the world

```rust
@system
fn aim(turret: (Turret, GlobalTransform), draw: Draw) {
    let (_, g) = turret;
    draw.line(g.position, g.position + from_angle(g.rotation) * 40.0, rgb(255, 80, 80), 2.0);
}
```

## Parent

- entity: The parent; no entity detaches
- keep_world: Rewrite the child's Transform when linking, so it stays where it is

```rust
cmd.spawn([at(12.0, 0.0), Sprite { image: art.gun, .. }, Parent { entity: ship, .. }]);
```

## Name

A name for an entity, shown in the inspector.

- value: The name

```rust
cmd.spawn([at(0.0, 0.0), Name { value: "Boss" }, Boss { .. }]);
```

## Sprite

Draws a texture at the entity's position. Load the texture with [`texture`](assets.md#texture).

- image: The texture to draw
- frame: The part of the image to draw, in pixels; a zero-size rect draws all of it
- anchor: The point of the image placed at the position, from (0, 0) top-left to (1, 1) bottom-right; it turns and scales around this point
- color: Tint, multiplied with the image's colors
- flip_x: Mirrors the image left to right
- flip_y: Mirrors the image top to bottom
- z: Draw order within the layer; higher draws on top
- layer: Render layer, 0 to 30 for the world, `hud_layer()` for the screen
- visible: `false` hides it without removing it
- blend: How it mixes with what is behind it

```rust
cmd.spawn([at(160.0, 90.0), Sprite { image: texture("assets/ship.png"), anchor: vec2(0.5, 1.0), .. }]);
```

## Rectangle

Draws a filled rectangle, or its outline.

- size: Width and height in pixels
- color: Fill or line color
- outline: Line width; 0 fills the rectangle
- corner_radius: Rounds the corners
- anchor: The point placed at the position, from (0, 0) top-left to (1, 1) bottom-right
- z: Draw order within the layer
- layer: Render layer
- visible: `false` hides it

```rust
cmd.spawn([at(320.0, 340.0), Rectangle { size: vec2(640.0, 20.0), color: rgb(70, 70, 90), .. }]);
```

## Circle

Draws a filled circle, or its outline.

- radius: Radius in pixels
- color: Fill or line color
- outline: Line width; 0 fills the circle
- z: Draw order within the layer
- layer: Render layer
- visible: `false` hides it

```rust
cmd.spawn([at(100.0, 100.0), Circle { radius: 20.0, color: rgb(255, 200, 60), .. }]);
```

## Text

Draws a string at the entity's position.

- value: The string
- size: Pixel height; raise it rather than scaling the Transform, so the glyphs stay sharp
- color: Text color
- align: Which point of the text sits on the position, left to right
- valign: Which point of the text sits on the position, top to bottom
- wrap: Wraps lines at this width in pixels; 0 does not wrap
- line_spacing: Multiplies the distance between lines
- outline: Outline width around each glyph; 0 for none
- outline_color: Outline color
- shadow_offset: Drop shadow offset; zero for none
- shadow_color: Drop shadow color
- rich_text: Reads `[color=#rrggbb]...[/color]` tags in `value`
- font: The font; `Font {}` is the built-in one
- z: Draw order within the layer
- layer: Render layer; `hud_layer()` places it in screen pixels
- visible: `false` hides it

```rust
cmd.spawn([at(16.0, 12.0), Text { value: "Score 0", size: 28.0, outline: 2.0, layer: hud_layer(), .. }, ScoreLabel {}]);
```

## Camera

- zoom: Magnification; 2.0 shows the world twice as large
- rotation: Turn of the view in radians
- active: `false` stops this camera drawing
- viewport: The part of the window it draws to, from (0, 0) to (1, 1)
- order: Cameras draw in ascending order
- layer_mask: The layers it draws; see `layer_mask()`
- target: A render target to draw into; `Texture {}` draws to the window
- clear: Clears its target or viewport before drawing
- clear_color: The color it clears to

Without a camera, world position (0, 0) is the top-left of the screen and one world unit is one logical pixel. A camera centers its view on its entity's position. See [Cameras](../guides/cameras.md).

```rust
cmd.spawn([at(480.0, 270.0), Camera { zoom: 2.0, .. }]);
```

## CameraFollow

- target: The entity to follow
- smoothing: How quickly it catches up; higher is snappier
- offset: Aims this far from the target
- dead_zone: Half size of the box the target moves in without moving the camera

```rust
cmd.spawn([at(0.0, 0.0), Camera { .. }, CameraFollow { target: player, smoothing: 6.0, dead_zone: vec2(40.0, 24.0), .. }]);
```

## CameraBounds

- area: The world rect the camera's view stays inside

```rust
cmd.insert(camera, CameraBounds { area: rect(0.0, 0.0, 3200.0, 720.0) });
```

## Tilemap

Shows a [`Map`](assets.md#map) in the world.

- map: The map to show
- z: Draw order within the layer
- layer: Render layer
- colliders: Builds static colliders from the map's solid layers
- visible: `false` hides it

```rust
let map = assets.tilemap("assets/level1.tmj");
cmd.spawn([at(0.0, 0.0), Tilemap { map: map, .. }]);
```

## Velocity

Moves the entity every frame. On an entity with a dynamic `RigidBody` it is the body's velocity: read it to see how fast the body moves, write it to set its speed.

- linear: Pixels per second
- angular: Radians per second

```rust
cmd.spawn([at(0.0, 80.0), Sprite { image: art.cloud, .. }, Velocity { linear: vec2(12.0, 0.0), .. }]);
```

## RigidBody

Makes the entity a physics body. Give it a collider, and a `Velocity`: a moving body shows its motion through the entity's `Velocity`, and without one it stays where it was drawn. The body is made at the end of the frame its components are added; changing a field later makes it again. See [Collisions and Physics](../guides/collisions.md).

- kind: `Dynamic` falls and is pushed, `Kinematic` moves only by its Velocity, `Static` never moves
- fixed_rotation: Keeps it upright, for characters
- gravity_scale: Multiplies world gravity for this body
- linear_damping: Slows its motion over time
- angular_damping: Slows its spin over time

```rust
cmd.spawn([at(200.0, 50.0), Rectangle { .. }, RigidBody { fixed_rotation: true, .. }, BoxCollider { .. }, Velocity { .. }]);
```

## BoxCollider

A box-shaped collider. Without a `RigidBody` it is static.

- half_size: Half the width and height, in pixels
- offset: Moves the box from the entity's position
- sensor: Detects overlaps without pushing anything
- density: Mass per area
- friction: 0 is ice
- restitution: Bounciness; 1 keeps all the speed
- category: The bits this collider is
- mask: The categories it collides with; two colliders touch when each one's category is in the other's mask

```rust
cmd.spawn([at(320.0, 470.0), BoxCollider { half_size: vec2(320.0, 10.0), .. }]);   // a floor
```

## CircleCollider

A circle-shaped collider. Without a `RigidBody` it is static.

- radius: Radius in pixels
- offset: Moves the circle from the entity's position
- sensor: Detects overlaps without pushing anything
- density: Mass per area
- friction: 0 is ice
- restitution: Bounciness; 1 keeps all the speed
- category: The bits this collider is
- mask: The categories it collides with

```rust
cmd.spawn([at(x, y), Circle { radius: 8.0, .. }, CircleCollider { radius: 8.0, sensor: true, .. }, Coin {}]);
```

## Joint

- kind: The kind of joint
- target: The other body; no entity pins this one to the world
- anchor: Where it attaches, in this body's local space
- target_anchor: Where it attaches on the target, or a world point without a target

```rust
// A pendulum: a ball hanging from a point in the world.
cmd.spawn([at(300.0, 200.0), Circle { radius: 12.0, .. }, RigidBody { .. }, CircleCollider { radius: 12.0, .. },
           Velocity { .. }, Joint { kind: JointKind::Distance, target_anchor: vec2(300.0, 80.0), .. }]);
```

## Lifetime

Despawns the entity, with its children, when the time runs out. Use it for bullets, floating labels and debris.

- seconds: Seconds left

```rust
cmd.spawn([at(x, y), Text { value: "+1", .. }, Velocity { linear: vec2(0.0, -60.0), .. }, Lifetime { seconds: 0.6 }]);
```

## Tween

Animates the entity from where it is to a target, over time, along an easing curve. Build one with `tween()` and chain steps; they run one after another. An entity holds one tween; inserting a changed one starts it again.

- steps: The steps, built by the methods below
- loops: How many times to play the steps again; -1 forever
- mirror: Plays back and forth instead of from the start

```rust
cmd.spawn([at(x, y), Sprite { image: art.star, .. },
           tween().scale_to(vec2(1.4, 1.4), 0.1, Ease::OutQuad).scale_to(vec2(1.0, 1.0), 0.3, Ease::OutBack)]);
```

## tween

An empty `Tween` to chain steps onto.

```rust
let bob = tween().move_by(vec2(0.0, -6.0), 0.5, Ease::InOutSine).yoyo(-1);
```

## Tween.move_to

Moves to a position.

```rust
tween().move_to(vec2(400.0, 100.0), 1.0, Ease::OutCubic)
```

## Tween.move_by

Moves by an offset from where the step starts.

```rust
tween().move_by(vec2(0.0, -20.0), 0.3, Ease::OutQuad)
```

## Tween.scale_to

Scales to a size multiplier.

```rust
tween().scale_to(vec2(2.0, 2.0), 0.2, Ease::OutBack)
```

## Tween.rotate_to

Turns to an angle in radians.

```rust
tween().rotate_to(PI, 0.5, Ease::InOutSine)
```

## Tween.rotate_by

Turns by an angle in radians.

```rust
tween().rotate_by(TAU, 1.0, Ease::Linear).repeat(-1)   // spins forever
```

## Tween.fade_to

```rust
tween().delay(1.0).fade_to(0.0, 0.5, Ease::InQuad)
```

## Tween.zoom_to

```rust
cmd.insert(camera, tween().zoom_to(2.0, 0.8, Ease::InOutCubic));
```

## Tween.tint_to

Changes the sprite's color.

```rust
tween().tint_to(rgb(255, 80, 80), 0.1, Ease::Linear).tint_to(rgb(255, 255, 255), 0.3, Ease::OutQuad)
```

## Tween.delay

Waits before the next step.

```rust
tween().delay(0.5).move_by(vec2(0.0, 40.0), 0.3, Ease::OutBounce)
```

## Tween.repeat

Plays the whole chain again `times` more times from the start; -1 repeats forever.

```rust
tween().rotate_by(TAU, 2.0, Ease::Linear).repeat(-1)
```

## Tween.yoyo

Plays the chain forward, then backward, `times` more times; -1 forever.

```rust
tween().scale_to(vec2(1.2, 1.2), 0.4, Ease::InOutSine).yoyo(-1)   // a pulse
```

## ParticleEmitter

Emits particles from its entity for as long as `playing` is on. A one-off burst does not need an entity: see [`Effects.burst`](resources.md#effectsburst). See [Particles and Effects](../guides/effects.md).

- rate: Particles per second
- lifetime_min: Shortest life in seconds
- lifetime_max: Longest life in seconds
- speed_min: Slowest start speed in pixels per second
- speed_max: Fastest start speed
- angle_min: Smallest start direction, in radians from +x; -PI/2 is up
- angle_max: Largest start direction
- size_start: Size at birth, in pixels
- size_end: Size at death
- color_start: Color at birth
- color_end: Color at death; an alpha of 0 fades them out
- gravity: Acceleration in pixels per second squared
- drag: Slows particles; 0 for none
- max_particles: How many may be alive at once
- blend: `Blend::Additive` makes overlapping particles glow
- texture: An image for each particle; `Texture {}` draws soft squares
- shape: Where particles start
- shape_radius: Radius for `Circle` and `Ring` shapes
- shape_extents: Half size for `Box`, direction for `Line`
- duration: Seconds the emitter runs; -1 forever
- looping: Starts over when `duration` runs out
- z: Draw order
- playing: Whether it emits; write it to switch the emitter on and off
- frame: Part of `texture` to draw
- size_start_max: With it above `size_start`, birth sizes vary between the two
- size_end_max: The same for the size at death
- bursts: Extra particles at set times; see `Burst`

```rust
cmd.spawn([at(200.0, 300.0), ParticleEmitter {
    rate: 40.0, lifetime_min: 0.4, lifetime_max: 0.8, speed_min: 20.0, speed_max: 60.0,
    angle_min: -2.0, angle_max: -1.1, size_start: 6.0, size_end: 1.0,
    color_start: rgb(255, 200, 80), color_end: rgba(255, 60, 20, 0), blend: Blend::Additive, ..
}]);
```

## Burst

```rust
ParticleEmitter { rate: 0.0, bursts: [Burst { count: 30, cycles: 0, interval: 1.0, .. }], .. }   // 30 every second
```


## Trail

- width: Width in pixels
- lifetime: Seconds a point of the ribbon lasts
- color_start: Color at the entity
- color_end: Color at the tail
- min_distance: The entity moves this far before the trail adds a point
- max_points: The most points the ribbon keeps
- z: Draw order

```rust
cmd.spawn([at(x, y), Circle { radius: 3.0, .. }, Velocity { linear: vec2(300.0, 0.0), .. },
           Trail { width: 4.0, lifetime: 0.3, color_start: rgb(120, 200, 255), .. }]);
```

## PointLight

Lights add to the ambient light of `[lighting]` in `drift.toml`; without that section nothing is lit or darkened. See [Particles and Effects](../guides/effects.md#lights).

- color: Light color
- intensity: Brightness
- radius: How far it reaches, in pixels
- falloff: 1 fades evenly to the edge; more keeps a tighter core
- shadows: Casts shadows from `LightOccluder` entities
- layer_mask: The layers it lights

```rust
cmd.spawn([at(400.0, 300.0), PointLight { color: rgb(255, 180, 90), intensity: 1.5, radius: 300.0, .. }]);
```

## SpotLight

- color: Light color
- intensity: Brightness
- radius: How far it reaches, in pixels
- falloff: 1 fades evenly to the edge; more keeps a tighter core
- direction: Radians from +x, added to the entity's rotation
- angle: The whole cone, in radians
- softness: 0 is a hard edge; 1 fades from the middle
- shadows: Casts shadows from `LightOccluder` entities
- layer_mask: The layers it lights

```rust
cmd.spawn([at(100.0, 100.0), SpotLight { radius: 450.0, direction: 0.8, angle: 0.6, softness: 0.3, .. }]);
```

## LightOccluder

- shape: The occluder's shape; `FromCollider` takes the entity's collider
- half_size: Half size of a `Box`
- radius: Radius of a `Circle`
- points: Corners of a `Polygon`, up to 16
- offset: Moves the shape from the entity's position
- shadow_self: `false` keeps the occluder's own area lit
- layer_mask: The lights' layers it blocks

```rust
cmd.spawn([at(300.0, 200.0), Rectangle { size: vec2(64.0, 64.0), .. }, BoxCollider { half_size: vec2(32.0, 32.0), .. },
           LightOccluder { shape: OccluderShape::FromCollider, .. }]);
```

## AudioSource

- sound: The sound to play
- bus: The bus it plays on; `Bus {}` is the sfx bus
- volume: 0 to 1
- pitch: Playback speed; 2 is an octave up
- looping: Plays again when it ends
- autoplay: Starts when the entity is spawned
- spatial: Volume and pan follow the position relative to the listener
- min_distance: Full volume within this distance
- max_distance: Silent past this distance
- rolloff: How volume falls between the two
- priority: Keeps its voice over lower priorities when every voice is busy
- play: Set it to start the sound again

```rust
cmd.spawn([at(400.0, 200.0), AudioSource { sound: sfx.fire, looping: true, spatial: true, max_distance: 900.0, .. }]);
```

## AudioListener

- active: Whether spatial sounds are heard from this entity

```rust
cmd.spawn([at(0.0, 0.0), Camera { .. }, AudioListener { .. }]);
```

# Engine Resources {#resources}

The engine's resources reach a system as parameters, the same way as your own. Their fields are this frame's values, and their methods act on the engine at once.

## Time

- dt: Seconds since the last frame, times `scale`; multiply speeds by it
- elapsed: Seconds since the game started
- frame: Frames so far
- scale: The game's speed: 1 normal, 0.5 half, 0 frozen
- real_dt: Seconds since the last frame, ignoring `scale`

```rust
@system
fn spin(fan: (Fan, Transform), time: Time) {
    let (_, t) = fan;
    t.rotation += 3.0 * time.dt;
}
```


## Time.set_scale

```rust
time.set_scale(0.25);   // slow motion
```

## Input

The keyboard, mouse, gamepads, touch and input actions. An action is a name, such as `"jump"`, that any mix of keys, buttons and sticks can drive; write game logic against actions. Bind actions in the `[input]` section of `drift.toml` or from code. See [Input](../guides/input.md).

```rust
@system
fn controls(input: Input, window: Window) {
    if input.action_pressed("pause") {
        log("paused at " + string(input.mouse_world().x));
    }
}
```

## Input.key_down

Whether the key is held.

```rust
if input.key_down(Key::LShift) { speed = speed * 2.0; }
```

## Input.key_pressed

Whether the key went down this frame.

```rust
if input.key_pressed(Key::F1) { show_help = !show_help; }
```

## Input.key_released

Whether the key went up this frame.

```rust
if input.key_released(Key::Space) { charge = 0.0; }
```

## Input.mouse_down

Whether the mouse button is held.

```rust
if input.mouse_down(MouseButton::Left) { paint(input.mouse_world()); }
```

## Input.mouse_pressed

Whether the mouse button went down this frame.

```rust
if input.mouse_pressed(MouseButton::Right) { place_flag(input.mouse_world()); }
```

## Input.mouse_released

Whether the mouse button went up this frame.

```rust
if input.mouse_released(MouseButton::Left) { drop_item(); }
```

## Input.mouse_position

The mouse in logical screen pixels, letterbox bars accounted for.

```rust
let p = input.mouse_position();
```

## Input.mouse_world

The mouse in world coordinates, through the main camera.

```rust
let aim = (input.mouse_world() - t.position).normalized();
```

## Input.mouse_delta

How far the mouse moved this frame.

```rust
if input.mouse_delta().length() > 0.0 { show_cursor = true; }
```

## Input.mouse_wheel

```rust
camera.zoom = clamp(camera.zoom + input.mouse_wheel() * 0.1, 0.5, 3.0);
```

## Input.gamepad_down

Whether a gamepad button is held. Pads are numbered 0 to 3 and use the Xbox layout: `South` is A.

```rust
if input.gamepad_down(0, GamepadButton::South) { charge += time.dt; }
```

## Input.gamepad_pressed

Whether a gamepad button went down this frame.

```rust
if input.gamepad_pressed(0, GamepadButton::Start) { phase.set(Phase::Paused); }
```

## Input.gamepad_released

Whether a gamepad button went up this frame.

```rust
if input.gamepad_released(0, GamepadButton::South) { jump_held = false; }
```

## Input.gamepad_axis

A stick axis from -1 to 1 (Y down), or a trigger from 0 to 1.

```rust
let x = input.gamepad_axis(0, GamepadAxis::LeftX);
```

## Input.gamepad_connected

Whether pad `pad` is connected.

```rust
if !input.gamepad_connected(0) { hint.value = "Plug in a gamepad or use the keyboard"; }
```

## Input.bind_key

Adds a key to an action.

```rust
input.bind_key("jump", Key::Space);
```

## Input.bind_mouse

Adds a mouse button to an action.

```rust
input.bind_mouse("fire", MouseButton::Left);
```

## Input.bind_button

Adds a gamepad button to an action.

```rust
input.bind_button("jump", GamepadButton::South);
```

## Input.bind

```rust
input.bind("jump", "key:W");
input.bind("throttle", "axis:+RightTrigger");
```

## Input.bind_axis

```rust
input.bind_axis("steer", Key::Left, Key::Right);
```

## Input.bind_axis2d

A 2D axis from four keys; up is -Y, and diagonal key input has length 1. Calling it again for the same name adds another set of keys.

```rust
input.bind_axis2d("move", Key::A, Key::D, Key::W, Key::S);
input.bind_axis2d("move", Key::Left, Key::Right, Key::Up, Key::Down);
```

## Input.bind_stick

A gamepad stick as a 2D axis.

```rust
input.bind_stick("move", GamepadAxis::LeftX, GamepadAxis::LeftY);
```

## Input.action_pressed

Whether the action went down this frame.

```rust
fn jumped(input: Input) -> bool => input.action_pressed("jump");
```

## Input.action_held

Whether the action is held.

```rust
emitter.playing = input.action_held("thrust");
```

## Input.action_released

Whether the action went up this frame.

```rust
if input.action_released("jump") && v.linear.y < 0.0 { v.linear = vec2(v.linear.x, v.linear.y * 0.5); }
```

## Input.action_value

The action's analog value from 0 to 1, for triggers.

```rust
let throttle = input.action_value("accelerate");
```

## Input.axis

A 1D axis bound with `bind_axis`, from -1 to 1.

```rust
v.angular = input.axis("steer") * 3.0;
```

## Input.axis2d

A 2D axis bound with `bind_axis2d` or `bind_stick`.

```rust
v.linear = input.axis2d("move") * 200.0;
```

## Input.bindings

```rust
let shown = input.bindings("jump", 0).join(", ");   // "key:Space, button:South"
```

## Input.rebind

```rust
input.rebind("jump", 0, 0, "key:W");
```

## Input.capture_binding

```rust
let pressed = input.capture_binding();
if pressed != "" {
    input.rebind(menu.waiting_for, 0, 0, pressed);
}
```

## Input.set_action_tuning

```rust
input.set_action_tuning(0.2, 0.5, -1);
```

## Input.bindings_json

Every binding as JSON text, to keep in save data.

```rust
save.set_string("bindings", input.bindings_json());
save.flush();
```

## Input.load_bindings_json

```rust
input.load_bindings_json(save.get_string("bindings", ""));
```

## Input.send_key

```rust
input.send_key(Key::Right, true);   // a demo holds Right from next frame
```

## Input.send_mouse_move

Moves the mouse, as if from the device, next frame.

```rust
input.send_mouse_move(vec2(100.0, 100.0));
```

## Input.send_mouse_button

Presses or releases a mouse button at a position, as if from the device, next frame.

```rust
input.send_mouse_button(vec2(100.0, 100.0), MouseButton::Left, true);
```

## Input.start_text_input

```rust
input.start_text_input();
```

## Input.stop_text_input

Turns text input off.

```rust
input.stop_text_input();
```

## Input.text

The text typed this frame while text input is on.

```rust
form.name = form.name + input.text();
```

## Input.touches

```rust
for t in input.touches() {
    if t.pressed {
        effects.burst(t.position, ParticleEmitter { .. }, 10);
    }
}
```

## Touch

- id: Stays the same while the finger is down
- position: Where the finger is, in logical screen pixels
- start: Where it went down
- delta: How far it moved this frame
- pressed: It went down this frame
- released: It went up this frame; the touch is gone next frame

```rust
for t in input.touches() {
    if t.released && (t.position - t.start).length() > 50.0 {
        log("swipe");
    }
}
```

## Window

The game's window, or the canvas on a web page.

- width: Logical width in pixels
- height: Logical height in pixels

```rust
@system
fn center(title: (Title, Transform), window: Window) {
    let (_, t) = title;
    t.position = vec2(window.width * 0.5, 80.0);
}
```

## Window.quit

Ends the game after this frame. In the browser the page stops running the game.

```rust
if input.key_pressed(Key::Escape) { window.quit(); }
```

## Window.screenshot

```rust
if input.key_pressed(Key::F12) {
    window.screenshot("shot.png");
    window.download("shot.png");
}
```

## Window.download

```rust
window.download("shot.png");
```

## Assets

Makes textures, sounds, images and maps from data, and loads the kinds of file that `texture(path)` and `sound(path)` do not. Loading the same path again returns the same asset. See [Assets](../concepts/assets.md).

```rust
@system(schedule: Schedule::Startup)
fn load(cmd: Commands, assets: Assets) {
    let map = assets.tilemap("assets/level1.tmj");
    cmd.spawn([at(0.0, 0.0), Tilemap { map: map, .. }]);
}
```

## Assets.create_texture

A texture from colors, row by row from the top-left.

```rust
let pixels: Color[] = [];
for i in range(16 * 16) {
    pixels.push(rgb(255, 255, 255));
}
let white = assets.create_texture(16, 16, pixels);
```

## Assets.create_sound

A mono sound from samples between -1 and 1 at `rate` samples per second, for blips made in code.

```rust
let samples: float[] = [];
for i in range(2205) {
    samples.push(sin(float(i) * 0.12) * 0.3);
}
let beep = assets.create_sound(samples, 22050);
```

## Assets.tilemap

```rust
let map = assets.tilemap("assets/level1.tmj");
```

## Assets.ldtk_level

One level of an LDtk project, by its identifier.

```rust
let map = assets.ldtk_level("assets/world.ldtk", "Level_2");
```

## Assets.create_tilemap

```rust
let map = assets.create_tilemap(40, 25, 16, art.tiles, ["Ground", "Walls"]);
```

## Assets.render_target

```rust
let minimap = assets.render_target(256, 160);
cmd.spawn([at(0.0, 0.0), Camera { zoom: 0.2, order: -1, target: minimap, clear: true, .. }]);
cmd.spawn([at(1140.0, 100.0), Sprite { image: minimap, layer: hud_layer(), .. }]);
```

## Assets.create_image

An image of `w` by `h` pixels filled with one color, to draw into from code.

```rust
let canvas = assets.create_image(64, 64, rgba(0, 0, 0, 0));
```

## Assets.image

```rust
let img = assets.image("assets/hero.png");
img.flip_x();
let mirrored = img.to_texture();
```

## Assets.spritesheet

Cuts a texture into a grid of frames `frame_w` by `frame_h` pixels.

```rust
let sheet = assets.spritesheet(texture("assets/hero.png"), 16.0, 16.0);
cmd.spawn([at(100.0, 100.0), Sprite { image: sheet.texture, frame: sheet.frame(0, 0), .. }]);
```

## Effects

Screen flashes and shakes, particle bursts and emitter control. See [Particles and Effects](../guides/effects.md).

```rust
@system
fn crash(_: Contact<Player, Rock>, effects: Effects) {
    effects.flash(rgb(255, 60, 60), 0.3);
}
```

## Effects.flash

Covers the screen in a color that fades back to the picture over `seconds`.

```rust
effects.flash(rgb(255, 255, 255), 0.2);
```

## Effects.shake

Shakes a camera entity; the shake dies away by itself.

```rust
effects.shake(camera, 8.0);
```

## Effects.burst

```rust
effects.burst(t.position, ParticleEmitter {
    speed_min: 80.0, speed_max: 220.0, lifetime_max: 0.6,
    color_start: rgb(255, 240, 200), color_end: rgba(255, 80, 0, 0), ..
}, 40);
```

## Effects.restart

```rust
effects.restart(fountain);
```


## Effects.add

```rust
let grade = effects.add(ColorGrading { saturation: 0.6, brightness: 0.9, .. });
```

## Effects.add_at

The same, at a place of your choosing in the chain; effects run from the lowest order up.

```rust
effects.add_at(Vignette { intensity: 0.8, .. }, 100);
```

## Effects.bloom

Adds a `Bloom` with a threshold and an intensity.

```rust
let glow = effects.bloom(0.7, 1.2);
```

## Effects.vignette

Adds a `Vignette`: darker edges.

```rust
effects.vignette(0.6, 0.75);
```

## Effects.crt

Adds a `Crt`: a curved screen with scanlines.

```rust
effects.crt(0.15, 0.35);
```

## Effects.grayscale

Adds a `Grayscale`: color to gray, 0 to 1.

```rust
let dead = effects.grayscale(1.0);
```

## Effects.sepia

Adds a `Sepia`: color to brown, 0 to 1.

```rust
effects.sepia(0.8);
```

## Effects.pixelate

Adds a `Pixelate`: blocks of `size` pixels.

```rust
effects.pixelate(4.0);
```

## Effects.blur

Adds a `Blur`, for behind a pause menu.

```rust
let soft = effects.blur(2.0, 2);
```

## Effects.fade_to

```rust
effects.fade_to(rgb(0, 0, 0), 1.0, 0.5);   // to black over half a second
effects.fade_to(rgb(0, 0, 0), 0.0, 0.5);   // and back
```

## Effects.set_ambient

```rust
effects.set_ambient(rgb(40, 40, 70));   // night
```

## Effects.set_lighting

Switches lighting on and off while the game runs.

```rust
effects.set_lighting(false);
```

## Effects.set_tween_speed

```rust
effects.set_tween_speed(0.0);   // freeze every tween
```

## Effect

```rust
@resource
struct Looks { crt: Effect }

@system
fn toggle_crt(input: Input, looks: Looks) {
    if input.key_pressed(Key::F2) {
        looks.crt.set_enabled(!looks.crt.enabled());
    }
}
```

## Effect.set

```rust
glow.set(Bloom { threshold: 0.5, intensity: 2.5, .. });
```

## Effect.set_enabled

Switches the effect on or off.

```rust
looks.crt.set_enabled(false);
```

## Effect.enabled

Whether the effect is on.

```rust
if looks.crt.enabled() { log("crt on"); }
```

## Effect.set_order

```rust
glow.set_order(10);
```

## Effect.remove

Removes the effect from the chain.

```rust
looks.crt.remove();
```

## Bloom

```rust
effects.add(Bloom { threshold: 0.5, intensity: 1.4, levels: 6, tint: rgb(255, 230, 200), .. });
```

## ColorGrading

```rust
effects.add(ColorGrading { saturation: 0.6, contrast: 1.1, .. });
```

## Blur

- radius: Spread in pixels
- iterations: Passes; more is smoother

```rust
effects.add(Blur { radius: 2.0, iterations: 3 });
```

## Pixelate

- size: Block size in pixels

```rust
effects.add(Pixelate { size: 6.0 });
```

## Grayscale

- amount: 0 leaves the colors, 1 is fully gray

```rust
effects.add(Grayscale { amount: 1.0 });
```

## Sepia

- amount: 0 leaves the colors, 1 is fully brown

```rust
effects.add(Sepia { amount: 0.7 });
```

## Vignette

```rust
effects.add(Vignette { intensity: 0.8, color: rgb(40, 0, 0), .. });
```

## Crt

```rust
effects.add(Crt { curvature: 0.2, scanlines: 0.5, chromatic_aberration: 2.0, .. });
```

## Save

A key-value store kept between runs. On the desktop it is a JSON file in the user's data folder, named after `organization` and `name` in `[project]`; in the browser it lives in the page's storage. Values change in memory, and `flush()` writes them. See [Saving](../guides/saving.md).

```rust
@system(on_enter: Phase::Over)
fn keep_best(game: Game, save: Save) {
    if game.score > save.get_int("best", 0) {
        save.set_int("best", game.score);
        save.flush();
    }
}
```

## Save.get_int

The int under `key`, or `fallback` when there is none or it holds another type.

```rust
let best = save.get_int("best", 0);
```

## Save.set_int

Stores an int under `key`.

```rust
save.set_int("level", 3);
```

## Save.get_float

The float under `key`, or `fallback`.

```rust
let volume = save.get_float("volume", 0.8);
```

## Save.set_float

Stores a float under `key`.

```rust
save.set_float("best_time", 42.5);
```

## Save.get_string

The string under `key`, or `fallback`.

```rust
let name = save.get_string("name", "Player");
```

## Save.set_string

Stores a string under `key`.

```rust
save.set_string("name", "Ada");
```

## Save.has

Whether a value is stored under `key`.

```rust
if !save.has("tutorial_done") { phase.set(Phase::Tutorial); }
```

## Save.erase

Removes the value under `key`.

```rust
save.erase("checkpoint");
```

## Save.flush

```rust
save.set_int("coins", wallet.coins);
save.flush();
```


## Save.get_bool

The bool under `key`, or `fallback`.

```rust
let music_on = save.get_bool("music", true);
```

## Save.set_bool

Stores a bool under `key`.

```rust
save.set_bool("music", false);
```

## Save.clear

```rust
save.clear();
save.flush();
```

## Save.slot

```rust
let slot = save.slot("slot2");
slot.set_int("level", 4);
slot.flush();
```

## Camera2D

```rust
@system
fn hover(input: Input, cams: Camera2D, draw: Draw) {
    let p = cams.screen_to_world(input.mouse_position());
    draw.circle_lines(p, 10.0, rgb(255, 255, 255), 1.0);
}
```

## Camera2D.screen_to_world

```rust
let world = cams.screen_to_world(vec2(0.0, 0.0));   // the world point at the top-left
```

## Camera2D.world_to_screen

A world point to logical screen pixels, through the main camera.

```rust
let on_screen = cams.world_to_screen(boss_position);
```

## Camera2D.visible

```rust
let view = cams.visible();
if !view.contains(t.position) { cmd.despawn(e); }
```

## Camera2D.screen_to_world_in

```rust
let p = cams.screen_to_world_in(right_camera, input.mouse_position());
```

## Camera2D.world_to_screen_in

A world point to screen pixels through a camera entity of your own.

```rust
let p = cams.world_to_screen_in(minimap_camera, player_pos);
```

## Camera2D.visible_in

The world rect a camera entity of your own shows.

```rust
let shown = cams.visible_in(minimap_camera);
```

## Draw

For debugging, previews and anything with no entity of its own. See [UI and Score](../guides/ui-and-score.md#drawing-without-entities).

- layer: The layer the next draws go on
- z: The z the next draws use

```rust
@system
fn show_aim(players: (Player, Transform)[], input: Input, draw: Draw) {
    for (_, t) in players {
        draw.line(t.position, input.mouse_world(), rgb(255, 80, 80), 2.0);
    }
    draw.hud();
    draw.text("aim", vec2(8.0, 8.0), 16.0, rgb(255, 255, 255));
}
```

## Draw.hud

```rust
draw.hud();
draw.rect(rect(10.0, 10.0, 100.0, 8.0), rgb(200, 40, 40));   // a health bar on the screen
```

## Draw.world

```rust
draw.world();
```

## Draw.on_layer

Draws the next shapes on this render layer.

```rust
draw.on_layer(2);
```

## Draw.at_z

Draws the next shapes at this z.

```rust
draw.at_z(-1.0);   // under the entities of layer 0
```

## Draw.rect

A filled rectangle.

```rust
draw.rect(rect(0.0, 0.0, 64.0, 32.0), rgba(0, 0, 0, 160));
```

## Draw.rect_lines

A rectangle's outline.

```rust
draw.rect_lines(rect(x, y, w, h), rgb(0, 255, 0), 1.0);
```

## Draw.rounded_rect

A filled rectangle with rounded corners.

```rust
draw.rounded_rect(rect(20.0, 20.0, 200.0, 60.0), 8.0, rgba(20, 20, 30, 220));
```

## Draw.circle

A filled circle.

```rust
draw.circle(t.position, 4.0, rgb(255, 255, 0));
```

## Draw.circle_lines

A circle's outline.

```rust
draw.circle_lines(t.position, 48.0, rgba(255, 255, 255, 80), 1.0);
```

## Draw.arc

```rust
draw.arc(t.position, 20.0, -PI / 2.0, -PI / 2.0 + TAU * reload.fraction(), rgb(255, 255, 255), 3.0);
```

## Draw.line

A line from `a` to `b`.

```rust
draw.line(a, b, rgb(255, 80, 80), 2.0);
```

## Draw.polyline

Lines through the points in order; `closed` joins the last to the first.

```rust
draw.polyline(path_points, rgb(80, 200, 255), 2.0, false);
```

## Draw.triangle

A filled triangle.

```rust
draw.triangle(vec2(0.0, 0.0), vec2(20.0, 10.0), vec2(0.0, 20.0), rgb(255, 255, 255));
```

## Draw.polygon

```rust
draw.polygon([vec2(0.0, 0.0), vec2(40.0, 0.0), vec2(50.0, 30.0), vec2(10.0, 40.0)], rgb(90, 160, 90));
```

## Draw.text

```rust
draw.text("fps " + string(int(1.0 / time.dt)), vec2(8.0, 8.0), 16.0, rgb(255, 255, 255));
```

## Draw.text_styled

```rust
draw.text_styled("Paused", vec2(480.0, 270.0), Text { size: 48.0, align: Align::Center, outline: 3.0, .. });
```

## Draw.texture

```rust
draw.texture(art.cursor, input.mouse_world());
```

## Draw.sprite

```rust
draw.sprite(Sprite { image: art.ghost, color: rgba(255, 255, 255, 100), .. }, at(x, y));
```

## Draw.nine_slice

```rust
draw.hud();
draw.nine_slice(art.panel, rect(20.0, 300.0, 400.0, 120.0), rect(0.0, 0.0, 0.0, 0.0), rect(8.0, 8.0, 8.0, 8.0), rgb(255, 255, 255));
```

## Draw.push_clip

```rust
draw.push_clip(rect(20.0, 20.0, 200.0, 100.0));
draw.text(long_story, vec2(20.0, 20.0 - scroll), 16.0, rgb(255, 255, 255));
draw.pop_clip();
```

## Draw.pop_clip

Ends the clip `push_clip` started.

```rust
draw.pop_clip();
```

## Physics

Gravity, queries about colliders, and pushes on bodies. See [Collisions and Physics](../guides/collisions.md).

```rust
@system
fn line_of_sight(guard: (Guard, Transform), physics: Physics,
                 players: (Entity, Player, Transform)[]) {
    let (_, eye) = guard;
    for (player, _, t) in players {
        let to = t.position - eye.position;
        let ray = physics.raycast(eye.position, to.normalized(), to.length());
        if ray.hit && ray.entity == player {
            log("spotted");
        }
    }
}
```

## Physics.gravity

World gravity in pixels per second squared. `[physics] gravity` in `drift.toml` sets where it starts.

```rust
let g = physics.gravity();
```

## Physics.set_gravity

Changes world gravity.

```rust
physics.set_gravity(vec2(0.0, -900.0));   // upside down
```

## Physics.raycast

```rust
let ray = physics.raycast(t.position, vec2(0.0, 1.0), 20.0);
let on_ground = ray.hit;
```

## Physics.raycast_mask

```rust
let ray = physics.raycast_mask(eye, dir, 400.0, WALLS);   // WALLS: a category bit of your own
```

## Physics.overlap_point

```rust
for e in physics.overlap_point(input.mouse_world()) {
    cmd.insert(e, Selected {});
}
```

## Physics.overlap_rect

```rust
let inside = physics.overlap_rect(rect(x, y, 64.0, 64.0));
```

## Physics.overlap_circle

```rust
for e in physics.overlap_circle(blast, 80.0) {
    physics.apply_impulse(e, vec2(0.0, -300.0));
}
```

## Physics.overlap_circle_mask

Entities whose colliders, of a category in `mask`, touch the circle.

```rust
let enemies_near = physics.overlap_circle_mask(t.position, 120.0, ENEMIES);
```

## Physics.apply_impulse

```rust
physics.apply_impulse(player, vec2(0.0, -350.0));   // a jump
```

## Physics.apply_force

```rust
physics.apply_force(ship, from_angle(t.rotation) * 400.0);   // thrust while held
```

## Physics.apply_torque

Turns a dynamic body over this step.

```rust
physics.apply_torque(wheel, 50.0);
```

## Physics.apply_angular_impulse

Changes a dynamic body's spin at once.

```rust
physics.apply_angular_impulse(crate_entity, 5.0);
```

## Physics.set_debug_draw

```rust
if input.key_pressed(Key::F3) { physics.set_debug_draw(true); }
```

## RayHit

- hit: Whether the ray hit anything
- entity: The entity it hit
- point: Where, in the world
- normal: The surface direction at the hit
- fraction: How far along the ray, 0 to 1

```rust
let ray = physics.raycast(origin, dir, 300.0);
if ray.hit {
    draw.circle(ray.point, 3.0, rgb(255, 0, 0));
}
```

## Random

```rust
@system(schedule: Schedule::Startup)
fn setup(random: Random, level: Level) {
    log("seed " + string(random.seed()));
    level.rng = random.stream("levels");
}
```

## Random.seed

The seed in use. With `seed = 0` in `[random]`, a new one is picked each run and logged.

```rust
hud.value = "Seed " + string(random.seed());
```

## Random.stream

```rust
let loot = random.stream("loot");
let levels = random.stream("levels");   // independent of loot
```


## Audio

The mixer: one music track at a time on the music bus, playlists, and the buses every sound plays on. See [Sound and Music](../guides/sound.md).

```rust
@resource
struct Tracks { theme: Sound = music("assets/theme.ogg") }

@system(schedule: Schedule::Startup)
fn start_music(tracks: Tracks, audio: Audio) {
    audio.play_music(tracks.theme, 0.0, true);
}
```

## Audio.play_music

```rust
audio.play_music(tracks.boss, 2.0, true);   // crossfade over two seconds
```

## Audio.play_playlist

```rust
audio.play_playlist([tracks.a, tracks.b, tracks.c], 0.0, true, true);
```

## Audio.next_track

Skips to the next track of the playlist.

```rust
audio.next_track();
```

## Audio.stop_music

Stops the music, fading out over `fade` seconds.

```rust
audio.stop_music(1.0);
```

## Audio.pause_music

Pauses the music, fading out over `fade` seconds.

```rust
audio.pause_music(0.3);
```

## Audio.resume_music

Resumes paused music, fading in over `fade` seconds.

```rust
audio.resume_music(0.3);
```

## Audio.music_playing

Whether music is playing.

```rust
if !audio.music_playing() { audio.play_music(tracks.theme, 0.0, true); }
```

## Audio.master_bus

The bus every other bus sits under.

```rust
audio.master_bus().set_volume(0.5, 0.0);
```

## Audio.music_bus

The bus music plays on.

```rust
audio.music_bus().set_volume(0.3, 0.5);   // duck the music under a dialog
```

## Audio.sfx_bus

The bus sounds play on unless told otherwise.

```rust
audio.sfx_bus().set_paused(true);   // pause the game's sounds, keep the music
```

## Audio.bus

```rust
let voice = audio.bus("voice", Bus { .. });
sfx.line.play_with(PlayOptions { bus: voice, .. });
```

## Audio.has_device

```rust
if !audio.has_device() { log("no sound"); }
```

## Systems

```rust
@system(on_enter: Phase::Paused)
fn freeze(systems: Systems) {
    systems.disable("spawn_enemies");
}
```

## Systems.enable

Turns a system on by its function name.

```rust
systems.enable("spawn_enemies");
```

## Systems.disable

Turns a system off by its function name.

```rust
systems.disable("spawn_enemies");
```

## Systems.enabled

Whether a system is on.

```rust
if !systems.enabled("autosave") { log("autosave is off"); }
```

# Assets {#assets}

Images, sounds, maps and the handles that name them. A handle such as `Texture` or `Sound` names something the engine owns; copy it freely, and keep the ones you reuse in a resource. See [Assets](../concepts/assets.md).

## texture

Works anywhere, field defaults included, and hands back the same texture for the same path. A file that does not load stops the game with the path and the error. Images load from PNG, JPG, BMP, TGA and GIF files.

```rust
@resource
struct Art { ship: Texture = texture("assets/ship.png") }
```

## sound

Works anywhere, field defaults included. The whole file is decoded when it loads.

```rust
@resource
struct Sfx { jump: Sound = sound("assets/jump.wav") }
```


## music

```rust
@resource
struct Tracks { theme: Sound = music("assets/theme.ogg") }
```

## Texture

An image the engine can draw. Load one with `texture(path)`, make one with `Assets.create_texture` or `Assets.render_target`, or from an [`Image`](#image).

- id: The engine's handle; `Texture {}` is no texture

```rust
cmd.spawn([at(0.0, 0.0), Sprite { image: art.ship, .. }]);
```

## Texture.size

Width and height in pixels.

```rust
let s = art.ship.size();
```

## Texture.set_filter

How the texture looks when scaled. `Filter::Nearest`, the default, keeps pixel art crisp; `Filter::Linear` smooths it.

```rust
art.photo.set_filter(Filter::Linear);
```

## Texture.to_image

```rust
let img = minimap.to_image();
img.save_png("minimap.png");
```

## Texture.update

```rust
fog_image.fill_rect(rect(px, py, 16.0, 16.0), rgba(0, 0, 0, 0));
fog.update(fog_image);
```

## Texture.update_rect

```rust
fog.update_rect(fog_image, rect(px, py, 16.0, 16.0));
```

## Sound

A sound the engine can play. Load one with `sound(path)` or make one with `Assets.create_sound`.

- id: The engine's handle

```rust
sfx.jump.play();
```

## Sound.play

Plays the sound once at full volume and returns its `Voice`.

```rust
sfx.coin.play();
```

## Sound.play_with

Plays the sound with a volume, a pitch, or looping.

```rust
sfx.coin.play_with(PlayOptions { volume: 0.6, pitch: 1.2, .. });
```


## Sound.duration

```rust
let seconds = sfx.speech.duration();
```

## Sound.set_max_instances

```rust
sfx.step.set_max_instances(3);
```

## PlayOptions

- volume: 0 to 1
- pitch: Playback speed; 2 is an octave up
- looping: Plays again when it ends, until stopped
- pan: -1 left to 1 right
- bus: The bus it plays on; `Bus {}` is the sfx bus
- fade_in: Seconds to fade in
- priority: Keeps its voice over lower priorities when every voice is busy

```rust
sfx.engine.play_with(PlayOptions { looping: true, volume: 0.4, .. });
```


## Voice

```rust
@resource
struct Engine { hum: Voice = Voice { .. } }

@system(schedule: Schedule::Startup)
fn start_engine(sfx: Sfx, engine: Engine) {
    engine.hum = sfx.engine.play_with(PlayOptions { looping: true, volume: 0.4, .. });
}
```

## Voice.stop

Stops it, fading out over `fade` seconds.

```rust
engine.hum.stop(0.5);
```

## Voice.pause

Pauses it, fading out over `fade` seconds.

```rust
engine.hum.pause(0.2);
```

## Voice.resume

Resumes it, fading in over `fade` seconds.

```rust
engine.hum.resume(0.2);
```

## Voice.set_volume

```rust
engine.hum.set_volume(0.1, 1.0);
```

## Voice.set_pan

Pan from -1 (left) to 1 (right).

```rust
engine.hum.set_pan(-0.5);
```

## Voice.set_pitch

Playback speed; 2 is an octave up.

```rust
engine.hum.set_pitch(1.0 + speed / 400.0);
```

## Voice.set_looping

Whether it plays again when it ends.

```rust
engine.hum.set_looping(false);
```

## Voice.playing

```rust
if !voice.playing() { voice = sfx.alarm.play(); }
```

## Voice.paused

Whether it is paused.

```rust
if engine.hum.paused() { engine.hum.resume(0.2); }
```

## Bus

```rust
let voices = audio.bus("voice", Bus { .. });
voices.set_volume(options.voice_volume, 0.0);
```

## Bus.set_volume

Volume 0 to 1, reached over `fade` seconds.

```rust
audio.music_bus().set_volume(0.3, 0.5);
```

## Bus.volume

The bus's volume.

```rust
let v = audio.music_bus().volume();
```

## Bus.set_muted

Mutes or unmutes everything on the bus.

```rust
audio.master_bus().set_muted(true);
```

## Bus.muted

Whether the bus is muted.

```rust
let silent = audio.master_bus().muted();
```

## Bus.set_paused

Pauses or resumes everything on the bus.

```rust
audio.sfx_bus().set_paused(true);
```

## Bus.paused

Whether the bus is paused.

```rust
if audio.sfx_bus().paused() { log("sounds paused"); }
```

## Bus.stop

```rust
audio.sfx_bus().stop(0.2);
```

## Font

A font for `Text.font`. `Font {}` is the built-in font.

- id: The engine's handle

```rust
Text { value: "Hello", font: Font { .. }, .. }
```

## Image

```rust
@system(schedule: Schedule::Startup)
fn sky(cmd: Commands, assets: Assets) {
    let img = assets.create_image(320, 180, rgb(0, 0, 0));
    img.gradient(rgb(20, 30, 80), rgb(200, 120, 90), true);
    cmd.spawn([at(160.0, 90.0), Sprite { image: img.to_texture(), .. }]);
    img.free();
}
```

## Image.width

Width in pixels.

```rust
let w = img.width();
```

## Image.height

Height in pixels.

```rust
let h = img.height();
```

## Image.get

The color of one pixel.

```rust
let c = img.get(3, 4);
```

## Image.set

Sets one pixel.

```rust
img.set(3, 4, rgb(255, 0, 0));
```

## Image.fill

Sets every pixel to one color.

```rust
img.fill(rgba(0, 0, 0, 0));
```

## Image.fill_rect

Sets the pixels in a rect to one color.

```rust
img.fill_rect(rect(0.0, 0.0, 8.0, 8.0), rgb(255, 255, 255));
```

## Image.gradient

```rust
img.gradient(rgb(0, 0, 40), rgb(80, 160, 255), true);
```

## Image.noise

```rust
img.noise(Noise { seed: 3, frequency: 0.05, octaves: 4, .. }, rgb(30, 60, 30), rgb(120, 200, 90));
```

## Image.line

```rust
img.line(vec2(0.0, 0.0), vec2(63.0, 63.0), rgb(255, 255, 255), 1.0);
```

## Image.rect_lines

A rectangle's outline, blended over the pixels.

```rust
img.rect_lines(rect(0.0, 0.0, 64.0, 64.0), rgb(255, 255, 255), 2.0);
```

## Image.circle

A filled circle, blended over the pixels.

```rust
img.circle(vec2(32.0, 32.0), 12.0, rgb(255, 200, 60));
```

## Image.circle_lines

A circle's outline, blended over the pixels.

```rust
img.circle_lines(vec2(32.0, 32.0), 20.0, rgb(255, 255, 255), 2.0);
```

## Image.draw

```rust
canvas.draw(stamp, 10, 10);
```

## Image.draw_part

```rust
canvas.draw_part(sheet_image, rect(16.0, 0.0, 16.0, 16.0), 0, 0);
```

## Image.paste

```rust
canvas.paste(icon, 0, 0);
```

## Image.copy

A new image with the same pixels.

```rust
let backup = img.copy();
```

## Image.crop

A new image of the part `r`.

```rust
let corner = img.crop(rect(0.0, 0.0, 16.0, 16.0));
```

## Image.flip_x

Mirrors the pixels left to right.

```rust
img.flip_x();
```

## Image.flip_y

Mirrors the pixels top to bottom.

```rust
img.flip_y();
```

## Image.pixels

```rust
let px = img.pixels();
let first = color_of(px[0]);
```

## Image.set_pixels

Replaces every pixel, row by row, as 0xRRGGBBAA ints.

```rust
img.set_pixels(px);
```

## Image.read_rect

The pixels of a rect, row by row, as 0xRRGGBBAA ints.

```rust
let tile = img.read_rect(rect(16.0, 16.0, 16.0, 16.0));
```

## Image.write_rect

Writes 0xRRGGBBAA pixels into a rect, row by row.

```rust
img.write_rect(rect(0.0, 0.0, 16.0, 16.0), tile);
```

## Image.to_texture

```rust
let t = img.to_texture();
```

## Image.save_png

Writes the image to a PNG file. Raises `drift_save_failed` when it cannot.

```rust
img.save_png("drawing.png");
```

## Image.download

```rust
img.download("drawing.png");
```

## Image.free

Releases the pixels. Textures made from the image stay.

```rust
img.free();
```

## pixel

```rust
img.write_rect(rect(0.0, 0.0, 1.0, 1.0), [pixel(rgb(255, 0, 0))]);
```

## color_of

The color of a 0xRRGGBBAA int.

```rust
let c = color_of(img.pixels()[0]);
```

## Spritesheet

```rust
let sheet = assets.spritesheet(texture("assets/hero.png"), 16.0, 16.0);
cmd.spawn([at(100.0, 100.0), Sprite { image: sheet.texture, frame: sheet.frame_at(0), .. }]);
```

- texture: The whole image
- frame_w: Width of one frame in pixels
- frame_h: Height of one frame in pixels
- columns: Frames across
- rows: Frames down

## Spritesheet.frame

The frame at a column and row.

```rust
sprite.frame = sheet.frame(2, 1);
```

## Spritesheet.frame_at

The frame at an index, counted left to right, top to bottom from 0.

```rust
sprite.frame = sheet.frame_at(int(time.elapsed * 10.0) % sheet.count());
```

## Spritesheet.frames

`count` frames starting at index `first`.

```rust
let run = sheet.frames(8, 6);
```

## Spritesheet.count

How many frames the sheet has.

```rust
let n = sheet.count();
```

## Map

Show a map with a [`Tilemap`](components.md#tilemap) component. Cells are [`Cell`](procgen.md#cell) values and tile ids count from 1, as in Tiled; 0 is an empty cell. See [Levels and Tilemaps](../guides/levels-and-tilemaps.md).

- id: The engine's handle

```rust
@resource
struct Level { map: Map = Map { .. } }
```

## Map.size

```rust
let cells = level.map.size();   // Cell { x: columns, y: rows }
```

## Map.tile_size

```rust
let px = level.map.tile_size();
```

## Map.bounds

```rust
cmd.insert(camera, CameraBounds { area: level.map.bounds() });
```

## Map.layer_count

How many layers the map has.

```rust
for i in range(level.map.layer_count()) { log(level.map.layer_name(i)); }
```

## Map.layer

The index of a layer by name. Raises `drift_no_such_layer` for a name the map does not have.

```rust
let ground = level.map.layer("Ground");
```

## Map.layer_name

The name of the layer at an index.

```rust
let name = level.map.layer_name(0);
```

## Map.set_layer_visible

Shows or hides a layer.

```rust
level.map.set_layer_visible(level.map.layer("Secrets"), true);
```

## Map.set_layer_solid

```rust
level.map.set_layer_solid(level.map.layer("Walls"), true, []);
```

## Map.tile

The tile id in a cell of a layer; 0 for empty.

```rust
if level.map.tile(ground, c) == 0 { log("a hole"); }
```

## Map.set_tile

Changes a tile. On a solid layer the colliders follow next frame.

```rust
level.map.set_tile(ground, c, 0);   // dig
```

## Map.int_grid

The value in an LDtk IntGrid layer.

```rust
let v = level.map.int_grid(collision, c);
```

## Map.set_int_grid

Changes an LDtk IntGrid value. LDtk's auto-layer rules do not run again.

```rust
level.map.set_int_grid(collision, c, 1);
```

## Map.tile_at

The tile id under a world position.

```rust
let under = level.map.tile_at(ground, t.position);
```

## Map.world_to_tile

The cell under a world position.

```rust
let c = level.map.world_to_tile(input.mouse_world(), ground);
```

## Map.tile_to_world

```rust
let p = level.map.tile_to_world(cell(3, 4), ground);
```

## Map.fill

```rust
level.map.fill(0, dungeon_grid, [1, 2, 3], cell(0, 0));   // FLOOR -> 1, WALL -> 2, DOOR -> 3
```

## Map.to_grid

```rust
let walls = level.map.to_grid(level.map.layer("Walls"));
let path = find_path(walls, from, to, [3], false);
```

## Map.property

```rust
let music = level.map.property("music");
```

## Map.destroy

```rust
cmd.despawn(level.map_entity);
level.map.destroy();
```

# Procedural Generation {#procgen}

Seeded random numbers, noise, level generators, weighted tables and pathfinding over grids. A seed gives the same numbers and the same levels on every platform and in the browser. See [Procedural Generation](../guides/procedural-generation.md).

## Rng

A seeded random number generator. Its methods advance it, so keep it in a resource or component to carry on where it left off. Assigning a struct shares it: `let b = a;` gives a second name for the same generator, and `a.copy()` gives an independent one.

- hi: Generator state
- lo: Generator state
- inc: Generator stream

```rust
@resource
struct Loot { rng: Rng = rng(1234) }

@system
fn drop(loot: Loot) {
    let gold = loot.rng.range(5, 10);
}
```

## rng

```rust
let r = rng(42);
```

## rng_stream

```rust
let caves = rng_stream(seed, 1);
let dungeons = rng_stream(seed, 2);
```

## seed_from

```rust
let r = rng(seed_from("daily-2026-11-13"));
```

## Rng.next_int

```rust
let bits = r.next_int();
```

## Rng.next_float

```rust
if r.next_float() < 0.25 { spawn_bonus(); }
```

## Rng.below

```rust
let side = r.below(4);   // 0, 1, 2 or 3
```

## Rng.range

```rust
let damage = r.range(1, 6);   // a six-sided die
```

## Rng.range_float

```rust
let x = r.range_float(0.0, 640.0);
```

## Rng.chance

`true` with probability `p`, 0 to 1.

```rust
if r.chance(0.15) { drop_rare(); }
```

## Rng.sign

```rust
let dir = r.sign();
```

## Rng.angle

```rust
let v = from_angle(r.angle()) * 100.0;
```

## Rng.unit_vector

A direction of length 1.

```rust
let push = r.unit_vector() * 200.0;
```

## Rng.inside_circle

A point inside a circle of `radius` around the origin.

```rust
let spot = center + r.inside_circle(32.0);
```

## Rng.inside_rect

A point inside a rect.

```rust
let p = r.inside_rect(rect(0.0, 0.0, 640.0, 360.0));
```

## Rng.normal

```rust
let height = r.normal(170.0, 10.0);
```

## Rng.weighted

```rust
let i = r.weighted([70.0, 25.0, 5.0]);
```

## Rng.pick

```rust
let name = r.pick<string>(["Ada", "Grace", "Alan"]);
```

## Rng.shuffle

```rust
let deck = r.shuffle<Card>(cards);
```

## Rng.copy

```rust
let replay = r.copy();
```

## Rng.split

```rust
let room_rng = level.rng.split();
```

## WeightedTable

- items: The entries
- weights: Their weights

```rust
let loot = weighted_table<string>().add("coin", 70.0).add("potion", 25.0).add("sword", 5.0);
if loot.roll(r) is Some(item) {
    log("found a " + item);
}
```

## weighted_table

An empty table of items of type `T`. Weights are relative; an entry with weight 0 or less never comes up.

```rust
let enemies = weighted_table<int>().add(1, 10.0).add(2, 3.0);
```

## WeightedTable.add

Adds an item with a weight, and returns the table, so calls chain.

```rust
table.add("gem", 2.0);
```

## WeightedTable.roll

An item chosen in proportion to its weight, or `None` when no entry can come up.

```rust
if table.roll(r) is Some(kind) { spawn(kind); }
```

## Noise

- seed: Different seeds give unrelated fields
- kind: `Value` is blocky; `Perlin` and `Simplex` are smooth
- frequency: Scales positions; 0.05 makes features about 20 units across
- octaves: Layers of finer detail
- lacunarity: Each layer's frequency over the last one's
- gain: Each layer's strength over the last one's
- warp: Bends the field by up to this many units, for swirls and rivers
- warp_frequency: The frequency of the bending field

```rust
let terrain = Noise { seed: 7, frequency: 0.03, octaves: 5, .. };
let h = terrain.sample(x, y);
```

## noise

The default recipe with a seed.

```rust
let n = noise(seed);
```

## Noise.sample

The noise at a point, from -1 to 1.

```rust
let v = terrain.sample(float(x), float(y));
```

## Noise.samples

```rust
let row = terrain.samples(vec2(0.0, 0.0), 64, 64, 1.0);
```

## Noise.grid

```rust
let land = noise(seed).grid(60, 32, 0.1, FLOOR, WALL);
```

## Cell

A cell of a grid or tilemap: a column `x` and a row `y`.

- x: Column, from 0 at the left
- y: Row, from 0 at the top

```rust
let c = Cell { x: 3, y: 4 };
```

## cell

```rust
let start = cell(1, 1);
```

## Grid

The generators write `FLOOR` for open cells and `WALL` for solid ones, and dungeons add `DOOR`; your own values can be anything.

- width: Columns
- height: Rows
- cells: `width * height` values, row by row from the top-left

```rust
let g = grid(20, 10, FLOOR);
g.set(cell(5, 5), WALL);
```

## grid

A grid of `width` by `height` cells, all holding `value`.

```rust
let g = grid(40, 25, WALL);
```

## Grid.in_bounds

Whether a cell is inside the grid.

```rust
if g.in_bounds(c) { g.set(c, FLOOR); }
```

## Grid.get

```rust
if level.grid.get(next) != WALL { pos.cell = next; }
```

## Grid.set

Sets a cell's value; cells outside the grid are ignored.

```rust
g.set(cell(0, 0), DOOR);
```

## Grid.count

How many cells hold `value`.

```rust
let open = g.count(FLOOR);
```

## Grid.cells_of

```rust
let floors = g.cells_of(FLOOR);
```

## Grid.keep_largest_region

```rust
caves.keep_largest_region(FLOOR, WALL);
```

## Grid.remove_small_regions

```rust
g.remove_small_regions(FLOOR, 12, WALL);
```

## Grid.connect_regions

```rust
g.connect_regions(FLOOR, 2);
```

## Grid.region_sizes

```rust
let sizes = g.region_sizes(FLOOR);
```

## Grid.flood_fill

```rust
let reach = g.flood_fill(start);
```

## FLOOR

The value generators write for open cells.

```rust
if g.get(c) == FLOOR { place_coin(c); }
```

## WALL

The value generators write for solid cells, and what `Grid.get` answers outside the grid.

```rust
let path = find_path(g, a, b, [WALL], false);
```

## DOOR

The value `dungeon` writes where a corridor enters a room.

```rust
let doors = d.grid.cells_of(DOOR);
```

## CaveOptions

- fill: Chance a cell starts as wall; higher gives narrower caves
- steps: Smoothing passes
- birth: Wall neighbours, of 8, that turn a cell into wall
- survive: Wall neighbours that keep a wall
- solid_edges: The outer ring is always wall
- keep_largest: Fills every cavern but the largest, so the whole cave is reachable

```rust
let caves = cave(r, 60, 32, CaveOptions { fill: 0.48, steps: 4, .. });
```

## cave

```rust
let g = cave(level.rng, 60, 32, CaveOptions { .. });
```

## GridRect

- x: Left column
- y: Top row
- w: Width in cells
- h: Height in cells

```rust
for room in d.rooms {
    if room.contains(player_cell) { log("in a room"); }
}
```

## GridRect.center

The cell in the middle.

```rust
let start = d.rooms[0].center();
```

## GridRect.contains

Whether a cell is inside the room.

```rust
if room.contains(c) { lit = true; }
```

## DungeonOptions

- min_room: Smallest room side, in cells
- max_room: Largest room side
- padding: Wall kept around each room inside its partition
- corridor_width: Corridor width in cells
- extra_corridors: Corridors between nearby rooms on top of the ones that join them all; they make loops
- doors: Writes `DOOR` where a corridor enters a room

```rust
let d = dungeon(r, 40, 25, DungeonOptions { min_room: 4, max_room: 8, extra_corridors: 3, .. });
```

## Dungeon

What `dungeon` makes.

- grid: The level
- rooms: Every room, in the order the generator made them

```rust
let d = dungeon(r, 40, 25, DungeonOptions { .. });
let stairs = d.rooms[d.rooms.len() - 1].center();
```

## dungeon

```rust
let d = dungeon(level.rng, 40, 25, DungeonOptions { .. });
level.grid = d.grid;
```

## WalkOptions

- coverage: Stops once this share of the cells is floor
- walkers: How many walkers carve at once
- turn_chance: Chance per step to turn
- brush: Carves `brush` by `brush` cells per step
- start: Start cell; `cell(-1, -1)` is the middle

```rust
let tunnels = random_walk(r, 60, 32, WalkOptions { walkers: 4, brush: 2, .. });
```

## random_walk

```rust
let g = random_walk(r, 60, 32, WalkOptions { coverage: 0.35, .. });
```

## poisson_disk

```rust
for p in poisson_disk(r, rect(0.0, 0.0, 960.0, 540.0), 48.0) {
    cmd.spawn([at(p.x, p.y), Sprite { image: art.tree, .. }]);
}
```

## poisson_on_grid

```rust
for p in poisson_on_grid(r, level.grid, FLOOR, 6.0) {
    cmd.spawn([at(p.x * 16.0, p.y * 16.0), Coin {}]);
}
```

## WfcOptions

- attempts: Restarts after a contradiction
- periodic: Neighbours wrap around the edges

```rust
let ruins = wfc(r, sample, 60, 32, WfcOptions { attempts: 20, .. });
```

## wfc

```rust
if wfc(r, sample, 60, 32, WfcOptions { .. }) is Some(g) {
    g.keep_largest_region(FLOOR, WALL);
    level.grid = g;
}
```

## find_path

```rust
let path = find_path(level.grid, monster_cell, player_cell, [WALL], false);
if path.len() > 1 {
    pos.cell = path[1];   // one step closer
}
```

## FlowField

```rust
level.field = flow_field(level.grid, [player_cell], [WALL], true);
v.linear = level.field.direction(my_cell) * 60.0;
```

- width: Columns
- height: Rows
- next: For each cell, the index of the next cell toward a goal
- costs: For each cell, steps to the nearest goal

## flow_field

A flow field over a grid toward the nearest of `goals`, through cells whose value is not in `blocked`. Build it again when a goal moves to another cell, not every frame.

```rust
let field = flow_field(g, [goal], [WALL], true);
```

## FlowField.in_bounds

Whether a cell is inside the field.

```rust
if field.in_bounds(c) { step = field.step(c); }
```

## FlowField.cost

```rust
if field.cost(c) < 0.0 { log("cut off"); }
```

## FlowField.step

```rust
pos.cell = field.step(pos.cell);
```

## FlowField.direction

```rust
v.linear = field.direction(c) * speed;
```

## FlowField.direction_at

```rust
v.linear = field.direction_at(t.position, 16.0) * speed;
```

# Enums {#enums}

Every enum the `drift` module defines, with its variants.

## Schedule

When a system runs; see [`@system`](attributes.md#system).

- Startup: Once, before the first frame
- Update: Every frame

```rust
@system(schedule: Schedule::Startup)
fn setup(cmd: Commands) { }
```

## Align

Which point of a `Text` sits on its position, left to right; the text also turns around this point.

```rust
Text { value: "Title", align: Align::Center, .. }
```

## VAlign

Which point of a `Text` sits on its position, top to bottom.

```rust
Text { value: "Paused", align: Align::Center, valign: VAlign::Middle, .. }
```

## Blend

How a drawable mixes with what is behind it.

- Alpha: Ordinary transparency
- Additive: Adds light: glows, fire, sparks
- Multiply: Darkens: shadows, tints
- Screen: Lightens softly
- Premultiplied: For images whose colors are already multiplied by their alpha
- Opaque: Ignores alpha

```rust
Sprite { image: art.glow, blend: Blend::Additive, .. }
```

## Filter

How a texture looks scaled; see [`Texture.set_filter`](assets.md#textureset_filter).

- Nearest: Crisp pixels
- Linear: Smooth

```rust
art.background.set_filter(Filter::Linear);
```

## BodyKind

What kind of physics body a `RigidBody` is.

- Static: Never moves
- Dynamic: Falls, collides and is pushed
- Kinematic: Moves only by its Velocity; pushes dynamic bodies and is not pushed

```rust
RigidBody { kind: BodyKind::Kinematic, .. }
```

## JointKind

- Distance: Keeps the anchors a distance apart, or within `min_length` and `max_length`, as a rod, spring or rope
- Revolute: A hinge: the bodies turn about the anchor
- Prismatic: A slider along `axis`
- Weld: Glues the bodies together, or holds them with a spring

```rust
Joint { kind: JointKind::Revolute, target: wheel, .. }
```

## Ease

Easing curves for tweens. `In` starts slow, `Out` ends slow and `InOut` does both. `Back` overshoots, `Elastic` wobbles, `Bounce` bounces, and `Spring` settles like a spring.

```rust
tween().move_to(vec2(400.0, 300.0), 0.6, Ease::OutBack)
```

## EmitShape

Where a `ParticleEmitter`'s particles start.

- Point: At the entity's position
- Circle: Inside a circle of `shape_radius`
- Ring: On a circle of `shape_radius`
- Box: Inside a box of half size `shape_extents`
- Line: Along the line from `-shape_extents` to `shape_extents`

```rust
ParticleEmitter { shape: EmitShape::Ring, shape_radius: 40.0, .. }
```

## NoiseType

The kind of [`Noise`](procgen.md#noise).

- Value: Blocky
- Perlin: Smooth
- Simplex: Smooth, with fewer grid patterns

```rust
Noise { kind: NoiseType::Perlin, frequency: 0.05, .. }
```


## OccluderShape

The shape of a [`LightOccluder`](components.md#lightoccluder).

- Box: A box of `half_size`
- Circle: A circle of `radius`
- Polygon: The corners in `points`
- FromCollider: The entity's collider

```rust
LightOccluder { shape: OccluderShape::FromCollider, .. }
```

## Rolloff

How an [`AudioSource`](components.md#audiosource) gets quieter with distance.

- Linear: Evenly from `min_distance` to `max_distance`
- Inverse: Quickly near, slowly far

```rust
AudioSource { spatial: true, rolloff: Rolloff::Inverse, .. }
```

## Key

Keyboard keys, by position on a US layout.

```rust
if input.key_pressed(Key::Escape) { phase.set(Phase::Paused); }
```

## MouseButton

```rust
if input.mouse_pressed(MouseButton::Left) { shoot(); }
```

## GamepadButton

Gamepad buttons in the Xbox layout: `South` is A, `East` is B, `West` is X and `North` is Y.

```rust
input.bind_button("jump", GamepadButton::South);
```

## GamepadAxis

Sticks read -1 to 1 with Y down; triggers read 0 to 1.

```rust
input.bind_stick("move", GamepadAxis::LeftX, GamepadAxis::LeftY);
```

# Values and Functions {#values}

Plain values and free functions the module provides. A name you declare yourself shadows the one from the module, so a game can have its own `log` or `min`.

## Vec2

A 2D vector: a position, an offset, a direction or a velocity. `a + b`, `a - b`, `-a`, `a * s` and `a / s` (with a float `s`) work as expected.

- x: Horizontal, growing to the right
- y: Vertical, growing downward

```rust
let p = vec2(10.0, 20.0) + vec2(1.0, 0.0) * 5.0;
```

## vec2

```rust
let up = vec2(0.0, -1.0);
```

## Vec2.dot

```rust
let facing = dir.dot(to_player) > 0.0;
```

## Vec2.length

```rust
if v.linear.length() > 300.0 { v.linear = v.linear.normalized() * 300.0; }
```

## Vec2.distance

```rust
if t.position.distance(goal) < 8.0 { arrived = true; }
```

## Vec2.normalized

The same direction with length 1; the zero vector stays zero.

```rust
let dir = (target - t.position).normalized();
```

## Vec2.lerp

```rust
t.position = t.position.lerp(target, 0.1);   // eases toward the target
```

## Vec2.angle

```rust
t.rotation = (input.mouse_world() - t.position).angle();
```

## Vec2.rotated

```rust
let spread = dir.rotated(0.2);
```

## Vec2.perp

```rust
let side = dir.perp();
```

## Vec2.length_squared

The length times itself, cheaper than `length` for comparisons.

```rust
if offset.length_squared() < 100.0 { close = true; }
```

## Vec2.clamp_length

```rust
v.linear = v.linear.clamp_length(250.0);
```

## from_angle

```rust
let v = from_angle(t.rotation) * 300.0;
```

## Rect

A rectangle: top-left corner and size.

- x: Left edge
- y: Top edge
- w: Width
- h: Height

```rust
let arena = rect(0.0, 0.0, 960.0, 540.0);
```

## rect

```rust
let r = rect(10.0, 10.0, 100.0, 40.0);
```

## Rect.contains

Whether a point is inside.

```rust
if button.contains(input.mouse_position()) { hovered = true; }
```

## Rect.overlaps

Whether two rects overlap.

```rust
if a.overlaps(b) { log("touching"); }
```

## Rect.center

The middle point.

```rust
let mid = arena.center();
```

## Color

A color with red, green, blue and alpha from 0.0 to 1.0. Build one from 0 to 255 values with `rgb` and `rgba`.

- r: Red
- g: Green
- b: Blue
- a: Alpha: 0 is invisible, 1 is solid

```rust
let sky = Color { r: 0.4, g: 0.6, b: 0.9, a: 1.0 };
```

## rgb

A solid color from 0 to 255 values.

```rust
let gold = rgb(255, 210, 60);
```

## rgba

A color with alpha, all from 0 to 255.

```rust
let shade = rgba(0, 0, 0, 128);
```

## Color.alpha

The same color with another alpha, 0 to 1.

```rust
let faded = gold.alpha(0.3);
```

## Color.lerp

A blend from this color to `o`; `t` from 0 to 1.

```rust
text.color = rgb(255, 255, 255).lerp(rgb(255, 60, 60), danger);
```

## Entity

- id: The engine's id; 0 is no entity

```rust
@resource
struct Target { entity: Entity = Entity { .. } }

if target.entity.id == 0 { log("nothing selected"); }
```

## hud_layer

The screen layer. It draws last, after post effects, in screen pixels without a camera: (0, 0) is the top-left of the window's logical area. Put scores, menus and health bars there.

```rust
Text { value: "Lives 3", layer: hud_layer(), .. }
```

## layer_mask

```rust
Camera { layer_mask: layer_mask([0, 1, 2]), .. }
```

## ALL_LAYERS

```rust
Camera { layer_mask: ALL_LAYERS, .. }
```

## ALL_CATEGORIES

```rust
let ray = physics.raycast_mask(p, dir, 100.0, ALL_CATEGORIES);
```

## PI

Half a turn in radians.

```rust
tween().rotate_by(PI, 0.3, Ease::OutQuad)
```

## TAU

A whole turn in radians.

```rust
let angle = r.range_float(0.0, TAU);
```

## sin

```rust
t.position = vec2(x, 100.0 + sin(time.elapsed * 2.0) * 20.0);
```

## cos

```rust
let x = cos(angle) * radius;
```

## atan2

```rust
let a = atan2(dy, dx);
```

## clamp

```rust
hp = clamp(hp + 10.0, 0.0, 100.0);
```

## min

The smaller of two floats.

```rust
let step = min(distance, speed * time.dt);
```

## max

The larger of two floats.

```rust
let left = max(0.0, timer - time.dt);
```

## log

Writes a line to the engine log: the terminal on the desktop, the browser console on the web.

```rust
log("wave " + string(waves.count));
```

## warn

Writes a warning to the engine log.

```rust
warn("no spawn point in this level");
```

## Timer

Counts down game time for spawn waves, cooldowns and delays. It is a plain value: keep it in a resource or component and tick it with the frame time.

- seconds: The period
- left: Seconds left in the current period
- repeating: Starts over each time it finishes
- paused: A paused timer does not advance

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

## timer

A timer that finishes once after `seconds`.

```rust
let fuse = timer(3.0);
```

## repeating_timer

A timer that finishes every `seconds`.

```rust
let every_second = repeating_timer(1.0);
```

## Timer.tick

Advances by `dt` seconds and returns how many times the timer finished: 0 or 1 for a one-shot timer, more for a repeating one in a long frame.

```rust
if bomb.fuse.tick(time.dt) > 0 { explode(); }
```

## Timer.finished

Whether a one-shot timer has run out. It stays finished until `reset()`.

```rust
if shield.timer.finished() { shield.on = false; }
```

## Timer.fraction

Progress through the current period, 0 to 1.

```rust
bar.size = vec2(100.0 * reload.fraction(), 6.0);
```

## Timer.reset

Starts the period again.

```rust
gun.reload.reset();
```


## Timer.remaining

Seconds left in the current period.

```rust
label.value = string(int(round.left.remaining())) + " s";
```

## Cooldown

- seconds: The time between uses
- left: Seconds until it is ready

```rust
@component
struct Gun { reload: Cooldown = cooldown(0.25) }

@system
fn fire(row: (Entity, Gun), input: Input, time: Time) {
    let (_, gun) = row;
    gun.reload.tick(time.dt);
    if input.action_held("fire") && gun.reload.try_use() {
        log("bang");
    }
}
```

## cooldown

A cooldown of `seconds`, ready at once.

```rust
let dash = cooldown(0.8);
```

## Cooldown.tick

Advances it by `dt` seconds.

```rust
dash.tick(time.dt);
```

## Cooldown.ready

Whether it can be used.

```rust
icon.visible = dash.ready();
```

## Cooldown.try_use

```rust
if input.action_pressed("dash") && dash.try_use() { v.linear = v.linear * 3.0; }
```

## Cooldown.fraction

```rust
draw.arc(t.position, 12.0, 0.0, TAU * dash.fraction(), rgb(255, 255, 255), 2.0);
```

## Cooldown.reset

Makes it ready at once.

```rust
dash.reset();
```

## plus

```rust
cmd.spawn(plus(enemy(at(10.0, 20.0)), [Boss {}]));
```

## parent_of

```rust
if parent_of(e).id == 0 { log("a root"); }
```

## children_of

```rust
for c in children_of(ship) { cmd.despawn(c); }
```

## descendants_of

```rust
let parts = descendants_of(robot);
```

## world_transform

```rust
let tip = world_transform(sword_tip).position;
```

## to_world

```rust
let muzzle = to_world(gun, vec2(18.0, 0.0));
```

## to_local

```rust
let local = to_local(ship, input.mouse_world());
```

# Procedural Animation {#animation}

Angles, easing helpers, oscillators, springs, squash and stretch, inverse kinematics, ropes and stepping feet: the pieces for animating parts in code rather than from frames. See [Procedural Animation](../guides/procedural-animation.md) and the [Creature](../examples/creature.md) example.

# Data, Networking and Text {#data}

Data files, HTTP and WebSockets, sequences of timed steps, colors, text formatting and localization. See [Data Files](../guides/data-files.md), [Networking](../guides/networking.md) and [Localization](../guides/localization.md).

# Gameplay: drift/gameplay {#gameplay}

Health and damage, hit effects, screen bounds, spawners and a saved high score: components most action games write for themselves. Import the module beside `drift`:

```rust
import "drift" { * };
import "drift/gameplay" { * };
```

Each part runs only for entities that have its components. See [Building Blocks](../guides/building-blocks.md).

# Controllers: drift/control {#control}

Movement controllers for the player's entity: top-down 8-way movement, a platformer with coyote time, jump buffering and variable jump height, and an asteroids-style ship. Each reads the arrow keys, WASD and the first gamepad's left stick by default, plus the input actions it names, and drives the entity's `Velocity`.

```rust
import "drift" { * };
import "drift/control" { * };
```

See [Building Blocks](../guides/building-blocks.md).

# Screens: drift/screens {#screens}

A title screen, play, pause and game over, with fades and wipes between them. The module adds the `Screen` state and the `Screens` resource that changes it behind a transition.

```rust
import "drift" { * };
import "drift/screens" { * };
```

Mark game systems with `in_state: [Screen::Playing], run_if: [running]` so they stop on other screens and while paused. See [Building Blocks](../guides/building-blocks.md).

# State Machines and AI: drift/ai {#ai}

A state machine per entity over an enum of your own, and options picked by score. Both are plain values kept in your components and resources.

```rust
import "drift" { * };
import "drift/ai" { * };
```

See [State Machines and AI](../guides/ai.md).

# Inventory, Stats, Quests and Dialogue: drift/rpg {#rpg}

Inventories, stats with modifiers, story flags, quests, and dialogue played from a script, as plain values the game keeps and saves.

```rust
import "drift" { * };
import "drift/rpg" { * };
```

See [Inventory, Stats, Quests and Dialogue](../guides/game-systems.md).

# Debug Overlay: drift/debug {#debug}

An on-screen overlay and time controls that work in every build, the browser included: frames per second, the entity count, the time scale, watched resource fields and recent messages, with keys for collider outlines, slow motion, freezing and stepping.

```rust
import "drift" { * };
import "drift/debug" { * };
```

See [Debugging](../guides/debugging.md#the-debug-overlay-in-every-build).

# Sections {#sections}

Where names the notes do not place go, by the `// ---- Section ----` comment they sit under in the module's files. Names in a section not listed here go on the Values and Functions page.

- Values: values
- Assets: assets
- Engine components: components
- Collisions: system-parameters
- Lookups: system-parameters
- Commands: system-parameters
- States: system-parameters
- Engine resources: resources
- Timers: values
- Random numbers: procgen
- Noise: procgen
- Grids: procgen
- Generators: procgen
- Pathfinding: procgen
- Tilemaps: assets
- Rendering and presentation: resources
- Render layers: values
- Cameras: components
- Immediate drawing: resources
- Particles: components
- Images: assets
- Transforms, hierarchy and procedural animation: animation
- Sprite sheets: assets
- Hierarchy: values
- Angles: animation
- Oscillators and wobble: animation
- Squash and stretch: animation
- Springs: animation
- Two-bone IK: animation
- IK chains (FABRIK): animation
- Verlet ropes: animation
- Stepping feet: animation
- Input: resources
- Physics: resources
- Post effects: resources
- Lights: components
- Trails: components
- Audio: assets
- Systems: resources
- Fonts and text: assets
- Shaders and materials: assets
- The window: resources
- Tilemap objects and autotiling: assets
- Sprite animation: components
- Spatial index: procgen
- Sight on grids: procgen
- Data files: data
- Networking: data
- Sequences: data
- Math helpers: values
- Colors: data
- Text formatting: data
- Localization: data
- Scenes and prefabs: assets


# Internal {#internal}

Helpers the module needs; games do not call them.

## drift_type_name
## key_code
## mouse_code
## button_code
## axis_code
## rng_state
## grid_answer
## points_answer
## load_map
## color_list
## rect_list
## flag
## image_answer
## max_int
## ids_to_entities
## entity_list
## TweenStep
## Tween.add_step
## Rng.ints
## Rng.floats
## Noise.type_code
## Grid.op
## Camera2D.convert
## Draw.shape
## Image.op
## bool_float
## anchor_point
## align_of
## valign_of
## top_left
## part_rect
## fade_of
## widget_size
## spawn_widget
## ui_nav_up
## ui_nav_down
## ui_activate
## ui_nav_x
## ease_out
## reveal_step

# HUD and Menus: drift/ui {#ui}

The `drift/ui` module: labels, panels, bars, buttons, toggles, sliders, toasts and dialog boxes on the screen layer. Import it beside `drift`:

```rust
import "drift" { * };
import "drift/ui" { * };
```

See [UI and Score](../guides/ui-and-score.md) for a walk through it.
