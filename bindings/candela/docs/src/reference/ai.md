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

```rust
struct Fsm<S> {
    now: S,
    previous: S,
    time: float = 0.0,
    fresh: bool = true,
    entered: bool = false,
}
```

### Fsm.go

```rust
fn go(self, s: S)
```

Changes state; the time in it starts again.

### Fsm.tick

```rust
fn tick(self, dt: float)
```

Call once a frame before reading the state.

### Fsm.in_state

```rust
fn in_state(self, s: S) -> bool
```

### Fsm.just_entered

```rust
fn just_entered(self, s: S) -> bool
```

True on the first tick in state s: for one-off work on entering.

## fsm

```rust
fn fsm<S>(start: S) -> Fsm<S>
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

### Choice.consider

```rust
fn consider(self, option: T, score: float) -> Choice<T>
```

### Choice.best

```rust
fn best(self) -> Option<T>
```

The option with the highest score; None when there are none.

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
