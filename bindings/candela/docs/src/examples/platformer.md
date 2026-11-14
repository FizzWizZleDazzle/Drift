# Platformer

Run, jump, collect the coins and reach the flag. The level is written as text in the script and built into ground with merged colliders; the player is a dynamic body that knows when it stands on ground from contact events.

[Play it in the browser](../play/platformer/). Left and Right or A and D run; Space, Up or W jumps, and letting go early makes a short hop. Spikes and falling send you back to the start. R starts over.

## What it shows

- A level written as rows of text, with runs of ground merged into one collider each
- A dynamic body with `fixed_rotation` as the player, and gravity from `[physics]`
- Standing on ground from `Contact` and `ContactEnd` events and the contact normal
- Variable jump height from `action_released`
- Sensors for coins and the goal, solid spikes with a `Contact` handler
- A run timer from `Time.elapsed`, and the best time in save data with `get_float` and `set_float`
- States for playing and winning, settings from `[platformer]` in `drift.toml`

## Run it

```sh
cd bindings/candela/examples/platformer
drift build --web
cd build/web && python3 -m http.server 8000
```

The example is self-contained: every image and sound is made in code.

The jump sets the player's upward speed, and the gravity in `drift.toml` sets how high it goes.

## drift.toml

```toml
{{#include ../../../examples/platformer/drift.toml}}
```

## The level

Each string is a row of 24-pixel tiles. `setup` walks the rows, spawns ground for each run of `#`, spikes for `^`, the flag for `F`, and remembers `P` as the start.

```rust
{{#include ../../../examples/platformer/main.cdl:level}}
```

## Running and jumping

Writing `Velocity` sets the body's speed directly; gravity keeps pulling. The player may jump while `standing_on` lists any ground.

```rust
{{#include ../../../examples/platformer/main.cdl:jump}}
```

## Knowing when you stand on something

`Contact` reports each new touch with a normal pointing from the player to the ground. Ground under the feet has a normal pointing down; walls and ceilings do not, so the player cannot jump off them. `ContactEnd` reports the player leaving.

```rust
{{#include ../../../examples/platformer/main.cdl:ground}}
```

## The whole game

```rust
{{#include ../../../examples/platformer/main.cdl}}
```
