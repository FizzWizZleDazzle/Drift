# Scenes and Prefabs

A scene file holds entities and their components as JSON. Build levels and reusable objects (prefabs) as files, spawn them from systems, and save the running world back to a file.

## The file

```json
{
  "drift_scene": 1,
  "entities": [
    {
      "id": 0,
      "components": {
        "Name": { "value": "Player" },
        "Transform": { "position": [100, 200] },
        "Sprite": { "image": "assets/player.png" },
        "Player": { "speed": 320.0 }
      }
    },
    {
      "id": 1,
      "components": {
        "Name": { "value": "Sword" },
        "Parent": { "entity": 0 },
        "Transform": { "position": [20, 0] }
      }
    }
  ]
}
```

- Components are keyed by name. Engine components use the names in [Engine Components](../reference/components.md); yours use their struct name, so `Player` above is the game's `@drift::component struct Player`.
- Fields you leave out keep their defaults, so hand-written files stay short.
- `id` names an entity inside the file. `Entity` fields (`Parent`, `Follow.target`, and `Entity` fields of your own components) hold these ids, and spawning points them at the new entities.
- Asset fields such as `Sprite.image` hold the file path, loaded once however many entities use it.

Your own components are saved and loaded like the engine's, because Drift stores them with the entity. A component field the file names but the struct no longer has is skipped with a warning; a field the struct gained since the file was written takes its default.

## Spawning a scene

Load the file through `drift::Assets` and spawn it through `Commands`:

```rust
@drift::system(schedule: drift::Schedule::Startup)
fn load_level(cmd: Commands, assets: drift::Assets) {
    let level = cmd.spawn_scene(assets.scene("assets/level1.json"), SceneOptions { .. });
    if level.find("Player") is Some(player) {
        cmd.insert(player, CameraFollow { target: player, .. });
    }
}
```

`spawn_scene` returns a `SceneInstance` at once, with the new entities in file order (`entities`), the ones without a parent in the file (`roots`), and `find(name)` for the first entity with that `Name`. The entities and their components exist for the systems that run after this one.

## Prefabs

A prefab is a scene file you spawn many times. `SceneOptions` changes each copy:

| Field | Does |
|-------|------|
| `offset` | Moves each root entity by this much |
| `parent` | Makes the roots children of an entity |
| `overrides` | Per entity, by `Name`: a JSON patch of its components |

```rust
@drift::system
fn spawn_waves(cmd: Commands, time: Time, waves: Waves, assets: drift::Assets) {
    if waves.timer.tick(time.dt) == 0 {
        return;
    }
    let bat = assets.scene("assets/bat.json");
    let overrides: {string: string} = {};
    overrides["Bat"] = "{\"Sprite\": {\"color\": [1.0, 0.6, 0.6, 1.0]}}";
    cmd.spawn_scene(bat, SceneOptions { offset: vec2(640.0, 80.0), overrides: overrides, .. });
}
```

An override replaces the fields it lists and keeps the rest. Setting a component to `null` leaves it out of that copy.

## Saving

```rust
@drift::system
fn save_level(input: Input, cmd: Commands) {
    if input.key_pressed(Key::F5) {
        cmd.save_scene("level.json");
    }
}
```

`save_scene` writes every entity that has a saved component, after the system returns. `save_prefab(path, root)` writes one entity and its children, which is how you make a prefab from something you built in the running game. A `Parent` pointing outside the saved entities is dropped, so `root` becomes the top of the file.

## Reloading

In dev builds, Drift watches the scene files it loaded. Save one in an editor and the entities spawned from it update in place: they keep their ids, their components follow the file, new entities spawn and removed ones despawn. Spawn options apply again. Physics bodies keep the shape and body kind they were created with. The [dev overlay](debugging.md#the-dev-overlay) also has a Scene panel to save, spawn and reload files by hand.
