# Scene Editor

**Source:** `examples/scene_editor/main.cpp`

A small level editor built from the engine's own tools: the level is a JSON scene, the dev inspector edits it, and F5 writes it back.

## What It Shows

- Loading `assets/level.json` and spawning it: sprites, static colliders, a bouncing ball, a spinning parent with orbiting children, and a particle emitter, all described in JSON.
- A game component (`Spin`) opting into [reflection](../concepts/reflection.md), so the scene file and the inspector handle it like an engine component.
- Editing in the [inspector](../subsystems/dev-tools.md): select an entity, change its fields, add or remove components, spawn and despawn.
- Saving the world with `SceneResource::save` and loading the saved file on the next run.
- A prefab (`assets/coin.json`) spawned at the mouse with a random tint passed as an override.
- Physics debug drawing, switched on from code.
- Hot reload: edit the loaded scene file or an image in `assets/` while the example runs and the change appears.

## Controls

| Key | Action |
|-----|--------|
| F1 | Show or hide the dev overlay |
| F5 | Save the world to `level.json` |
| P | Spawn a coin prefab at the mouse |

The "Scene Editor" panel, listed in the overlay menu, has the same actions as buttons.

## Running

```bash
cmake --build build --target scene_editor
cd examples/scene_editor && ../../build/scene_editor
```

The first run loads `assets/level.json`. After you save, later runs load `level.json` from the working directory instead; delete it to start over.
