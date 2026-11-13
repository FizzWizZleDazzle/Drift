<!-- Written by scripts/candela-reference.py from the drift module and
     bindings/candela/docs/api-notes.md. Edit those, not this page. -->

# System Parameters

A system's parameter types say what it receives. Drift reads them when the game loads and fills each one, in any order. Every parameter needs its type written out.

| Parameter type | Receives |
|----------------|----------|
| A component, such as `bird: Bird` or `t: Transform` | That component of one entity. The system runs once per entity that has every such component |
| `Entity`, beside component parameters | The entity the call is for |
| `_: Enemy` | Requires the component without reading it |
| `(A, B)[]`, a list of tuples | One tuple per entity with every component; `Entity` in a position gives the entity's id |
| `Row[]`, where `Row` is a `@query` struct | One row per matching entity |
| [`Lookup<T>`](#lookup) | Every `T`, found by entity |
| A `@resource` or `@settings` struct | The resource. The system is skipped while it does not exist |
| [`State<S>`](#state) | The current variant of a `@state` enum, to read and change |
| `M` or `M[]`, where `M` is a `@message` struct | One message per call, or every message sent since the system last ran |
| [`Contact<A, B>`](#contact), [`ContactEnd`](#contactend), [`Overlap`](#overlap), [`OverlapEnd`](#overlapend), alone or as a list | Collisions between an entity with `A` and one with `B`; alone, the system runs once per collision |
| [`Commands`](#commands) | Spawning, inserting, removing and despawning, applied when the call returns |
| An engine resource: `Time`, `Input`, `Window`, `Assets`, `Effects`, `Save`, `Random`, `Camera2D`, `Draw`, `Physics` | See [Engine Resources](resources.md) |

A system may not take both a single component and a single event: it runs once per entity or once per event, not both.

Drift keeps every change a system makes to the components and resources it receives, whether through a single parameter, a tuple, a row or a lookup. The lists are built when the call starts, so spawning and despawning while looping over one is safe. Rows come in the order entities were spawned, the same on every run.

## Commands

Changes to the world's shape: making and removing entities, adding and removing components, inserting resources and sending messages. Drift applies them when the system returns, in the order recorded, before the next system starts. See [Components](../concepts/components.md#making-entities).

```rust
struct Commands {
    id: int = 0,
}
```

```rust
@system(schedule: Schedule::Startup)
fn setup(cmd: Commands) {
    let player = cmd.spawn([at(100.0, 100.0), Rectangle { .. }, Player { .. }]);
    cmd.insert(player, Velocity { linear: vec2(50.0, 0.0), .. });
}
```

### Commands.spawn_child

```rust
fn spawn_child(self, parent: Entity, bundle: any[]) -> Entity
```

Spawns an entity whose Transform is relative to `parent`.

```rust
let ship = cmd.spawn([at(200.0, 200.0), Sprite { image: art.ship, .. }]);
cmd.spawn_child(ship, [at(0.0, -20.0), Sprite { image: art.turret, .. }]);
```

### Commands.set_parent

```rust
fn set_parent(self, child: Entity, parent: Entity)
```

Moves `child` under `parent`, keeping its Transform's numbers.

```rust
cmd.set_parent(sword, hand);
```

### Commands.set_parent_in_place

```rust
fn set_parent_in_place(self, child: Entity, parent: Entity)
```

Moves `child` under `parent`, keeping it where it is on screen.

```rust
cmd.set_parent_in_place(crate_entity, platform);   // rides along from where it is
```

### Commands.detach

```rust
fn detach(self, child: Entity)
```

Makes `child` a root again, where it is on screen.

```rust
cmd.detach(sword);   // drops it where it is
```

### Commands.spawn

```rust
fn spawn(self, bundle: any[]) -> Entity
```

Makes an entity with the components in the list, at most one of each type, and returns its id. The id is valid at once: use it in later commands of the same system.

```rust
let e = cmd.spawn([at(320.0, 180.0), Circle { radius: 8.0, .. }, Coin {}]);
```

### Commands.insert

```rust
fn insert(self, e: Entity, component: any)
```

Adds a component to an entity, or replaces the one of its type.

```rust
cmd.insert(hit.b, Stunned { seconds: 2.0 });
```

### Commands.remove

```rust
fn remove<T>(self, e: Entity)
```

Takes a component off an entity. Name the type in angle brackets. Nothing happens if the entity does not have one.

```rust
cmd.remove<Stunned>(e);
```

### Commands.despawn

```rust
fn despawn(self, e: Entity)
```

Removes an entity, and its children. Despawning an entity that is already gone does nothing.

```rust
cmd.despawn(hit.b);
```

### Commands.insert_resource

```rust
fn insert_resource(self, r: any)
```

Adds a resource, or replaces the one of its type. Systems that take it start running.

```rust
cmd.insert_resource(Level { map: map, number: 1 });
```

### Commands.remove_resource

```rust
fn remove_resource<R>(self)
```

Removes a resource. Systems that take it stop running until it is inserted again.

```rust
cmd.remove_resource<Boss>();
```

### Commands.send

```rust
fn send(self, message: any)
```

Sends a message to the systems that read its type. See [Events](../concepts/events.md).

```rust
cmd.send(Damage { target: hit.b, amount: 10 });
```

### Commands.restart

```rust
fn restart(self)
```

Starts the game over once this frame's systems have run: every entity the game spawned goes, resources and states return to their defaults, and the startup systems run again.

```rust
if input.key_pressed(Key::F5) { cmd.restart(); }
```

### Commands.send_after

```rust
fn send_after(self, seconds: float, message: any)
```

Sends the message once `seconds` have passed.

### Commands.spawn_scene

```rust
fn spawn_scene(self, scene: Scene, o: SceneOptions) -> SceneInstance
```

Spawns a copy of a scene now; its entities and components exist for the systems after this one.

### Commands.save_scene

```rust
fn save_scene(self, path: string)
```

Writes every entity with a saved component to a scene file, after this system returns.

### Commands.save_prefab

```rust
fn save_prefab(self, path: string, root: Entity)
```

Writes `root` and its children: a prefab of something built in game.

## Lookup

Every component of type `T`, found by entity id. A query walks every match; a lookup answers for one entity, such as the other entity in a collision.

```rust
struct Lookup<T> {
    ids: int[],
    rows: T[],
}
```

```rust
@system
fn hurt(hit: Contact<Bullet, Enemy>, health: Lookup<Health>) {
    if health.get(hit.b) is Some(h) {
        h.hp -= 1;            // a change through a lookup is kept
    }
}
```

### Lookup.get

```rust
fn get(self, e: Entity) -> Option<T>
```

`Some(component)` when the entity has a `T`, `None` otherwise.

```rust
if places.get(hit.b) is Some(t) {
    effects.burst(t.position, ParticleEmitter { .. }, 20);
}
```

### Lookup.has

```rust
fn has(self, e: Entity) -> bool
```

Whether the entity has a `T`.

```rust
if shields.has(hit.a) { return; }
```

### Lookup.len

```rust
fn len(self) -> int
```

How many entities have a `T`.

```rust
log(string(enemies.len()) + " enemies left");
```

## State

The current variant of a `@state` enum `S`. `now` is the variant and `since` the seconds since it last changed. `next` and `changing` belong to `set`. See [States](../concepts/states.md).

```rust
struct State<S> {
    now: S,
    since: float,
    next: S,
    changing: bool,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `now` | `S` | required | The current variant |
| `since` | `float` | required | Seconds since the state last changed |
| `next` | `S` | required | The variant `set` asked for |
| `changing` | `bool` | required | Whether `set` was called this frame |

```rust
@system(in_state: [Phase::Over])
fn restart(input: Input, phase: State<Phase>) {
    if phase.since > 1.0 && input.action_pressed("jump") {
        phase.set(Phase::Playing);
    }
}
```

### State.set

```rust
fn set(self, s: S)
```

Asks for a change of state. It takes effect after this frame's `Update` systems: the `on_exit` systems of the old variant run, then the `on_enter` systems of the new one. When several systems ask in one frame, the last one to run wins; asking for the current variant changes nothing.

```rust
phase.set(Phase::GameOver);
```

## Contact

Two solid colliders started touching: one entity has an `A`, the other a `B`. `a` is the entity with `A` and `b` the one with `B`. Collisions reach systems the frame after the physics step that found them.

```rust
struct Contact<A, B> {
    a: Entity,
    b: Entity,
    point: Vec2 = Vec2 { .. },
    normal: Vec2 = Vec2 { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `a` | `Entity` | required | The entity with an `A` |
| `b` | `Entity` | required | The entity with a `B` |
| `point` | `Vec2` | `Vec2 { .. }` | The first contact point, in the world |
| `normal` | `Vec2` | `Vec2 { .. }` | The contact direction, pointing from `a` to `b` |

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

```rust
struct ContactEnd<A, B> {
    a: Entity,
    b: Entity,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `a` | `Entity` | required | The entity with an `A` |
| `b` | `Entity` | required | The entity with a `B` |

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

```rust
struct Overlap<A, B> {
    a: Entity,
    b: Entity,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `a` | `Entity` | required | The entity with an `A` |
| `b` | `Entity` | required | The entity with a `B` |

```rust
@system
fn collect(hit: Overlap<Player, Coin>, cmd: Commands, score: Score) {
    score.value += 1;
    cmd.despawn(hit.b);
}
```

## OverlapEnd

An entity left a sensor collider.

```rust
struct OverlapEnd<A, B> {
    a: Entity,
    b: Entity,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `a` | `Entity` | required | The entity with an `A` |
| `b` | `Entity` | required | The entity with a `B` |

```rust
@system
fn leave_water(hit: OverlapEnd<Player, Water>, players: Lookup<Player>) {
    if players.get(hit.a) is Some(p) {
        p.swimming = false;
    }
}
```
