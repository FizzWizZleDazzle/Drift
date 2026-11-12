# The Frame

What happens each frame, and how the engine's work and your systems share it.

## Order of a frame

1. Input arrives: keys, mouse, gamepads, the window.
2. Your `Update` systems run, one after another, in their order. Each one's commands apply when it returns.
3. State changes asked for this frame apply: the `on_exit` systems of the old state run, then the `on_enter` systems of the new one. See [States](states.md).
4. The engine finishes the frame: physics steps, transforms propagate to children, animations, tweens, particles and behaviors advance, and the frame is drawn.

Collision messages from a physics step reach your systems on the frame after it.

## Running beside the engine

candela runs your systems on one thread. The engine runs its own work on several, and it starts each piece as early as the data allows: while your systems run, physics, particles, animation and the other engine systems run on worker threads, as long as none of your systems uses the same types. A system's parameter types are how the engine knows.

So a system that takes only what it uses lets more engine work overlap it. A system that takes `Physics` or `Transform` waits for the engine work on those to finish before it starts, and the engine work after it waits for it to return.

Nothing about this changes what a system sees: the result is the same as running every system and every engine step one at a time, in order.

## Determinism

Systems run in a fixed order, queries list entities in spawn order, and messages arrive in the order they were sent. With `random::seed` called at startup and the same input, a game runs the same way every time, which makes bugs repeatable and input playback through `Input.send_key` possible.

## Frame time

`Time.dt` is the time since the last frame. Multiply speeds by it so movement does not depend on the frame rate. The engine's own motion (physics, `Velocity`, tweens) already does.
