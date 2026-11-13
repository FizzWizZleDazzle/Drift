<!-- Written by scripts/candela-reference.py from the drift module and
     bindings/candela/docs/api-notes.md. Edit those, not this page. -->

# Gameplay: drift/gameplay

Health and damage, hit effects, screen bounds, spawners and a saved high score: components most action games write for themselves. Import the module beside `drift`:

```rust
import "drift" { * };
import "drift/gameplay" { * };
```

Each part runs only for entities that have its components. See [Building Blocks](../guides/building-blocks.md).

## Health

Hit points. A hit takes `amount` off unless the entity is still invulnerable from the last one (`invulnerable_for` seconds). At 0 the entity dies: Died is sent, and it is despawned unless `despawn_on_death` is off.

```rust
@component
struct Health {
    hp: float = 100.0,
    max: float = 100.0,
    invulnerable_for: float = 0.0,
    despawn_on_death: bool = true,
    invulnerable: float = 0.0,
    dead: bool = false,
}
```

### Health.fraction

```rust
fn fraction(self) -> float
```

## health

```rust
fn health(hp: float) -> Health
```

## Hitbox

Deals `damage` to the Hurtbox entities it touches on another `team`, through a sensor overlap or a contact. `pierce` off despawns it after its first hit, as a bullet. `knockback` pushes a dynamic body away.

```rust
@component
struct Hitbox {
    damage: float = 10.0,
    team: int = 0,
    pierce: bool = true,
    knockback: float = 0.0,
}
```

## Hurtbox

Takes damage from Hitboxes of other teams.

```rust
@component
struct Hurtbox {
    team: int = 1,
}
```

## DealDamage

Send to hurt an entity with Health; Hitboxes send it for you.

```rust
@message
struct DealDamage {
    target: Entity = Entity { .. },
    amount: float = 0.0,
    source: Entity = Entity { .. },
}
```

## Damaged

Sent after Health took a hit.

```rust
@message
struct Damaged {
    entity: Entity = Entity { .. },
    amount: float = 0.0,
    hp: float = 0.0,
    source: Entity = Entity { .. },
}
```

## Died

Sent when Health reaches 0.

```rust
@message
struct Died {
    entity: Entity = Entity { .. },
    source: Entity = Entity { .. },
}
```

## damage

```rust
fn damage(cmd: Commands, target: Entity, amount: float, source: Entity)
```

## heal

```rust
fn heal(cmd: Commands, target: Entity, amount: float)
```

A negative amount heals, up to max.

## gp_hit

```rust
fn gp_hit(hitter: Entity, target: Entity, hitboxes: Lookup<Hitbox>, hurtboxes: Lookup<Hurtbox>, places: Lookup<Transform>, physics: Physics, cmd: Commands)
```

## gp_hit_overlaps

```rust
@system
fn gp_hit_overlaps(hit: Overlap<Hitbox, Hurtbox>, hitboxes: Lookup<Hitbox>, hurtboxes: Lookup<Hurtbox>, places: Lookup<Transform>, physics: Physics, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## gp_hit_contacts

```rust
@system
fn gp_hit_contacts(hit: Contact<Hitbox, Hurtbox>, hitboxes: Lookup<Hitbox>, hurtboxes: Lookup<Hurtbox>, places: Lookup<Transform>, physics: Physics, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## gp_damage

```rust
@system(after: [gp_hit_overlaps, gp_hit_contacts])
fn gp_damage(d: DealDamage, healths: Lookup<Health>, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## gp_invulnerability

```rust
@system
fn gp_invulnerability(health: (Entity, Health), time: Time)
```

A system the module adds: it runs in every game that imports the module.

## HealthBar

A bar over the entity's head showing its Health, drawn in the world.

```rust
@component
struct HealthBar {
    size: Vec2 = Vec2 { x: 32.0, y: 4.0 },
    offset: Vec2 = Vec2 { x: 0.0, y: -24.0 },
    fill: Color = Color { r: 0.36, g: 0.85, b: 0.45, a: 1.0 },
    back: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: 0.6 },
    hide_when_full: bool = false,
}
```

## gp_health_bars

```rust
@system
fn gp_health_bars(health: (Health, HealthBar, Transform), draw: Draw)
```

A system the module adds: it runs in every game that imports the module.

## HitFlash

Tints the entity's Sprite, Rectangle or Circle with `color` for `seconds` each time its Health takes a hit, or when flash() starts it.

```rust
@component
struct HitFlash {
    color: Color = Color { .. },
    seconds: float = 0.12,
    left: float = 0.0,
    base: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: -1.0 },
}
```

## flash

```rust
fn flash(cmd: Commands, e: Entity, f: HitFlash)
```

## gp_flash_on_hit

```rust
@system(after: [gp_damage])
fn gp_flash_on_hit(d: Damaged, flashes: Lookup<HitFlash>)
```

A system the module adds: it runs in every game that imports the module.

## flash_color

```rust
fn flash_color(f: HitFlash, current: Color, dt: float) -> Color
```

## gp_flash_sprites

```rust
@system(after: [gp_flash_on_hit])
fn gp_flash_sprites(hit_flash: (HitFlash, Sprite), time: Time)
```

A system the module adds: it runs in every game that imports the module.

## gp_flash_rects

```rust
@system(after: [gp_flash_on_hit])
fn gp_flash_rects(hit_flash: (HitFlash, Rectangle), time: Time)
```

A system the module adds: it runs in every game that imports the module.

## gp_flash_circles

```rust
@system(after: [gp_flash_on_hit])
fn gp_flash_circles(hit_flash: (HitFlash, Circle), time: Time)
```

A system the module adds: it runs in every game that imports the module.

## HitStop

Send to freeze the game for `seconds` of real time: a hit stop.

```rust
@message
struct HitStop {
    seconds: float = 0.06,
}
```

## Shake

Send to shake every camera.

```rust
@message
struct Shake {
    intensity: float = 0.4,
}
```

## HitStopClock

```rust
@resource
struct HitStopClock {
    left: float = 0.0,
}
```

## gp_hit_stop

```rust
@system
fn gp_hit_stop(stop: HitStop, clock: HitStopClock, time: Time)
```

A system the module adds: it runs in every game that imports the module.

## gp_hit_stop_clock

```rust
@system
fn gp_hit_stop_clock(clock: HitStopClock, time: Time)
```

A system the module adds: it runs in every game that imports the module.

## gp_shake

```rust
@system
fn gp_shake(s: Shake, cameras: (Entity, Camera)[], effects: Effects)
```

A system the module adds: it runs in every game that imports the module.

## FadeOut

Fades the entity's Sprite, Text, Rectangle or Circle out over `seconds`, then despawns it.

```rust
@component
struct FadeOut {
    seconds: float = 0.6,
    left: float = -1.0,
    alpha: float = -1.0,
}
```

## fade_step

```rust
fn fade_step(f: FadeOut, a: float, dt: float, e: Entity, cmd: Commands) -> float
```

## gp_fade_sprites

```rust
@system
fn gp_fade_sprites(fade_out: (Entity, FadeOut, Sprite), time: Time, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## gp_fade_texts

```rust
@system
fn gp_fade_texts(fade_out: (Entity, FadeOut, Text), time: Time, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## gp_fade_rects

```rust
@system
fn gp_fade_rects(fade_out: (Entity, FadeOut, Rectangle), time: Time, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## gp_fade_circles

```rust
@system
fn gp_fade_circles(fade_out: (Entity, FadeOut, Circle), time: Time, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## popup

```rust
fn popup(cmd: Commands, pos: Vec2, text: string, c: Color) -> Entity
```

A number or word that pops up at a world position, floats up and fades: damage numbers, "+100", "Combo!".

## bounds_or_view

```rust
fn bounds_or_view(area: Rect, camera: Camera2D) -> Rect
```

The part of the world the main camera shows, or `area` when it is set.

## KeepInBounds

Keeps the entity inside `area` (the camera's view when unset), `margin` pixels in, and stops its Velocity at the edge.

```rust
@component
struct KeepInBounds {
    area: Rect = Rect { .. },
    margin: float = 0.0,
}
```

## gp_keep_in_bounds

```rust
@system
fn gp_keep_in_bounds(keep_in_bounds: (KeepInBounds, Transform), camera: Camera2D)
```

A system the module adds: it runs in every game that imports the module.

## gp_keep_in_bounds_velocity

```rust
@system(after: [gp_keep_in_bounds])
fn gp_keep_in_bounds_velocity(keep_in_bounds: (KeepInBounds, Transform, Velocity), camera: Camera2D)
```

A system the module adds: it runs in every game that imports the module.

## WrapAround

Moves the entity to the other side when it leaves `area` (the camera's view when unset) by more than `margin` pixels: asteroids-style wrapping.

```rust
@component
struct WrapAround {
    area: Rect = Rect { .. },
    margin: float = 16.0,
}
```

## gp_wrap_around

```rust
@system
fn gp_wrap_around(wrap_around: (WrapAround, Transform), camera: Camera2D)
```

A system the module adds: it runs in every game that imports the module.

## DespawnOffscreen

Despawns the entity once it is more than `margin` pixels outside `area` (the camera's view when unset): bullets, debris, scrolled-past enemies.

```rust
@component
struct DespawnOffscreen {
    area: Rect = Rect { .. },
    margin: float = 64.0,
}
```

## gp_despawn_offscreen

```rust
@system
fn gp_despawn_offscreen(despawn_offscreen: (Entity, DespawnOffscreen, Transform), camera: Camera2D, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## SpawnTick

Sent each time a Spawner fires: spawn what you want at `position`.

```rust
@message
struct SpawnTick {
    spawner: Entity = Entity { .. },
    id: string = "",
    position: Vec2 = Vec2 { .. },
    index: int = 0,
}
```

## Spawner

Fires SpawnTick every `every` seconds at the entity's position plus a random offset up to `spread`, `count` times in all (-1 forever).

```rust
@component
struct Spawner {
    id: string = "",
    every: float = 1.0,
    count: int = -1,
    spread: float = 0.0,
    left: float = 0.0,
    fired: int = 0,
    paused: bool = false,
}
```

## spawner

```rust
fn spawner(id: string, every: float) -> Spawner
```

## SpawnRng

```rust
@resource
struct SpawnRng {
    rng: Rng = rng(9127),
}
```

## gp_spawners

```rust
@system
fn gp_spawners(spawner: (Entity, Spawner, Transform), time: Time, r: SpawnRng, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## Wave

One wave: `count` spawns of `id`, `every` seconds apart, starting `delay` seconds after the wave before it ends.

```rust
struct Wave {
    id: string = "",
    count: int = 5,
    every: float = 1.0,
    delay: float = 2.0,
}
```

## wave

```rust
fn wave(id: string, count: int, every: float, delay: float) -> Wave
```

## WaveSpawn

Sent for each spawn of a wave.

```rust
@message
struct WaveSpawn {
    wave: int = 0,
    id: string = "",
    index: int = 0,
}
```

## WaveStarted

Sent when a wave starts and when the last wave has spawned everything.

```rust
@message
struct WaveStarted {
    wave: int = 0,
}
```

## WavesDone

```rust
@message
struct WavesDone {
    waves: int = 0,
}
```

## WaveSpawner

Runs a list of waves: put it on an entity with the waves in order. It sends WaveSpawn for every spawn; `loop` starts over after the last.

```rust
@component
struct WaveSpawner {
    waves: Wave[] = [],
    looping: bool = false,
    wave: int = 0,
    spawned: int = 0,
    left: float = -1.0,
    done: bool = false,
}
```

## gp_wave_spawners

```rust
@system
fn gp_wave_spawners(wave_spawner: (Entity, WaveSpawner), time: Time, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## Score

The score and the best one, kept in the save under `key`. The best is read when the game starts and written whenever it is beaten.

```rust
@resource
struct Score {
    value: int = 0,
    best: int = 0,
    key: string = "best",
    loaded: bool = false,
    saved: int = 0,
}
```

### Score.add

```rust
fn add(self, points: int)
```

### Score.reset

```rust
fn reset(self)
```

### Score.is_record

```rust
fn is_record(self) -> bool
```

Whether this run beat the best that was saved when it started.

## gp_score

```rust
@system
fn gp_score(score: Score, save: Save, time: Time)
```

A system the module adds: it runs in every game that imports the module.
