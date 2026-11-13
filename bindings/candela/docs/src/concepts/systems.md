# Systems

A system is a function that Drift calls every frame with the data its parameters ask for. All of a game's behavior lives in systems; there is no main loop to write.

```rust
@system
fn regenerate(hero: (Player, Stamina), time: Time) {
    let (_, stamina) = hero;
    stamina.value = min(stamina.max, stamina.value + 5.0 * time.dt);
}
```

Drift finds every function marked `@system`, reads its parameter types when the game loads, and calls it with the matching data. [System Parameters](../reference/system-parameters.md) lists every kind of parameter.

## Once per entity, or once for all

A parameter that is a tuple of components, like `hero: (Player, Stamina)`, is a row: the system runs once for each entity that has every component in it, and the row holds that entity's components. It has the shape of one element of a query such as `(Player, Stamina)[]`. `regenerate` above runs once per entity with a `Player` and a `Stamina`. A system takes at most one row.

Take the row apart with `let`:

- `_` skips a place: `let (_, stamina) = hero;` requires a `Player` without reading it.
- `Entity` in the tuple gives the entity the call is for: `fn hurt(enemy: (Entity, Enemy, Health), cmd: Commands)` with `let (e, _, health) = enemy;`.
- The names share the row's values, so `stamina.value = 10.0` and `t.position.x += 1.0` change the entity. Assigning a new value to a name itself, `stamina = Stamina { .. }`, only rebinds the name; set its fields instead.

A tuple has at least two places, so a row of one component pairs it with its `Entity`: `(Entity, Transform)`. Components as separate parameters, as in `fn steer(ship: Ship, v: Velocity)`, stop the game when it loads with `drift_loose_components`, which shows the row to write instead.

### Updating a game written with separate component parameters

Games written before rows took each component as its own parameter. Gather a system's component and `Entity` parameters into one tuple, in the same order, and take it apart on the first line; the body stays as it was. A system that took a single component adds `Entity` in front of it.

```rust
// Before
@system
fn steer(_: Ship, t: Transform, velocity: Velocity, input: Input) {
    velocity.linear = input.axis2d("move") * 320.0;
}

// After
@system
fn steer(ship: (Ship, Transform, Velocity), input: Input) {
    let (_, t, velocity) = ship;
    velocity.linear = input.axis2d("move") * 320.0;
}
```

A parameter that is a list, like `(Entity, Enemy)[]`, receives every match at once. Use one to count entities, compare them, or join two sets:

```rust
@system
fn count_enemies(enemies: (Entity, Enemy)[], hud: Hud) {
    hud.enemies = enemies.len();
}
```

A parameter that is one collision or message, such as `hit: Overlap<Player, Coin>`, makes the system run once per event; see [Events and Collisions](events.md). A system runs once per entity or once per event, so it takes a row or a single event, not both; read the other side through a list or a [Lookup](queries.md).

## Changes are kept

A system changes components and resources by assigning to its parameters, and Drift keeps every change when it returns. There is nothing to declare: a parameter's type is what the system reads and may write.

```rust
@system
fn fall(flier: (Bird, Transform), time: Time) {
    let (bird, t) = flier;
    bird.vy += 1400.0 * time.dt;
    t.position = t.position + vec2(0.0, bird.vy * time.dt);
}
```

## Startup systems

```rust
@system(schedule: Schedule::Startup)
fn setup(cmd: Commands) { }
```

A `Startup` system runs once, before the first frame. A startup system ordered `after:` another sees what that one spawned.

## Order

Systems run one at a time, in the order they appear in the source, unless their attributes say otherwise:

```rust
@system(after: [steer, collect])
fn hud(score: Score, labels: (ScoreLabel, Text)[]) { }
```

`after: [a, b]` runs this system once `a` and `b` have run this frame; `before: [c]` runs it before `c`. A loop between them is an error when the game loads. Order matters when one system's changes or commands should be seen by another in the same frame.

## Running some of the time

`run_if:` names conditions: functions that return `bool`. The system runs only in frames where every one of them holds.

```rust
fn fired(input: Input) -> bool => input.action_pressed("fire");

@system(run_if: [fired])
fn fire(gun: (Gun, Transform), cmd: Commands) {
    let (_, t) = gun;
    cmd.spawn(bullet(t));
}
```

A condition takes resources, settings, engine resources and queries as parameters, the way a system does. What it changes is discarded.

Other ways to switch systems:

- For game phases such as a title screen, play and game over, use [states](states.md): `in_state:` runs a system only in some phases, and `on_enter:` and `on_exit:` run one when a phase begins or ends.
- A system that takes a resource does not run while that resource does not exist, so inserting and removing a resource switches a group of systems.
- The `Systems` engine resource turns systems on and off by name: `systems.disable("spawn_enemies")`.

## Time

`Time.dt` is the time since the last frame, in seconds. Multiply speeds by it so movement does not depend on the frame rate; the engine's own motion (`Velocity`, physics, tweens) already does. `Time.elapsed` counts seconds since the start.

`time.set_scale(0.5)` slows the whole game to half speed from the next frame, physics, tweens and particles included; 0 freezes it. `Time.real_dt` ignores the scale, for menus that keep running in slow motion.

## Doing something every few seconds

A `Timer` counts down. Keep one in a resource or a component and tick it with `dt`; `tick` returns how many times it finished:

```rust
@resource
struct Waves { spawn: Timer = repeating_timer(2.0) }

@system
fn spawn_wave(waves: Waves, time: Time, cmd: Commands) {
    for i in range(waves.spawn.tick(time.dt)) {
        cmd.spawn(enemy());
    }
}
```

`timer(seconds)` finishes once; `repeating_timer(seconds)` starts over each time. A `Cooldown` gates an action, such as a dash or a gun: `cooldown.try_use()` is `true` once per `seconds`. For something that should happen once after a delay, such as removing an entity, a `Lifetime` component does it in the engine.

### Steps in order

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

## The frame

1. Input arrives.
2. The `Update` systems run, one after another, in their order. Each one's commands apply when it returns.
3. State changes asked for this frame apply: the old state's `on_exit` systems run, then the new state's `on_enter` systems.
4. The engine steps physics, moves and animates entities, advances particles and tweens, and draws the frame.

Collision events from a physics step reach systems the frame after.

## When a system fails

A system that raises an error it does not catch stops for the rest of the run: its changes and commands from that call are dropped, and the error is logged with the script line. The other systems keep running. See [Errors](../reference/errors.md).
