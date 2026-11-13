# Snake

Eat the apples and do not run into the walls or yourself. Each apple makes the snake longer and a little faster.

[Play it in the browser](../play/snake/). The arrow keys or WASD steer, and the first press starts the game. After a crash, Space sets up a new one.

## What it shows

- A game on a grid with no physics: the snake is a list of cells in a resource
- A repeating `Timer` that steps the snake, and one entity per cell, spawned at the head and despawned at the tail
- Replacing a component with `cmd.insert` to recolor the old head
- A per-entity system that keeps the apple on its cell
- Actions for each direction from `[input]` in `drift.toml`, gamepad d-pad included
- Ready, playing and game-over states, settings from `[snake]`, the best score in save data

## Run it

```sh
cd bindings/candela/examples/snake
drift build --web
cd build/web && python3 -m http.server 8000
```

The example is self-contained: every image and sound is made in code.

## drift.toml

```toml
{{#include ../../../examples/snake/drift.toml}}
```

## Steering

The last direction pressed waits for the next step, and the snake never turns straight back into itself.

```rust
{{#include ../../../examples/snake/main.cdl:steer}}
```

## One step

Each time the timer finishes, the head moves one cell. The tail cell leaves unless the snake is growing, so the head may move into it.

```rust
{{#include ../../../examples/snake/main.cdl:advance}}
```

## The whole game

```rust
{{#include ../../../examples/snake/main.cdl}}
```
