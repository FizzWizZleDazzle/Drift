# Systems

A system is a function that runs every frame and works on the data its parameters ask for. All of a game's behavior lives in systems; there is no main loop to write.

```rust
@drift::system(writes: [Health])
fn regenerate(living: (Health, Regen)[], time: drift::Time) {
    for row in living {
        let (health, regen) = row;
        health.hp = min(health.max, health.hp + int(regen.per_second * time.dt));
    }
}
```

Drift finds every function marked `@drift::system`, reads its parameter types, and calls it with the matching data: here every entity that has both `Health` and `Regen`, and the frame time. [System Parameters](../reference/system-parameters.md) lists every kind of parameter.

## Reads and writes

A system's parameters are what it reads. `writes:` lists the components and resources it changes:

```rust
@drift::system(writes: [Transform])
fn bob(things: (Transform, Bob)[], time: drift::Time) { ... }
```

Drift keeps changes only to the types in `writes:`. A change to anything else is discarded when the system returns, and dev builds log it, naming the system and the type, so a missing entry is found the first time it matters. `drift check` lists what each system reads and writes.

Declaring writes is what lets the engine run its own work beside your systems: physics, animation, particles and rendering preparation run on worker threads while your systems run, whenever they do not touch the same data. See [The Frame](frame.md).

## Order

Systems run in the order they appear in the source, unless the attribute says otherwise:

```rust
@drift::system(writes: [Velocity])
fn steer(...) { ... }

@drift::system(writes: [Text], after: [steer, score])
fn hud(...) { ... }
```

`after: [a, b]` runs this system once `a` and `b` have run this frame; `before: [c]` runs it before `c`. A loop between them is an error when the game compiles.

## Startup systems

```rust
@drift::system(schedule: drift::Schedule::Startup)
fn setup(cmd: drift::Commands, assets: drift::Assets) { ... }
```

A `Startup` system runs once, before the first frame. Commands a system issues apply when it returns, so a startup system ordered `after:` another sees what that one spawned and inserted.

## Running some of the time

A system runs every frame. To run it only in some situations, return early:

```rust
@drift::system(writes: [Velocity])
fn steer(players: (Player, Velocity)[], game: Game, input: drift::Input) {
    if game.paused {
        return;
    }
    ...
}
```

A system that takes a resource does not run while that resource does not exist, so a resource also works as a switch: insert `Boss` when the boss fight starts, remove it when it ends, and every system that takes `Boss` runs only during the fight. `drift::Systems` turns individual systems on and off by name.

## Functions that are not systems

Only functions marked `@drift::system` are systems. Write helpers as plain functions and call them from systems; they take whatever arguments you pass, including a `Commands`, a resource or a single component.
