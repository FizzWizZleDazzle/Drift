# Timers and Save Data

Timers count down game time for spawn waves, cooldowns and delays; save data keeps high scores, progress and options between runs.

## Timers

A `Timer` is a plain value. Keep it in a resource or a component, and tick it from a system with the frame time:

```rust
@drift::resource
struct Waves { timer: Timer = repeating_timer(1.5) }

@drift::system(writes: [Waves])
fn spawn_enemies(cmd: Commands, time: Time, waves: Waves) {
    for i in range(waves.timer.tick(time.dt)) {
        cmd.spawn([at(640.0, 0.0), Enemy { .. }]);
    }
}
```

`timer(seconds)` finishes once; `repeating_timer(seconds)` starts over each time it finishes.

| Member | Meaning |
|--------|---------|
| `tick(dt)` | Advance by `dt` seconds. Returns how many times the timer finished during this tick: 0 or 1 for a one-shot timer, possibly more for a repeating timer in a long frame |
| `finished()` | A one-shot timer has run out; it stays finished until `reset()` |
| `remaining()` | Seconds left in the current period |
| `fraction()` | Progress through the current period, 0 to 1 |
| `reset()` | Start the period again |
| `paused` | A paused timer does not advance |

Ticking a timer changes it, so the system names the resource or component that holds it in `writes:`.

### Timers on entities

To give every enemy its own fire rate, put the timer in a component:

```rust
@drift::component
struct Gun { reload: Timer = repeating_timer(0.8) }

@drift::system(writes: [Gun])
fn fire(guns: (Gun, Transform)[], time: Time, cmd: Commands) {
    for row in guns {
        let (gun, t) = row;
        if gun.reload.tick(time.dt) > 0 {
            cmd.spawn([Transform { position: t.position, .. }, Velocity { linear: vec2(0.0, 300.0), .. },
                       Lifetime { seconds: 2.0 }, Bullet {}]);
        }
    }
}
```

For something that only needs to happen once after a delay, such as removing an entity, a `Lifetime` component does it inside the engine without a timer.

## Cooldowns

A `Cooldown` gates an action: it is ready, you use it, and it is not ready again until its time has passed.

```rust
@drift::component
struct Dash { cooldown: Cooldown = cooldown(0.8) }

@drift::system(writes: [Dash, Velocity])
fn dash(players: (Dash, Velocity)[], input: Input, time: Time) {
    for row in players {
        let (d, v) = row;
        d.cooldown.tick(time.dt);
        if input.action_pressed("dash") && d.cooldown.try_use() {
            v.linear = v.linear.mul(3.0);
        }
    }
}
```

| Member | Meaning |
|--------|---------|
| `tick(dt)` | Advance by `dt` seconds |
| `ready()` | Whether it can be used |
| `try_use()` | If ready, starts the cooldown and returns `true` |
| `fraction()` | 1 right after use, falling to 0 when ready; suits a radial indicator |
| `reset()` | Ready at once |

## Save data

`drift::Save` is a key-value store kept as one JSON file per slot in the per-user data directory, named after `organization` and `name` in the `[project]` section of `drift.toml`:

| Platform | Directory |
|----------|-----------|
| Linux | `~/.local/share/<organization>/<name>/` |
| Windows | `%APPDATA%\<organization>\<name>\` |
| macOS | `~/Library/Application Support/<organization>/<name>/` |

```rust
@drift::system(writes: [Game])
fn game_over(hits: drift::Contact<Player, Hazard>[], save: drift::Save, game: Game) {
    if hits.len() == 0 {
        return;
    }
    if game.score > save.get_int("best", 0) {
        save.set_int("best", game.score);
        save.flush();
    }
}
```

- `get_int`, `get_float`, `get_string` and `get_bool` return the fallback for a missing key or one of another type.
- `set_*` changes the slot in memory. Nothing reaches the disk until `flush()`, so call it at checkpoints and when options change, not every frame.
- `flush()` writes a new file and renames it over the old one, so a crash during a save keeps the previous save.
- A slot file that does not parse is reported in the log, and the slot starts empty.

The default slot is `save`. `save.slot("slot2")` opens another one in the same directory, for several save games:

```rust
let slot = save.slot("slot" + string(menu.choice));
slot.set_int("level", progress.level);
slot.flush();
```

Save data methods act at once, so a system that saves needs no `writes:` for it.
