<!-- Written by scripts/candela-reference.py from the doc comments in
     bindings/candela/drift/src. Edit those, not this page. -->

# Gameplay: drift/gameplay

Health and damage, hit effects, screen bounds, spawners and a saved high score: components
most action games write for themselves. Import the module beside `drift`:

```rust
import "drift" { * };
import "drift/gameplay" { * };
```

Each part runs only for entities that have its components. See [Building
Blocks](../guides/building-blocks.md).

## Health

Hit points that DealDamage messages and Hitboxes take away.

A hit takes its amount off unless the entity is still invulnerable from the last one
(`invulnerable_for` seconds). At 0 the entity dies: Died is sent, and it is despawned unless
`despawn_on_death` is off. A dead entity ignores every later hit and heal. Each hit that
lands sends Damaged.

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

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `hp` | `float` | `100.0` | Hit points left, from 0 to `max`. |
| `max` | `float` | `100.0` | The most hit points a heal restores to. |
| `invulnerable_for` | `float` | `0.0` | Seconds of game time after a hit during which further hits do nothing; 0 lets every hit land. |
| `despawn_on_death` | `bool` | `true` | Despawns the entity when it dies; off keeps it, for a death animation or a respawn. |
| `invulnerable` | `float` | `0.0` | Seconds of invulnerability left; the module counts it down. |
| `dead` | `bool` | `false` | Set when `hp` reaches 0; a dead entity takes no more damage or healing. |

```rust
cmd.spawn([at(100.0, 200.0), health(30.0), Hurtbox { team: 1 }, BoxCollider { .. }]);
```

### Health.fraction

```rust
fn fraction(self) -> float
```

Hit points as a share of `max`, from 0.0 to 1.0; 0.0 when `max` is 0 or less.

```rust
let low = h.fraction() < 0.25;
```

## health

```rust
fn health(hp: float) -> Health
```

A full Health with `hp` hit points.

```rust
cmd.spawn([at(x, y), health(50.0), Hurtbox { .. }]);
```

## Hitbox

Deals `damage` to the Hurtbox entities it touches on another `team`, through a sensor
overlap or a contact.

Both entities need a collider for the touch to be seen. `pierce` off despawns it after its
first hit, as a bullet. `knockback` pushes the target away from it with an impulse, which
moves only a dynamic body.

```rust
@component
struct Hitbox {
    damage: float = 10.0,
    team: int = 0,
    pierce: bool = true,
    knockback: float = 0.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `damage` | `float` | `10.0` | Hit points each hit takes away. |
| `team` | `int` | `0` | Its side; it hurts only Hurtboxes whose team differs. |
| `pierce` | `bool` | `true` | Keeps it after a hit; off despawns it after its first hit. |
| `knockback` | `float` | `0.0` | Strength of the impulse that pushes the target away from it; 0 pushes nothing. |

```rust
cmd.spawn([at(x, y), Hitbox { damage: 5.0, team: 0, pierce: false, .. },
           CircleCollider { radius: 3.0, sensor: true, .. }, Velocity { linear: vec2(400.0, 0.0), .. }]);
```

## Hurtbox

Takes damage from Hitboxes of other teams.

The entity needs a collider for a hit to be seen, and Health for the hit to count.

```rust
@component
struct Hurtbox {
    team: int = 1,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `team` | `int` | `1` | Its side; Hitboxes of the same team pass through it. |

```rust
cmd.spawn([at(x, y), health(20.0), Hurtbox { team: 1 }, BoxCollider { .. }]);
```

## DealDamage

A message that hurts an entity with Health, or heals it with a negative amount.

Hitboxes send it for you; damage() and heal() send it from your systems.

```rust
@message
struct DealDamage {
    target: Entity = Entity { .. },
    amount: float = 0.0,
    source: Entity = Entity { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `target` | `Entity` | `Entity { .. }` | The entity to hurt. |
| `amount` | `float` | `0.0` | Hit points to take away; below 0 it heals, up to `max`. |
| `source` | `Entity` | `Entity { .. }` | Who dealt it, passed on in Damaged and Died; no entity when nobody did. |

## Damaged

Sent after Health took a hit that landed.

A hit during invulnerability, a heal and a hit on a dead entity send nothing.

```rust
@message
struct Damaged {
    entity: Entity = Entity { .. },
    amount: float = 0.0,
    hp: float = 0.0,
    source: Entity = Entity { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `entity` | `Entity` | `Entity { .. }` | The entity that was hit. |
| `amount` | `float` | `0.0` | The damage dealt. |
| `hp` | `float` | `0.0` | Hit points left after the hit. |
| `source` | `Entity` | `Entity { .. }` | Who dealt it. |

```rust
@system
fn ouch(hit: Damaged) {
    log(format("hit for {}, {} left", [hit.amount, hit.hp]));
}
```

## Died

Sent once, when an entity's Health reaches 0.

```rust
@message
struct Died {
    entity: Entity = Entity { .. },
    source: Entity = Entity { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `entity` | `Entity` | `Entity { .. }` | The entity that died; despawned in the same frame unless `despawn_on_death` is off. |
| `source` | `Entity` | `Entity { .. }` | Who dealt the last hit. |

## damage

```rust
fn damage(cmd: Commands, target: Entity, amount: float, source: Entity)
```

Sends DealDamage: hurts `target` by `amount` hit points, dealt by `source`.

```rust
damage(cmd, enemy, 25.0, player);
```

## heal

```rust
fn heal(cmd: Commands, target: Entity, amount: float)
```

Heals `target` by `amount` hit points, up to its Health's `max`.

A heal ignores invulnerability; a dead entity is not healed.

```rust
heal(cmd, player, 10.0);
```

## gp_hit

```rust
fn gp_hit(hitter: Entity, target: Entity, hitboxes: Lookup<Hitbox>, hurtboxes: Lookup<Hurtbox>, places: Lookup<Transform>, physics: Physics, cmd: Commands)
```

Applies one touch between a Hitbox and a Hurtbox: sends DealDamage unless both are on the
same team, pushes the target by `knockback`, and despawns a Hitbox that does not pierce.

## gp_hit_overlaps

```rust
@system
fn gp_hit_overlaps(hit: Overlap<Hitbox, Hurtbox>, hitboxes: Lookup<Hitbox>, hurtboxes: Lookup<Hurtbox>, places: Lookup<Transform>, physics: Physics, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Turns each sensor overlap between a Hitbox and a Hurtbox into a hit.

## gp_hit_contacts

```rust
@system
fn gp_hit_contacts(hit: Contact<Hitbox, Hurtbox>, hitboxes: Lookup<Hitbox>, hurtboxes: Lookup<Hurtbox>, places: Lookup<Transform>, physics: Physics, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Turns each contact between a Hitbox and a Hurtbox into a hit.

## gp_damage

```rust
@system(after: [gp_hit_overlaps, gp_hit_contacts])
fn gp_damage(d: DealDamage, healths: Lookup<Health>, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Applies each DealDamage to its target's Health: heals, or takes hit points off and starts
the invulnerability, and sends Damaged and Died.

## gp_invulnerability

```rust
@system
fn gp_invulnerability(health: (Entity, Health), time: Time)
```

A system the module adds: it runs in every game that imports the module.

Counts down each Health's invulnerability in game time.

## HealthBar

A bar over the entity's head showing its Health, drawn in the world.

The entity needs Health and a Transform. The bar is drawn with Draw every frame and ignores
the entity's rotation and scale.

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

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `size` | `Vec2` | `Vec2 { x: 32.0, y: 4.0 }` | Width and height of the bar in pixels. |
| `offset` | `Vec2` | `Vec2 { x: 0.0, y: -24.0 }` | Where the bar's center sits from the entity's position, in pixels; negative y is above. |
| `fill` | `Color` | `Color { r: 0.36, g: 0.85, b: 0.45, a: 1.0 }` | Color of the part that shows the hit points left. |
| `back` | `Color` | `Color { r: 0.0, g: 0.0, b: 0.0, a: 0.6 }` | Color behind it, one pixel larger on every side. |
| `hide_when_full` | `bool` | `false` | Draws nothing while Health is full. |

```rust
cmd.spawn([at(x, y), health(40.0), HealthBar { hide_when_full: true, .. }]);
```

## gp_health_bars

```rust
@system
fn gp_health_bars(health: (Health, HealthBar, Transform), draw: Draw)
```

A system the module adds: it runs in every game that imports the module.

Draws every HealthBar.

## HitFlash

Tints the entity's Sprite, Rectangle or Circle with `color` for `seconds` each time its
Health takes a hit, or when flash_entity() starts it.

The color is replaced, not mixed, and put back when the flash ends. The flash counts real
time, so it plays out during a hit stop.

```rust
@component
struct HitFlash {
    color: Color = Color { .. },
    seconds: float = 0.12,
    left: float = 0.0,
    base: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: -1.0 },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `color` | `Color` | `Color { .. }` | The color shown during the flash. |
| `seconds` | `float` | `0.12` | How long one flash lasts, in seconds of real time. |
| `left` | `float` | `0.0` | Seconds of the current flash left; 0 when none is showing. |
| `base` | `Color` | `Color { r: 0.0, g: 0.0, b: 0.0, a: -1.0 }` | The color to put back, kept while a flash shows; alpha -1 when none is kept. |

```rust
cmd.spawn([at(x, y), Sprite { image: art.enemy, .. }, health(10.0), HitFlash { .. }]);
```

## flash_entity

```rust
fn flash_entity(cmd: Commands, e: Entity, f: HitFlash)
```

Gives the entity `f` and starts its flash now, without a hit.

```rust
flash_entity(cmd, player, HitFlash { color: rgb(255, 255, 255), .. });
```

## gp_flash_on_hit

```rust
@system(after: [gp_damage])
fn gp_flash_on_hit(d: Damaged, flashes: Lookup<HitFlash>)
```

A system the module adds: it runs in every game that imports the module.

Starts the HitFlash of every entity that took a hit, unless one is already showing.

## flash_color

```rust
fn flash_color(f: HitFlash, current: Color, dt: float) -> Color
```

The color a flashing shape shows this frame: `f.color` while the flash lasts, then the
color it had before. Counts the flash down by `dt` seconds.

## gp_flash_sprites

```rust
@system(after: [gp_flash_on_hit])
fn gp_flash_sprites(hit_flash: (HitFlash, Sprite), time: Time)
```

A system the module adds: it runs in every game that imports the module.

Shows HitFlash on Sprites.

## gp_flash_rects

```rust
@system(after: [gp_flash_on_hit])
fn gp_flash_rects(hit_flash: (HitFlash, Rectangle), time: Time)
```

A system the module adds: it runs in every game that imports the module.

Shows HitFlash on Rectangles.

## gp_flash_circles

```rust
@system(after: [gp_flash_on_hit])
fn gp_flash_circles(hit_flash: (HitFlash, Circle), time: Time)
```

A system the module adds: it runs in every game that imports the module.

Shows HitFlash on Circles.

## HitStop

Send to freeze the game for `seconds` of real time: a hit stop.

It sets the time scale to 0, and back to 1 when the time is up, so it ends any slow motion
the game set with `Time.set_scale`. Two stops at once last as long as the longer one.

```rust
@message
struct HitStop {
    seconds: float = 0.06,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `seconds` | `float` | `0.06` | How long the game stays frozen, in seconds of real time. |

```rust
cmd.send(HitStop { seconds: 0.08 });
```

## Shake

Send to shake every camera.

```rust
@message
struct Shake {
    intensity: float = 0.4,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `intensity` | `float` | `0.4` | How far the cameras move at first, in pixels; the shake dies away by itself. |

```rust
cmd.send(Shake { intensity: 6.0 });
```

## HitStopClock

The real time a HitStop has left; the module keeps it.

```rust
@resource
struct HitStopClock {
    left: float = 0.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `left` | `float` | `0.0` | Seconds of real time until the game runs again; 0 when no stop is running. |

## gp_hit_stop

```rust
@system
fn gp_hit_stop(stop: HitStop, clock: HitStopClock, time: Time)
```

A system the module adds: it runs in every game that imports the module.

Starts a HitStop: freezes game time and sets the clock.

## gp_hit_stop_clock

```rust
@system
fn gp_hit_stop_clock(clock: HitStopClock, time: Time)
```

A system the module adds: it runs in every game that imports the module.

Counts a running HitStop down in real time and sets the time scale back to 1 at its end.

## gp_shake

```rust
@system
fn gp_shake(s: Shake, cameras: (Entity, Camera)[], effects: Effects)
```

A system the module adds: it runs in every game that imports the module.

Shakes every Camera for each Shake message.

## FadeOut

Fades the entity's Sprite, Text, Rectangle or Circle out over `seconds`, then despawns it.

The fade starts from the alpha the shape has when the fade begins, and counts game time.

```rust
@component
struct FadeOut {
    seconds: float = 0.6,
    left: float = -1.0,
    alpha: float = -1.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `seconds` | `float` | `0.6` | How long the fade takes, in seconds of game time. |
| `left` | `float` | `-1.0` | Seconds left; below 0 until the fade starts. |
| `alpha` | `float` | `-1.0` | The alpha the fade started from; below 0 until it starts. |

```rust
cmd.insert(e, FadeOut { seconds: 1.0, .. });
```

## fade_step

```rust
fn fade_step(f: FadeOut, a: float, dt: float, e: Entity, cmd: Commands) -> float
```

The alpha a fading shape shows this frame, from its starting alpha `a` down to 0. Counts
`f` down by `dt` seconds and despawns `e` when it reaches 0.

## gp_fade_sprites

```rust
@system
fn gp_fade_sprites(fade_out: (Entity, FadeOut, Sprite), time: Time, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Fades out Sprites that have FadeOut.

## gp_fade_texts

```rust
@system
fn gp_fade_texts(fade_out: (Entity, FadeOut, Text), time: Time, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Fades out Texts, with their shadow and outline that have FadeOut.

## gp_fade_rects

```rust
@system
fn gp_fade_rects(fade_out: (Entity, FadeOut, Rectangle), time: Time, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Fades out Rectangles that have FadeOut.

## gp_fade_circles

```rust
@system
fn gp_fade_circles(fade_out: (Entity, FadeOut, Circle), time: Time, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Fades out Circles that have FadeOut.

## popup

```rust
fn popup(cmd: Commands, pos: Vec2, text: string, c: Color) -> Entity
```

A number or word that pops up at a world position, floats up and fades: damage numbers,
"+100", "Combo!".

The text is 22 pixels, centered, in color `c` with an outline. It grows in over 0.15
seconds, rises at 70 pixels per second and fades out over 0.8 seconds, then is despawned.
Returns its entity.

```rust
popup(cmd, t.position, "+100", rgb(255, 220, 80));
```

## bounds_or_view

```rust
fn bounds_or_view(area: Rect, camera: Camera2D) -> Rect
```

The part of the world the main camera shows, or `area` when its width and height are above
0.

```rust
let r = bounds_or_view(Rect { .. }, camera);   // the camera's view
```

## KeepInBounds

Keeps the entity inside `area` (the camera's view when unset), `margin` pixels in, and stops
its Velocity at the edge.

It clamps the entity's position, not its collider: set `margin` to half the entity's size
to keep all of it inside.

```rust
@component
struct KeepInBounds {
    area: Rect = Rect { .. },
    margin: float = 0.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `area` | `Rect` | `Rect { .. }` | The world rect to stay in; a zero width or height uses the main camera's view. |
| `margin` | `float` | `0.0` | How far inside the edges the position stays, in pixels. |

```rust
cmd.spawn([at(320.0, 400.0), Sprite { image: art.ship, .. }, KeepInBounds { margin: 16.0, .. }]);
```

## gp_keep_in_bounds

```rust
@system
fn gp_keep_in_bounds(keep_in_bounds: (KeepInBounds, Transform), camera: Camera2D)
```

A system the module adds: it runs in every game that imports the module.

Clamps the position of every entity with KeepInBounds.

## gp_keep_in_bounds_velocity

```rust
@system(after: [gp_keep_in_bounds])
fn gp_keep_in_bounds_velocity(keep_in_bounds: (KeepInBounds, Transform, Velocity), camera: Camera2D)
```

A system the module adds: it runs in every game that imports the module.

Zeroes the Velocity of a KeepInBounds entity on each axis where it moves out past an edge.

## WrapAround

Moves the entity to the other side when it leaves `area` (the camera's view when unset) by
more than `margin` pixels: asteroids-style wrapping.

```rust
@component
struct WrapAround {
    area: Rect = Rect { .. },
    margin: float = 16.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `area` | `Rect` | `Rect { .. }` | The world rect to wrap around; a zero width or height uses the main camera's view. |
| `margin` | `float` | `16.0` | How far past an edge the position goes before it wraps, in pixels; about half the entity's size lets it leave the view before it comes back. |

```rust
cmd.spawn([at(200.0, 200.0), Sprite { image: art.rock, .. }, WrapAround { .. }]);
```

## gp_wrap_around

```rust
@system
fn gp_wrap_around(wrap_around: (WrapAround, Transform), camera: Camera2D)
```

A system the module adds: it runs in every game that imports the module.

Wraps every entity with WrapAround.

## DespawnOffscreen

Despawns the entity once it is more than `margin` pixels outside `area` (the camera's view
when unset): bullets, debris, scrolled-past enemies.

It checks every frame, so an entity spawned outside the area is despawned at once.

```rust
@component
struct DespawnOffscreen {
    area: Rect = Rect { .. },
    margin: float = 64.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `area` | `Rect` | `Rect { .. }` | The world rect to stay near; a zero width or height uses the main camera's view. |
| `margin` | `float` | `64.0` | How far outside the area the position may go before the entity is despawned, in pixels. |

```rust
cmd.spawn([at(x, y), Circle { radius: 3.0, .. }, Velocity { linear: vec2(0.0, -500.0), .. },
           DespawnOffscreen { .. }]);
```

## gp_despawn_offscreen

```rust
@system
fn gp_despawn_offscreen(despawn_offscreen: (Entity, DespawnOffscreen, Transform), camera: Camera2D, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Despawns every DespawnOffscreen entity that is past its margin.

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

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `spawner` | `Entity` | `Entity { .. }` | The entity whose Spawner fired. |
| `id` | `string` | `""` | The Spawner's `id`, to tell spawners apart. |
| `position` | `Vec2` | `Vec2 { .. }` | Where to spawn, in the world: the spawner's position plus its random offset. |
| `index` | `int` | `0` | How many times this Spawner fired before, counting from 0. |

```rust
@system
fn spawn_bats(tick: SpawnTick, cmd: Commands) {
    if tick.id == "bat" {
        cmd.spawn([at(tick.position.x, tick.position.y), Bat {}]);
    }
}
```

## Spawner

Fires SpawnTick every `every` seconds at the entity's position plus a random offset up to
`spread`, `count` times in all (-1 forever).

The entity needs a Transform. It counts game time, so a time scale of 0 pauses it, and a
long frame fires every tick it covered.

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

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `string` | `""` | A name passed on in each SpawnTick. |
| `every` | `float` | `1.0` | Seconds between ticks; anything under 0.01 counts as 0.01. |
| `count` | `int` | `-1` | How many ticks in all; -1 keeps firing forever. |
| `spread` | `float` | `0.0` | The largest random offset from the entity's position, in pixels, picked inside a circle; 0 is none. |
| `left` | `float` | `0.0` | Seconds until the next tick; 0 fires on the first frame. |
| `fired` | `int` | `0` | Ticks fired so far. |
| `paused` | `bool` | `false` | Stops the ticks while on, keeping the count and the time left. |

```rust
cmd.spawn([at(600.0, 100.0), spawner("bat", 2.0)]);
```

## spawner

```rust
fn spawner(id: string, every: float) -> Spawner
```

A Spawner named `id` that fires every `every` seconds forever, the first tick one interval
after it is added.

```rust
cmd.spawn([at(0.0, 300.0), spawner("coin", 1.5)]);
```

## SpawnRng

The random numbers Spawners pick their offsets with, seeded so every run repeats them.

```rust
@resource
struct SpawnRng {
    rng: Rng = rng(9127),
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `rng` | `Rng` | `rng(9127)` | The generator, seeded with 9127. |

## gp_spawners

```rust
@system
fn gp_spawners(spawner: (Entity, Spawner, Transform), time: Time, r: SpawnRng, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Fires every Spawner that is due.

## Wave

One wave of a WaveSpawner: `count` spawns of `id`, `every` seconds apart, starting `delay`
seconds after the wave before it ends.

The first wave starts `delay` seconds after the WaveSpawner is added.

```rust
struct Wave {
    id: string = "",
    count: int = 5,
    every: float = 1.0,
    delay: float = 2.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `string` | `""` | A name passed on in each WaveSpawn. |
| `count` | `int` | `5` | How many spawns the wave has. |
| `every` | `float` | `1.0` | Seconds between its spawns; anything under 0.01 counts as 0.01. |
| `delay` | `float` | `2.0` | Seconds of waiting before its first spawn. |

```rust
let w = Wave { id: "slime", count: 8, every: 0.5, .. };
```

## wave

```rust
fn wave(id: string, count: int, every: float, delay: float) -> Wave
```

A Wave of `count` spawns of `id`, `every` seconds apart, `delay` seconds after the wave
before it.

```rust
cmd.spawn([WaveSpawner { waves: [wave("bat", 5, 1.0, 2.0), wave("ogre", 2, 3.0, 4.0)], .. }]);
```

## WaveSpawn

Sent for each spawn of a wave: spawn what you want for it.

```rust
@message
struct WaveSpawn {
    wave: int = 0,
    id: string = "",
    index: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `wave` | `int` | `0` | Which wave, counting from 0. |
| `id` | `string` | `""` | The wave's `id`. |
| `index` | `int` | `0` | Which spawn of the wave, counting from 0. |

```rust
@system
fn spawn_wave(s: WaveSpawn, cmd: Commands) {
    cmd.spawn([at(640.0, 100.0 + 40.0 * float(s.index)), Enemy {}]);
}
```

## WaveStarted

Sent when a wave starts, just before its first WaveSpawn.

```rust
@message
struct WaveStarted {
    wave: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `wave` | `int` | `0` | Which wave, counting from 0. |

## WavesDone

Sent when the last wave has spawned everything; a looping WaveSpawner never sends it.

```rust
@message
struct WavesDone {
    waves: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `waves` | `int` | `0` | How many waves there were. |

## WaveSpawner

Runs a list of waves: put it on an entity with the waves in order. It sends WaveSpawn for
every spawn; `looping` starts over after the last.

It counts game time and needs no Transform.

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

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `waves` | `Wave[]` | `[]` | The waves, in the order they run. |
| `looping` | `bool` | `false` | Starts over at the first wave after the last. |
| `wave` | `int` | `0` | The wave running now, counting from 0. |
| `spawned` | `int` | `0` | Spawns of the current wave sent so far. |
| `left` | `float` | `-1.0` | Seconds until the next spawn; below 0 until the first wave's delay starts. |
| `done` | `bool` | `false` | Set after the last wave of a WaveSpawner that does not loop. |

```rust
cmd.spawn([WaveSpawner { waves: [wave("bat", 5, 1.0, 2.0), wave("ogre", 2, 3.0, 4.0)], .. }]);
```

## gp_wave_spawners

```rust
@system
fn gp_wave_spawners(wave_spawner: (Entity, WaveSpawner), time: Time, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Runs every WaveSpawner.

## Score

The score and the best one, kept in the save under `key`. The best is read when the game
starts and written whenever it is beaten.

The best goes to the default save slot and is flushed in the frame it rises.

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

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `value` | `int` | `0` | The score of this run. |
| `best` | `int` | `0` | The best score: the saved one, or this run's once it is higher. |
| `key` | `string` | `"best"` | The save key the best is kept under. |
| `loaded` | `bool` | `false` | Set once the saved best has been read. |
| `saved` | `int` | `0` | The best last written to the save. |

```rust
@system
fn collect(hit: Overlap<Player, Coin>, score: Score, cmd: Commands) {
    score.add(10);
    cmd.despawn(hit.b);
}
```

### Score.add

```rust
fn add(self, points: int)
```

Adds `points` to the score, and raises the best when the score passes it.

```rust
score.add(100);
```

### Score.reset

```rust
fn reset(self)
```

Sets the score back to 0 for a new run; the best stays.

```rust
score.reset();
```

### Score.is_record

```rust
fn is_record(self) -> bool
```

Whether this run's score is above 0 and the best so far, counting a tie with the saved
best.

```rust
let line = if score.is_record() { "New best!" } else { format("Best {}", [score.best]) };
```

## gp_score

```rust
@system
fn gp_score(score: Score, save: Save, time: Time)
```

A system the module adds: it runs in every game that imports the module.

Reads the saved best once, keeps `best` up to date, and saves it whenever it rises.
