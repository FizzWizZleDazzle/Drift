# States

A state is the phase a game is in: a title screen, playing, paused, game over. Declare the phases as an enum marked `@state`, and systems say which phases they run in.

```rust
@state
enum Phase { Title, Playing, Paused, Over }

@system(in_state: [Phase::Playing])
fn spawn_enemies(cmd: Commands, waves: Waves, time: Time) { }

@system(on_enter: Phase::Over)
fn crash(effects: Effects, sfx: Sfx) {
    sfx.hit.play();
    effects.flash(rgb(255, 255, 255), 0.25);
}
```

## Running in a state

`in_state: [Phase::Title, Phase::Playing]` runs a system only while the state is one of those variants. A system without `in_state` runs in every state.

`on_enter: Phase::Over` runs a system once, in the frame the state becomes `Over`; `on_exit: Phase::Over` runs one once when it stops being `Over`. These systems do not run otherwise. Use them for what happens at a change: building a menu, clearing the board, saving a best score, starting music.

A pause screen is a state too: the gameplay systems carry `in_state: [Phase::Playing]`, so they stop while the state is `Paused`. Motion the engine runs by itself, such as `Velocity` and tweens, keeps going; freeze it with `time.set_scale(0.0)` while paused, or move paused things in your own systems. The [Menus and States](../examples/menu.md) example does the second.

## Changing state

A system takes `State<Phase>` to read and change the state:

| Member | Meaning |
|--------|---------|
| `now` | The current variant |
| `since` | Seconds since the state last changed |
| `set(v)` | Asks for a change to `v` |

```rust
fn pressed_start(input: Input) -> bool => input.action_pressed("start");

@system(in_state: [Phase::Title], run_if: [pressed_start])
fn start(phase: State<Phase>) {
    phase.set(Phase::Playing);
}

@system
fn banner(_: Banner, text: Text, phase: State<Phase>) {
    text.visible = phase.now == Phase::Over;
}
```

A change takes effect after the frame's `Update` systems have run: the `on_exit` systems of the old variant run, then the state changes, then the `on_enter` systems of the new one run. Every system in that frame sees the same state. When several systems ask for a change in one frame, the last one to run wins; asking for the current variant changes nothing.

The first variant of the enum is the starting state. Its `on_enter` systems run once, after the `Startup` systems, so the entities those spawned exist.

## Starting over

To restart a round, set the state that builds the round again, as the examples do with `on_enter`. To restart the whole game, `cmd.restart()` removes every entity the game spawned, returns resources and states to their defaults, and runs the startup systems again.
