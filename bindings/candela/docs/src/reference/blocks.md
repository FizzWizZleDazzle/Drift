# Building Block Modules

What `drift/gameplay`, `drift/control`, `drift/screens` and `drift/debug` bring in. The [Building Blocks](../guides/building-blocks.md) and [Debugging](../guides/debugging.md#the-debug-overlay-in-every-build) guides show them in use.

## drift/gameplay

### Components

| Component | Fields | Example |
|-----------|--------|---------|
| `Health` | `hp`, `max`, `invulnerable_for` (seconds after a hit), `despawn_on_death` (true), `invulnerable` (seconds left), `dead`; `fraction()` | `health(30.0)` |
| `Hitbox` | `damage`, `team`, `pierce` (false despawns it after one hit), `knockback` | `Hitbox { damage: 5.0, team: 1, .. }` |
| `Hurtbox` | `team` | `Hurtbox { team: 0 }` |
| `HealthBar` | `size`, `offset`, `fill`, `back`, `hide_when_full` | `HealthBar { offset: vec2(0.0, -30.0), .. }` |
| `HitFlash` | `color` (white), `seconds` | `HitFlash { color: rgb(255, 80, 80), .. }` |
| `FadeOut` | `seconds` | `FadeOut { seconds: 0.4, .. }` |
| `KeepInBounds` | `area` (the camera's view when unset), `margin` | `KeepInBounds { margin: 16.0, .. }` |
| `WrapAround` | `area`, `margin` | `WrapAround { .. }` |
| `DespawnOffscreen` | `area`, `margin` | `DespawnOffscreen { margin: 100.0, .. }` |
| `Spawner` | `id`, `every`, `count` (-1 forever), `spread`, `paused`, `fired` | `spawner("bat", 0.8)` |
| `WaveSpawner` | `waves` (a `Wave[]`), `looping`, `wave` (the current one), `done` | `WaveSpawner { waves: [wave("bat", 5, 0.6, 1.0)], .. }` |

### Resources

| Resource | Fields and methods |
|----------|--------------------|
| `Score` | `value`, `best`, `key` (the save key, `"best"`); `add(points)`, `reset()`, `is_record()` |

### Messages

| Message | Sent when, or what it does |
|---------|----------------------------|
| `DealDamage { target, amount, source }` | Send it to hurt (or, negative, heal) an entity with `Health` |
| `Damaged { entity, amount, hp, source }` | `Health` took a hit |
| `Died { entity, source }` | `Health` reached 0 |
| `HitStop { seconds }` | Send it to freeze the game for that long |
| `Shake { intensity }` | Send it to shake every camera |
| `SpawnTick { spawner, id, position, index }` | A `Spawner` fired |
| `WaveStarted { wave }`, `WaveSpawn { wave, id, index }`, `WavesDone { waves }` | A `WaveSpawner` started a wave, spawned, finished |

### Functions

| Function | Does |
|----------|------|
| `health(hp)` | A `Health` with `hp` and `max` both `hp` |
| `damage(cmd, target, amount, source)`, `heal(cmd, target, amount)` | Send `DealDamage` |
| `flash(cmd, e, f)` | Starts a `HitFlash` on an entity now |
| `popup(cmd, pos, text, color)` | Spawns text that pops, floats up and fades at a world position; returns it |
| `spawner(id, every)` | A `Spawner` |
| `wave(id, count, every, delay)` | A `Wave` |

## drift/control

| Component | Fields |
|-----------|--------|
| `TopDownController` | `speed`, `acceleration`, `friction`, `action` (a 2D action to read too), `default_keys`, `enabled`, `facing` |
| `PlatformerController` | `speed`, `acceleration`, `air_acceleration`, `friction`, `jump_speed`, `jump_cut`, `coyote_time`, `jump_buffer`, `air_jumps`, `max_fall`, `foot`, `width`, `jump_action`, `move_action`, `default_keys`, `enabled`, `grounded`, `facing` |
| `ShipController` | `thrust`, `turn_speed`, `max_speed`, `drag`, `reverse`, `action`, `default_keys`, `enabled`, `thrusting` |

| Message | Sent when |
|---------|-----------|
| `Jumped { entity, air }` | A `PlatformerController` jumps; `air` for an air jump |
| `Landed { entity }` | It lands |

All three bring a `Velocity` with them.

## drift/screens

| Item | What it is |
|------|------------|
| `Screen` | State enum: `Title`, `Playing`, `GameOver` |
| `Screens` | Settings from `[screens]`: `color`, `seconds`, `kind` (`TransitionKind::Fade`, `Wipe`, `Cut`). `go(to)`, `fade_to(to)`, `wipe_to(to)`, `go_with(to, kind)`, `busy()` |
| `Pause` | Resource: `on`, `menu`, `allowed`, `title`; `set(on, time)` |
| `running(pause)` | `run_if` condition: true while not paused |
| `OnlyIn { screen }` | Component: despawns the entity when the game leaves `screen` |
| `title_screen(cmd, theme, title, subtitle)` | Heading and a Play button on the title screen |
| `game_over_screen(cmd, theme, title, detail)` | Heading, detail and Play again and Title buttons on the game-over screen |

Button ids the module answers: `"play"` and `"retry"` go to `Playing`, `"title"` to `Title`, `"resume"` ends a pause.

## drift/debug

| Item | What it is |
|------|------------|
| `Debug` | Settings from `[debug]`: `shown` (the overlay, true), `keys` (the F-keys work, true), `slow` (the slow-motion scale, 0.25), `watches`. `watch(path)` shows a resource field; `print(text)` puts a line on the overlay for five seconds and logs it |

| Key | Does |
|-----|------|
| F2 | Shows or hides the overlay |
| F3 | Draws the colliders |
| F4 | Slow motion on or off |
| F6 | Freezes or unfreezes game time |
| F7 | Steps one frame while frozen |
