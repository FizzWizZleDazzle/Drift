# Top-Down Shooter

Survive the swarm. Enemies come in from the edges, faster as time goes on, and chase you; shoot them before they reach you.

[Play it in the browser](../play/shooter/). WASD or the arrow keys move, the mouse aims, and the left button or Space fires. R starts again after the run ends.

## What it shows

- Aiming at the mouse with `mouse_world()` and `Vec2.angle()`
- A fire rate from a `Cooldown` kept in the player component
- Bullets as kinematic sensors with a `Velocity` and a `Lifetime`
- Enemies that steer toward the player in a per-entity system
- `Overlap` for bullet hits and `Contact` for enemies reaching the player
- A `ParticleEmitter` switched on and off by writing `playing`, bursts and flashes
- Spawning from the screen edges with a seeded `Rng`, on a `Timer` that speeds up

## Run it

```sh
cd bindings/candela/examples/shooter
drift build --web
cd build/web && python3 -m http.server 8000
```

The example is self-contained: every image and sound is made in code.

## drift.toml

```toml
{{#include ../../../examples/shooter/drift.toml}}
```

## Shooting

The `Cooldown` in the player component ticks every frame; `try_use` lets one shot through per `fire_every` seconds while the button is held.

```rust
{{#include ../../../examples/shooter/main.cdl:shoot}}
```

## Chasing

A system that takes single components runs once per enemy. Each one reads the player's position from a query and sets its own velocity toward it.

```rust
{{#include ../../../examples/shooter/main.cdl:chase}}
```

## Hits

The bullet is a kinematic sensor and the enemy a dynamic body, so the sensor reports the overlap. The handler runs once per hit.

```rust
{{#include ../../../examples/shooter/main.cdl:hits}}
```

## The whole game

```rust
{{#include ../../../examples/shooter/main.cdl}}
```
