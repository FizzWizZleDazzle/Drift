# Timers, Sequences and Save Data

Timers count down game time for spawn waves, cooldowns and delays; sequences run timed steps one after another; save data keeps high scores, progress and options between runs.

## Timers

A `Timer` is a plain value. Keep it in a resource or a component, and tick it from a system with the frame time:

```rust
@resource
struct Waves { timer: Timer = repeating_timer(1.5) }

@system
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

Ticking a timer changes it, and Drift keeps the change like any other.

### Timers on entities

To give every enemy its own fire rate, put the timer in a component:

```rust
@component
struct Gun { reload: Timer = repeating_timer(0.8) }

@system
fn fire(guns: (Gun, Transform)[], time: Time, cmd: Commands) {
    for (gun, t) in guns {
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
@component
struct Dash { cooldown: Cooldown = cooldown(0.8) }

@system
fn dash(d: Dash, v: Velocity, input: Input, time: Time) {
    d.cooldown.tick(time.dt);
    if input.action_pressed("dash") && d.cooldown.try_use() {
        v.linear = v.linear * 3.0;
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

## Delayed messages

`cmd.send_after(seconds, message)` sends a [message](../concepts/messages.md) once the time has passed, for one-off delays that need no state of their own:

```rust
@system
fn pick_up(_: Overlap<Player, Bomb>, cmd: Commands) {
    cmd.send_after(2.0, Explode { .. });
}
```

The message arrives at the start of the first frame after the delay, before your `Update` systems run.

## Sequences

A `Sequence` is a list of steps that run one after another across frames: waits, named events, messages, and waits for a signal. It suits intros, tutorials, boss patterns and spawn waves. Like a timer it is a plain value; keep it in a resource or component and tick it every frame:

```rust
@resource
struct Intro {
    steps: Sequence = sequence()
        .wait(1.0).then("show_title")
        .wait(2.0).send(SpawnWave { size: 5 })
        .wait_for("wave_cleared").then("show_boss"),
}

@system
fn run_intro(intro: Intro, time: Time, ui: Ui) {
    for name in intro.steps.tick(time.dt) {
        if name == "show_title" { ui.title_visible = true; }
        if name == "show_boss" { ui.boss_bar = true; }
    }
}

@system
fn wave_done(_: WaveCleared, intro: Intro) {
    intro.steps.signal("wave_cleared");
}
```

| Member | Meaning |
|--------|---------|
| `wait(seconds)` | Pause the sequence |
| `then(name)` | `tick` returns `name` when the sequence reaches this step |
| `send(message)` | Send a message when the sequence reaches this step |
| `wait_for(name)` | Pause until `signal(name)` is called; a signal sent early is kept until the step is reached |
| `repeat(times)` | Run the steps again `times` more times once they end; -1 repeats forever |
| `tick(dt)` | Advance by `dt` seconds. Returns the names of the `then` steps reached, in order; a long frame can pass several |
| `signal(name)` | Let a `wait_for(name)` step go on |
| `finished()` | Every step has run |
| `restart()` | Start again from the first step |

## Save data

`Save` is a key-value store kept as one JSON file per slot in the per-user data directory, named after `organization` and `name` in the `[project]` section of `drift.toml`:

| Platform | Directory |
|----------|-----------|
| Linux | `~/.local/share/<organization>/<name>/` |
| Windows | `%APPDATA%\<organization>\<name>\` |
| macOS | `~/Library/Application Support/<organization>/<name>/` |
| Web | the browser's `localStorage` for the page's site; see [Web Games](web.md#saves) |

```rust
@system
fn game_over(_: Contact<Player, Hazard>, save: Save, game: Game) {
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

### Whole values

`set_value(key, value)` stores any value as JSON in one key, and `get_value<T>(key, fallback)` reads it back, or returns `fallback` when the key is missing. A struct holding the player's progress, an `Inventory`, `Flags` or `Quests` saves in one call:

```rust
struct Progress { level: int = 1, coins: int = 0, unlocked: string[] = [] }

@system(schedule: Schedule::Startup)
fn load_progress(save: Save, game: Game) {
    game.progress = save.get_value<Progress>("progress", Progress { .. });
}

@system
fn checkpoint(_: ReachedCheckpoint, save: Save, game: Game) {
    save.set_value("progress", game.progress);
    save.flush();
}
```

A field the stored JSON lacks, such as one added in a later version of the game, reads as its type's zero value.

Save data methods act at once.
