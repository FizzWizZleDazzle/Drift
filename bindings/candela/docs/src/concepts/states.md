# States

A state is the phase a game is in: a title screen, playing, paused, game over. Declare the phases as an enum marked `@state`, and systems say which phases they run in.

```rust
@state
enum Phase { Ready, Playing, Over }

@system(in_state: [Phase::Playing])
fn spawn_pipes(...) { ... }

@system(on_enter: Phase::Over)
fn crash(art: Art, effects: Effects) {
    art.hit.play();
    effects.flash(rgb(255, 255, 255), 0.25);
}
```

## Running in a state

`in_state: [Phase::Ready, Phase::Playing]` runs a system only while the state is one of those variants. A system without `in_state` runs in every state.

`on_enter: Phase::Over` runs a system once, in the frame the state becomes `Over`; `on_exit: Phase::Over` runs one once when it stops being `Over`. These systems do not run otherwise. Use them for what happens at a change: clearing the board, saving a best score, starting music.

## Changing state

A system takes `State<Phase>` to read and change the state:

| Member | Meaning |
|--------|---------|
| `now` | The current variant |
| `since` | Seconds since the state last changed |
| `set(v)` | Asks for a change to `v` |

```rust
@system(in_state: [Phase::Over], run_if: [flapped])
fn restart(state: State<Phase>) {
    if state.since >= 0.5 {
        state.set(Phase::Ready);
    }
}
```

A change takes effect after the frame's `Update` systems have run: the `on_exit` systems of the old variant run, then the state changes, then the `on_enter` systems of the new one run. Every system in that frame sees the same state. When several systems ask for a change in one frame, the last one to run wins; asking for the current variant changes nothing.

The first variant of the enum is the starting state. Its `on_enter` systems run once, after the `Startup` systems, so the entities those spawned exist.
