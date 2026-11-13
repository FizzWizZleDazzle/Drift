# Building Blocks

Most action games write the same pieces: health and damage, bullets that leave the screen, enemies that come in waves, a high score. `drift/gameplay` has them as components with their systems, so a game adds a component instead of writing the system:

```rust
import "drift" { * };
import "drift/gameplay" { * };
```

Each part only runs for entities that have its components; the ones a game does not use cost nothing. `drift/control` has movement controllers, and `drift/ui` the HUD widgets in [HUD and Menus](hud.md).

## Prefabs

A prefab is a function that returns a bundle. Give it parameters for what changes between copies, and add or replace components with `plus`:

```rust
fn enemy(pos: Vec2) -> any[] {
    return [
        Transform { position: pos, .. },
        Sprite { image: texture("assets/bat.png"), .. },
        RigidBody { kind: BodyKind::Kinematic, .. },
        CircleCollider { radius: 10.0, sensor: true, .. },
        health(30.0), Hurtbox { team: 1 }, Hitbox { damage: 10.0, team: 1, .. }, HitFlash { .. },
    ];
}

cmd.spawn(enemy(vec2(100.0, 40.0)));
cmd.spawn(plus(enemy(vec2(300.0, 40.0)), [Boss {}, health(500.0)]));   // a later Health replaces the first
```

## Health and damage

`Health` holds hit points. A `Hitbox` hurts the `Hurtbox` entities it touches on another `team`, through a sensor overlap or a contact; at least one of the two needs a dynamic body, as for any collision. Damage from code goes through `damage(cmd, target, amount, source)`, and `heal(cmd, target, amount)` heals up to `max`.

```rust
cmd.spawn([at(480.0, 270.0), Rectangle { .. },
           RigidBody { kind: BodyKind::Dynamic, fixed_rotation: true, .. }, BoxCollider { .. },
           Player {}, Health { hp: 5.0, max: 5.0, invulnerable_for: 1.0, .. }, Hurtbox { team: 0 }]);

// A bullet: gone after its first hit.
cmd.spawn([Transform { position: muzzle, .. }, Circle { radius: 3.0, .. }, Velocity { linear: dir * 600.0, .. },
           RigidBody { kind: BodyKind::Kinematic, .. }, CircleCollider { radius: 3.0, sensor: true, .. },
           Hitbox { damage: 1.0, team: 0, pierce: false, .. }, DespawnOffscreen { .. }]);
```

- After a hit, `Health` ignores damage for `invulnerable_for` seconds: invulnerability frames.
- A hit sends `Damaged { entity, amount, hp, source }`. At 0 hp it sends `Died { entity, source }`, and despawns the entity unless `despawn_on_death` is off.
- `knockback` on a `Hitbox` pushes the target's body away from it.
- `HealthBar` draws a small bar over the entity's head from its `Health`; `hide_when_full` hides it at full health.

```rust
@system
fn on_death(d: Died, score: Score) {
    score.add(100);
}
```

## Hit effects

- `HitFlash { color, seconds }` tints the entity's `Sprite`, `Rectangle` or `Circle` each time its `Health` takes a hit. `flash(cmd, e, HitFlash { .. })` starts one from code.
- `cmd.send(HitStop { seconds: 0.06 })` freezes the game for that long: physics, tweens, particles and `dt` stop, then carry on. It uses `Time.set_scale`.
- `cmd.send(Shake { intensity: 0.5 })` shakes every camera.
- `popup(cmd, pos, "-12", rgb(255, 90, 90))` puts a number or word at a world position that pops in, floats up and fades: damage numbers and pickups.
- `FadeOut { seconds }` fades the entity's `Sprite`, `Text`, `Rectangle` or `Circle` and then despawns it.
- `Trail` (see [Particles, Lights and Effects](effects.md)) draws a ribbon behind a fast object.

```rust
@system
fn juice(d: Damaged, places: Lookup<Transform>, cmd: Commands) {
    if places.get(d.entity) is Some(t) {
        popup(cmd, t.position, "-" + string(int(d.amount)), rgb(255, 90, 90));
    }
    cmd.send(Shake { intensity: 0.3 });
    cmd.send(HitStop { seconds: 0.05 });
}
```

## Screen bounds

The area is the main camera's view unless the component sets one.

- `KeepInBounds { margin }` keeps the entity inside the area.
- `WrapAround { margin }` moves it to the other side when it leaves: asteroids-style.
- `DespawnOffscreen { margin }` despawns it once it is `margin` pixels outside: bullets, debris, enemies the player scrolled past.

## Following and orbiting

`Follow` and `Orbit` are engine behaviors in `drift` itself; see [Animation, Tweens and Behaviors](animation.md).

```rust
cmd.spawn([at(0.0, 0.0), Circle { radius: 6.0, .. }, Orbit { center: player, radius: 50.0, speed: 3.0, .. }]);
```

## Spawners and waves

A `Spawner` sends `SpawnTick { spawner, id, position, index }` every `every` seconds from its entity's position plus up to `spread` pixels, `count` times (-1 forever). Spawn what you want in a system that takes the message:

```rust
cmd.spawn([at(900.0, 100.0), spawner("bat", 0.8)]);

@system
fn spawn_bats(s: SpawnTick, cmd: Commands) {
    cmd.spawn(enemy(s.position));
}
```

A `WaveSpawner` runs a list of `Wave`s in order: each spawns `count` times, `every` seconds apart, `delay` seconds after the one before. It sends `WaveStarted { wave }`, `WaveSpawn { wave, id, index }` for every spawn and `WavesDone` after the last wave; `looping` starts over instead.

```rust
cmd.spawn([WaveSpawner { waves: [wave("bat", 5, 0.6, 1.0), wave("bat", 10, 0.4, 3.0), wave("boss", 1, 0.0, 4.0)], .. }]);
```

## Score and high score

`Score` is a resource with the score and the best one. The best is read from the save when the game starts and written whenever it is beaten, under `key` (`"best"`).

```rust
score.add(10);      // value and, when beaten, best
score.reset();      // a new run
score.is_record()   // this run is the best so far
```

Show them with `bind("Score.value", "Score {}")` and `bind("Score.best", "Best {}")` from [HUD and Menus](hud.md).

## Finding entities

`nearest(rows, from)` returns the entity nearest a point among query rows that start with an `Entity` and a `Transform`, and `within(rows, from, radius)` the ones within a radius, nearest first. For the rest, query lists have `len()`, `first()`, `any(f)`, `all(f)`, `find(f)` and `filter(f)`.

```rust
@system
fn aim(turrets: (Turret, Transform)[], enemies: (Entity, Transform, Enemy)[], places: Lookup<Transform>) {
    for (turret, t) in turrets {
        let target = nearest(enemies, t.position);
        if target.alive() {
            ...
        }
    }
}
```

For colliders, `Physics.overlap_circle` asks the physics world instead; see [Physics](physics.md).

## Movement controllers

`import "drift/control" { * };` adds three controllers. Each reads the arrow keys, WASD, the d-pad and the first gamepad's left stick (turn that off with `default_keys: false`), plus the input actions it names, and drives the entity's `Velocity`. Set `enabled: false` to take control away during a cutscene.

```rust
// Top-down: eight directions, with acceleration and friction.
cmd.spawn([at(100.0, 100.0), Sprite { .. }, TopDownController { speed: 200.0, .. }]);

// Platformer: on a dynamic body with fixed rotation, under the physics gravity.
cmd.spawn([at(100.0, 300.0), Rectangle { size: vec2(24.0, 32.0), .. },
           RigidBody { kind: BodyKind::Dynamic, fixed_rotation: true, .. },
           BoxCollider { half_size: vec2(12.0, 16.0), friction: 0.0, .. },
           PlatformerController { foot: 16.0, width: 24.0, air_jumps: 1, .. }]);

// Asteroids: turn, thrust and drift.
cmd.spawn([at(480.0, 270.0), Sprite { .. }, ShipController { .. }, WrapAround { .. }]);
```

`TopDownController` reaches `speed` at `acceleration` pixels per second squared and stops at `friction`; diagonals are as fast as straight moves, and `facing` keeps the last direction for picking a sprite.

`PlatformerController` jumps with Space, Up, W or the South button. It finds the ground with three short rays under `foot` (the distance from the entity's position to its feet), `width` apart.

- Coyote time: a jump still works `coyote_time` seconds after running off a ledge.
- Jump buffering: a jump pressed up to `jump_buffer` seconds before landing happens on landing.
- Variable height: letting go of jump while rising multiplies the upward speed by `jump_cut`, so a tap is a hop and a hold is a full jump.
- `air_jumps` extra jumps in the air: 1 is a double jump. `max_fall` caps the falling speed.
- It sends `Jumped { entity, air }` and `Landed { entity }`, for sounds and dust; `grounded` and `facing` are on the component.

`ShipController` turns with Left and Right at `turn_speed` radians per second, thrusts along its facing with Up, and drifts, slowed by `drag`, up to `max_speed`. At rotation 0 the ship faces right. `thrusting` is true while thrust is held, for a flame.
