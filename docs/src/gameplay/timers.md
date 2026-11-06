# Timers

`Timer` counts down a duration once or repeatedly; `Cooldown` gates an ability so it can only be used every so often. Both are small value types: keep them in your own resources and tick them yourself, or put a `Timer` on an entity and let the plugin tick it.

**Header:** `#include <drift/gameplay/Timer.hpp>`

**Plugin:** `TimerPlugin` (included in `DefaultPlugins`)

## Timer

```cpp
Timer t = Timer::once(2.f);          // finishes once after 2 s
Timer r = Timer::repeating(0.5f);    // finishes every 0.5 s

if (r.tick(time->delta)) {
    // finished during this tick; r.timesFinished says how many times
}
```

| Member | Meaning |
|--------|---------|
| `tick(dt)` | Advance; returns true if the timer finished during this tick |
| `justFinished()` | True for the tick in which it finished |
| `finished()` | One-shot timers: true once the duration has passed (stays true until `reset()`) |
| `timesFinished` | Finishes during the last tick; a repeating timer can finish several times in one long frame |
| `remaining()`, `fraction()` | Time left, and progress 0..1 through the current period |
| `paused` | A paused timer does not advance |
| `reset()` | Start the period again |
| `tag` | Free for your use; copied into `TimerFinished` |

## Timers on entities

Insert a `Timer` component and `TimerPlugin` ticks it in `PreUpdate`, so `Update` systems see `justFinished()` in the same frame. Each finish also sends `TimerFinished { EntityId entity; uint32_t tag; }`, readable the frame after it is sent.

```cpp
// A spawner: every 1.5 s, spawn an enemy.
cmd.spawn().insert(Timer::repeating(1.5f, /*tag=*/SpawnEnemy));

void spawnEnemies(EventReader<TimerFinished> timers, Commands& cmd) {
    for (const auto& ev : timers)
        if (ev.tag == SpawnEnemy) cmd.spawn().insert(/* ... */);
}
```

A one-shot `Timer` component stays on the entity after it finishes; remove or reset it when you are done.

## Cooldown

```cpp
struct Player : Resource { Cooldown dash{0.8f}; };

void dash(ResMut<Player> p, Res<InputActions> actions, Res<Time> time) {
    p->dash.tick(time->delta);
    if (actions->pressed("dash") && p->dash.tryUse()) {
        // dash
    }
}
```

`tryUse()` returns true and starts the cooldown when it is ready. `fraction()` goes from 1 right after use to 0 when ready, which suits a radial cooldown indicator.
