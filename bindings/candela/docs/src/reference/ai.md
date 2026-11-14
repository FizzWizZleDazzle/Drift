<!-- Written by scripts/candela-reference.py from the doc comments in
     bindings/candela/drift/src. Edit those, not this page. -->

# State Machines and AI: drift/ai

A state machine per entity over an enum of your own, and options picked by score. Both are
plain values kept in your components and resources.

```rust
import "drift" { * };
import "drift/ai" { * };
```

See [State Machines and AI](../guides/ai.md).

## Fsm

The state of one entity, over an enum of your own: keep it in a component, tick it every
frame, and match on `now`.

Make one with `fsm(start)`. Call `tick(dt)` once a frame before reading it, and `go(s)` to
change state.

```rust
struct Fsm<S> {
    now: S,
    previous: S,
    time: float = 0.0,
    fresh: bool = true,
    entered: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `now` | `S` | required | The current state. |
| `previous` | `S` | required | The state before the last change; the start state until the first change. |
| `time` | `float` | `0.0` | Seconds spent in `now`, counted by `tick`. The tick that enters a state adds nothing. |
| `fresh` | `bool` | `true` | Set by `go` and at the start: the next `tick` counts as entering the state. |
| `entered` | `bool` | `false` | Whether the last `tick` was the first one in `now`; `just_entered` reads it. |

```rust
enum Mood { Idle, Chase }

@component
struct Brain { mind: Fsm<Mood> = fsm(Mood::Idle) }
```

### Fsm.go

```rust
fn go(self, s: S)
```

Changes state; the time in it starts again. Going to the state it is already in does
nothing.

### Fsm.tick

```rust
fn tick(self, dt: float)
```

Advances the time in the state by `dt` seconds; call it once a frame before reading the
state.

### Fsm.in_state

```rust
fn in_state(self, s: S) -> bool
```

Whether the current state is `s`.

### Fsm.just_entered

```rust
fn just_entered(self, s: S) -> bool
```

True on the first tick in state s: for one-off work on entering.

## fsm

```rust
fn fsm<S>(start: S) -> Fsm<S>
```

A state machine that starts in `start`; its first `tick` counts as entering it.

```rust
let mind = fsm(Mood::Idle);
```

## Choice

Options scored by how good each is right now; best() picks the highest. Build one each time
an AI decides.

```rust
struct Choice<T> {
    options: T[],
    scores: float[],
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `options` | `T[]` | required | The options, in the order `consider` added them. |
| `scores` | `float[]` | required | The score of each option, at the same index. |

```rust
let pick = choice<Action>()
    .consider(Action::Attack, 1.0 - dist / 300.0)
    .consider(Action::Flee, 1.0 - hp)
    .best();
```

### Choice.consider

```rust
fn consider(self, option: T, score: float) -> Choice<T>
```

Adds an option with its score and returns the same choice, so calls chain. Higher scores
are better; any range works for `best`, while `weighted` skips scores at or below zero.

### Choice.best

```rust
fn best(self) -> Option<T>
```

The option with the highest score; None when there are none. On a tie the option added
first wins.

### Choice.weighted

```rust
fn weighted(self, rng: Rng) -> Option<T>
```

An option chosen at random in proportion to its score, so close scores take turns.
Scores at or below zero are never picked.

## choice

```rust
fn choice<T>() -> Choice<T>
```

An empty choice to add options to with `consider`. Give the option type when nothing else
says it, as in `choice<Action>()`.
