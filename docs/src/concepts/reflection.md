# Reflection

Reflection describes a component's name and fields once, so tools can read and write it without knowing its C++ type. Scene files, prefabs and the dev inspector all work from these descriptions. Every engine component is described already; your own components opt in with a few lines.

**Header:** `#include <drift/Reflect.hpp>`

## Describing a Component

```cpp
struct Health {
    float hp = 100.f;
    float max = 100.f;
    bool invulnerable = false;
};

class GamePlugin : public Plugin {
public:
    void build(App& app) override {
        reflect::component<Health>(app.world(), "Health")
            .field("hp", &Health::hp)
            .field("max", &Health::max)
            .field("invulnerable", &Health::invulnerable);
    }
    DRIFT_PLUGIN(GamePlugin)
};
```

`reflect::component<T>(world, name)` registers `T` with the world under `name` (if it is not registered yet) and returns a builder. The name is what scene files use, so keep it stable. Describe components in a plugin's `build()`, before any scene loads.

Fields you leave out are not saved or shown; they keep their default value when a scene spawns the component. A component with no fields (a marker tag) is still saved, as `{}`.

## Field Types

These types work as fields without further setup:

| C++ type | Saved as |
|----------|----------|
| `bool`, integers, `float`, `double` | number or `true`/`false` |
| `Vec2`, `Vec4`, `Rect` | `[x, y]`, `[x, y, z, w]`, `[x, y, w, h]` |
| `Color` | `[r, g, b, a]`, 0 to 255 |
| `ColorF` | `[r, g, b, a]`, 0 to 1 |
| `char[N]` | string |
| `std::string` | string |
| `TextureHandle`, `SoundHandle` | the file path the asset was loaded from |
| `FontHandle` | `{"path": ..., "size": ...}` |
| `std::vector<T>` of any described type | array |

Asset handles go through the [Asset Server](../subsystems/asset-server.md): saving writes the path the asset was loaded from, loading loads that path (cached). An asset made in code has no path and saves as `null`, unless you name it with `AssetServer::add`.

Enums and nested structs need a description first:

```cpp
enum class Team { Red, Blue };

struct Delay {
    float min = 1.f;
    float max = 2.f;
};

struct Spawner {
    Team team = Team::Red;
    Delay delay;
    EntityId target = InvalidEntityId;
};

reflect::enumType<Team>("Team", {{"Red", Team::Red}, {"Blue", Team::Blue}});
reflect::type<Delay>("Delay")                    // nested struct
    .field("min", &Delay::min)
    .field("max", &Delay::max);
reflect::component<Spawner>(app.world(), "Spawner")
    .field("team", &Spawner::team)
    .field("delay", &Spawner::delay)
    .entity("target", &Spawner::target);
```

Enums save as their value name. `EntityId` fields that point at another entity use `.entity(...)` instead of `.field(...)`: scenes save them as the target's id inside the file and remap them when the scene spawns. A field whose type has no description is skipped and logged as an error.

## Engine Components

These are described by the plugin that owns them:

| Component | Described by |
|-----------|--------------|
| `Transform2D`, `Sprite`, `Camera`, `Name`, `Parent` | the world itself |
| `GlobalTransform2D` | `HierarchyPlugin` |
| `RigidBody2D`, `BoxCollider2D`, `CircleCollider2D`, `Velocity2D` | `PhysicsPlugin` |
| `ParticleEmitter` (with `EmitterConfig`, `ValueRange`, `BurstEntry`) | `ParticlePlugin` |
| `SpriteAnimator` (with `AnimationClip`) | `SpriteAnimationPlugin` |
| `CameraFollow`, `CameraShake` | `CameraPlugin` |
| `TrailRenderer` | `TrailPlugin` |
| `Shape2D` | `ShapePlugin` |
| `Lifetime`, `Oscillate`, `Follow` | `BehaviorPlugin` |

`Children` is not described: it is derived from `Parent`, and scenes rebuild it when they spawn. `Sprite::material` and `Camera::target` are runtime handles and are not saved.

## Using Descriptions Directly

Most code uses reflection through scenes and the inspector. Tools of your own can walk the registry:

```cpp
for (const reflect::ComponentInfo* c : reflect::components()) {
    ComponentId id = c->id(world);
    if (!id || !world.hasComponent(entity, id)) continue;
    nlohmann::json value = reflect::toJson(*c->info, world.getComponent(entity, id), {});
    DRIFT_LOG_INFO("%s = %s", c->name.c_str(), value.dump().c_str());
}
```

- `reflect::components()`, `findComponent(name)`: the described components.
- `reflect::toJson(type, ptr, ctx)` / `fromJson(type, ptr, json, ctx)`: convert one value. `fromJson` leaves fields missing from the JSON unchanged. `JsonContext` supplies the `AssetServer` for asset paths and the entity mapping for `.entity` fields.
- `reflect::Value`: owned storage for one value of a described type, built and destroyed with the type's own constructor and destructor.
- `TypeInfo` lists a type's `kind`, `fields` (name, offset, type), `enumValues` and vector `element` type.

## Components That Own Memory

Components with members such as `std::vector` (`SpriteAnimator`, `ParticleEmitter`) are stored with their C++ constructor, copy and destructor, so inserting them through `Commands` or a scene keeps their contents intact. This applies to any component type that is not trivially copyable, described or not.
