# Commands

`drift::Commands` changes the world's shape: making and removing entities, adding and removing components, inserting resources and sending messages. A system takes it as a parameter, and Drift applies what it recorded when the system returns, in the order recorded.

```rust
@drift::system
fn fire(input: drift::Input, guns: (Gun, Transform)[], cmd: drift::Commands, assets: drift::Assets) {
    if !input.action_pressed("fire") {
        return;
    }
    for row in guns {
        let (_, t) = row;
        cmd.spawn([
            Transform { position: t.position, rotation: t.rotation, .. },
            Sprite { image: assets.texture("assets/bullet.png"), .. },
            Velocity { linear: vec2(600.0, 0.0).rotated(t.rotation), .. },
            Lifetime { seconds: 2.0 },
            Bullet {},
        ]);
    }
}
```

A system that only spawns needs no `writes:`; `writes:` is for changing components and resources the system received.

## Methods

| Method | Does |
|--------|------|
| `spawn(bundle) -> Entity` | Makes an entity with the components in the list. The id is valid at once: use it in later commands of the same system |
| `spawn_child(parent, bundle) -> Entity` | The same, with its `Transform` relative to `parent` |
| `insert(e, component)` | Adds a component, or replaces the one of its type |
| `remove<T>(e)` | Takes a component off; nothing happens if `e` has none |
| `despawn(e)` | Removes the entity and its children |
| `insert_resource(r)` | Adds a resource, or replaces it |
| `remove_resource<R>()` | Removes a resource; systems that take it stop running |
| `send(message)` | Sends a message to the systems that read its type; see [Messages](messages.md) |
| `spawn_scene(scene, options) -> SceneInstance` | Spawns a copy of a scene or prefab; see [Scenes and Prefabs](../guides/scenes.md) |

## When they apply

Commands apply as soon as their system returns, before the next system starts. A system ordered after this one sees the new entities in its queries; one that runs before it this frame sees them next frame. The engine sees them at once too: a spawned body joins the physics world at the next step.

Despawning an entity that is already gone does nothing, so two systems may despawn the same entity in one frame.
