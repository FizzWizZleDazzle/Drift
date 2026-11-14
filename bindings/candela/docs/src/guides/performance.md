# Performance

Drift is built so that a game written the plain way runs fast: you should not have to tune it. This page says what the engine does for you, what still costs time, and how to see where a frame goes.

## What the engine does for you

**Only what changed goes back.** A system gets copies of the components and resources it asks for. When it returns, the engine compares each one with what it handed out and keeps only the ones the system changed; reading a component and leaving it alone costs nothing more than the read.

**Resources are copied once per run.** A system that runs once per entity gets the same copy of each resource for every entity, until one of them changes it.

**What no camera sees costs almost nothing.** Sprites, shapes, text, particles, trails, tilemap chunks and lights that no camera shows are skipped before anything is built for them, with several cameras, render targets, turned and zoomed cameras, and parts of a hierarchy alike. A level made of thousands of tile entities is fine.

**Things that stand still cost almost nothing.** Static colliders, and decorations parented to things that do not move, are not touched again until something changes them; the engine notices the change itself, so there is nothing to mark. Bodies that come to rest fall asleep until something hits or moves them.

**Sprites batch.** Sprites that share a texture or a sprite sheet, a `z` and a layer draw in one call.

**Particles and lights stay cheap off screen.** An emitter keeps simulating while no camera sees it, so its effect looks right when the camera comes back, but draws nothing; `max_particles` caps each emitter. A light is drawn only by the cameras its radius reaches.

**Dropped textures and images are freed.** A texture or image you make and stop holding is freed once the engine sees nothing uses it, so a system can make a fresh image every frame. See [Images](sprites.md#images).

**Pages start quickly.** The page downloads the runtime and the game side by side. `drift build --web` compiles the game into the page where it can, so the page starts without compiling the scripts, and prints why when it cannot; such a page, like a dev page, compiles them once when it loads, and a dev page once more for each change.

## What still costs time

- **Every entity a system reads is copied into the script and back.** The cost grows with the number of entities and the size of their components, and is about the same whether the system runs once per entity or once over a list. Ask only for the components a system uses, and tag components such as `(Enemy, Transform)` keep a query to the entities it needs.
- **Big components cost what they hold.** A component with a long list, such as the points of a rope, is copied every time a system reads it. Keep a list like that in its own component, which only the systems that need it read.
- **A list parameter in a per-entity system is built for every call.** To read one other entity, use a `Lookup`; see [Keeping queries cheap](../concepts/queries.md#keeping-queries-cheap).
- **A `z` per sprite splits batches.** Giving every sprite its own `z`, as y-sorting does, makes a new draw wherever the texture changes between neighbours in draw order.
- **Every engine call crosses into the engine.** Procedural animation solvers, physics queries and `Draw` calls each cross once per call; per entity per frame, that adds up.
- **Pixels set one by one in a script are slow.** Build images with `fill`, `fill_rect`, `draw`, `blit`, `crop` and the shape methods, which run in the engine, rather than `set` in a loop. A startup system that draws a big image pixel by pixel delays the first frame.
- **Compiling in the page.** A page that compiles its scripts spends that time before the first frame, and each struct marked `@component`, `@resource`, `@settings` or `@message` with a field that has no default adds one more compile; give fields defaults.

## Seeing where time goes

Press F1 in a running game for the engine's overlay: it shows the frame rate and times the engine's systems. The browser's developer tools (F12, the Performance tab) record where a frame's time goes, script and engine alike.
