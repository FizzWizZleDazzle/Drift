# Creature

A lizard made of rounded rectangles and circles, animated entirely in code. It walks toward the mouse, or where the arrow keys or WASD point. Its feet stay planted until they stray, then step in diagonal pairs while two-bone IK bends the legs to reach them; the tail is a verlet rope dragged behind; the head turns toward the cursor on a spring and the pupils follow it; the antennae lag on springs of their own; the body breathes and stretches as it runs.

The [Procedural Animation](../guides/procedural-animation.md#walkthrough-the-creature) guide walks through it step by step.

Run it from its folder:

```sh
cd examples/creature
drift run
```

## drift.toml

```toml
{{#include ../../../examples/creature/drift.toml}}
```

`[creature]` holds the tuning the game reads into its `Tuning` settings: try a longer `step_distance` for a loping stride, or a shorter `step_time` for a scurry.

## main.cdl

```rust
{{#include ../../../examples/creature/main.cdl}}
```
