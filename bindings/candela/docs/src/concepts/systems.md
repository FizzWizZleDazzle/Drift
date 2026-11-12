# Systems

A system is a function that runs every frame and works on the data its parameters ask for. All of a game's behavior lives in systems; there is no main loop to write.

```rust
@system
fn regenerate(stamina: Stamina, regen: Regen, time: Time) {
    stamina.value = min(stamina.max, stamina.value + regen.per_second * time.dt);
}
```

Drift finds every function marked `@system`, reads its parameter types, and calls it with the matching data. [System Parameters](../reference/system-parameters.md) lists every kind of parameter.

## Once per entity, or once for all

A parameter that is one component, like `stamina: Stamina`, makes the system run once for each entity that has every such component. `regenerate` above runs once per entity with both a `Stamina` and a `Regen`.

A parameter that is a list, like `(Stamina, Regen)[]` or a [query row](queries.md) list, receives every match at once, which a system needs to compare entities, count them or join two sets:

```rust
@system
fn count_enemies(enemies: (Entity, Enemy)[], hud: Hud) {
    hud.enemies = enemies.len();
}
```

Name a component parameter `_` to require it without reading it: `fn drift_left(_: Cloud, t: Transform, time: Time)` runs for every entity with a `Cloud` and a `Transform`. An `Entity` parameter next to component parameters receives the entity the call is for.

A parameter that is one collision or message, such as `hit: Overlap<Player, Coin>`, makes the system run once per event; see [Messages and Collisions](messages.md).

## Changes are kept

A system changes components and resources by assigning to its parameters, and Drift keeps every change when the system returns. There is nothing to declare: a parameter's type is what the system reads and may write.

The engine's own work (physics, animation, particles, rendering preparation) runs on worker threads beside your systems wherever they do not use the same types. See [The Frame](frame.md).

## Order

Systems run in the order they appear in the source, unless the attribute says otherwise:

```rust
@system(after: [steer, score])
fn hud(...) { ... }
```

`after: [a, b]` runs this system once `a` and `b` have run this frame; `before: [c]` runs it before `c`. A loop between them is an error when the game loads.

## Startup systems

```rust
@system(schedule: Schedule::Startup)
fn setup(cmd: Commands) { ... }
```

A `Startup` system runs once, before the first frame. Commands a system issues apply when it returns, so a startup system ordered `after:` another sees what that one spawned and inserted.

## Running some of the time

`run_if:` names conditions: functions that return `bool`. The system runs only in frames where every one of them holds.

```rust
fn flapped(input: Input) -> bool => input.action_pressed("flap");

@system(run_if: [flapped])
fn flap(bird: Bird, tuning: Tuning) {
    bird.vy = -tuning.flap_speed;
}
```

A condition takes parameters the way a system does: resources, settings, `Input`, `Time` and queries. It is evaluated once per frame however many systems name it, and what it changes is discarded.

For game phases such as a menu, play and game over, use [states](states.md): `in_state:` runs a system only in some phases, and `on_enter:` and `on_exit:` run one when a phase begins or ends.

A system that takes a resource does not run while that resource does not exist, so a resource also works as a switch. `Systems` turns individual systems on and off by name.

## Functions that are not systems

Only functions marked `@system` are systems. Write helpers as plain functions and call them from systems; they take whatever arguments you pass, including `Commands`, a resource or a single component.
