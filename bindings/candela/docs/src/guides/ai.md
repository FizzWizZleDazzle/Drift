# State Machines and AI

Helpers for enemies and NPCs: a state machine per entity, and utility choices that pick the best action by score. They come with the `drift/ai` module:

```rust
import "drift" { * };
import "drift/ai" { * };
```

Pathfinding and flow fields are in [Procedural Generation](procedural-generation.md#finding-the-way). The [drift/ai reference](../reference/ai.md) lists every member.

## State machines per entity

[States](../concepts/states.md) switch the whole game between menus, play and pause. An enemy that patrols, chases and flees needs a state of its own: an `Fsm<S>` over an enum you declare, kept in a component.

```rust
enum Mode { Patrol, Chase, Flee }

@component
struct Guard { brain: Fsm<Mode> = fsm(Mode::Patrol), speed: float = 60.0 }

@system
fn think(g: Guard, t: Transform, v: Velocity, time: Time, players: (Player, Transform)[]) {
    g.brain.tick(time.dt);
    let target = players[0].1.position;
    let near = t.position.distance(target) < 120.0;
    match g.brain.now {
        Mode::Patrol => {
            if g.brain.just_entered(Mode::Patrol) { v.linear = vec2(g.speed, 0.0); }
            if near { g.brain.go(Mode::Chase); }
        }
        Mode::Chase => {
            v.linear = (target - t.position).normalized() * g.speed * 1.5;
            if !near && g.brain.time > 2.0 { g.brain.go(Mode::Patrol); }
        }
        Mode::Flee => { v.linear = (t.position - target).normalized() * g.speed; }
    }
}
```

| Member | Meaning |
|--------|---------|
| `fsm(start)` | A machine in state `start` |
| `now`, `previous` | The current state, and the one before the last change |
| `time` | Seconds in the current state, counted by `tick` |
| `tick(dt)` | Call once a frame before reading the state |
| `go(s)` | Change state; `time` starts again. Going to the current state does nothing |
| `in_state(s)` | Whether the machine is in `s` |
| `just_entered(s)` | `true` on the first `tick` in `s`, for work done once on entering: set a speed, play a sound, start a timer |

## Utility choices

When an NPC weighs several options, score each one and take the best. Build a `Choice<T>` each time the NPC decides:

```rust
enum Action { Eat, Sleep, Wander }

@system
fn decide(n: Villager, rng: VillageRng) {
    let pick = choice<Action>()
        .consider(Action::Eat, 1.0 - n.food)
        .consider(Action::Sleep, 1.0 - n.energy)
        .consider(Action::Wander, 0.3);
    match pick.best() {
        Some(a) => { n.action = a; }
        None => {}
    }
}
```

| Member | Returns |
|--------|---------|
| `choice<T>()` | An empty choice; name the option type |
| `consider(option, score)` | The choice, with the option added |
| `best()` | The option with the highest score, or `None` when there are none. The first of equal scores wins |
| `weighted(rng)` | An option picked at random in proportion to its score, so close options take turns; options scoring 0 or less are never picked |

`remap`, `smoothstep` and `clamp` from [Values](../reference/ai.md) turn distances, health and timers into scores between 0 and 1.
