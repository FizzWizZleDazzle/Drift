# Trail Renderer

The `TrailRenderer` component draws a fading line behind a moving entity, creating motion trails for projectiles, characters, particles, or any object that moves through space.

## Definition

```cpp
struct TrailRenderer {
    float width = 4.f;
    float lifetime = 0.5f;              // seconds before a trail point fades
    ColorF colorStart = {1, 1, 1, 1};
    ColorF colorEnd = {1, 1, 1, 0};
    float minDistance = 2.f;             // min pixels before adding a new point
    int32_t maxPoints = 64;
    float zOrder = -1.f;
};
```

**Header:** `#include <drift/components/TrailRenderer.hpp>`

## Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `width` | `float` | `4.f` | Width of the trail line in pixels. The trail tapers from full width at the newest point to near zero at the oldest. |
| `lifetime` | `float` | `0.5f` | How long each trail point lives before it is removed. Longer lifetimes create longer trails. |
| `colorStart` | `ColorF` | `{1,1,1,1}` | Color at the newest (head) end of the trail. |
| `colorEnd` | `ColorF` | `{1,1,1,0}` | Color at the oldest (tail) end. The default fades alpha to 0 for a natural disappearing tail. |
| `minDistance` | `float` | `2.f` | Minimum distance in pixels the entity must move before a new trail point is recorded. Prevents excessive point density when the entity moves slowly. |
| `maxPoints` | `int32_t` | `64` | Maximum number of trail points stored. Older points beyond this limit are discarded. |
| `zOrder` | `float` | `-1.f` | Draw order for the trail. Defaults to behind most sprites. |

## Requirements

`TrailRenderer` requires a `Transform2D` on the same entity. The trail system reads the entity's position each frame and records it as a new trail point when the entity has moved far enough.

## Plugin

The `TrailPlugin` is included in `DefaultPlugins`. It registers:

- A **trail update** system in `PostUpdate` that records positions and ages trail points.
- A **trail render** system in `Render` that draws the line segments.

If you are not using `DefaultPlugins`, add it manually:

```cpp
app.addPlugin<TrailPlugin>();
```

## Basic usage

### Simple white trail

```cpp
cmd.spawn().insert(
    Transform2D{.position = {400.f, 300.f}},
    Sprite{.texture = tex, .origin = {8.f, 8.f}},
    TrailRenderer{}   // default white fading trail
);
```

### Colored trail

```cpp
cmd.spawn().insert(
    Transform2D{.position = {100.f, 100.f}},
    Sprite{.texture = tex},
    TrailRenderer{
        .width = 6.f,
        .lifetime = 1.0f,
        .colorStart = {0.2f, 0.6f, 1.f, 1.f},   // blue
        .colorEnd = {0.2f, 0.6f, 1.f, 0.f}        // blue, faded
    }
);
```

### Projectile trail

A fast-moving projectile with a short, bright trail:

```cpp
cmd.spawn().insert(
    Transform2D{.position = spawnPos},
    Sprite{.texture = bulletTex, .origin = {4.f, 4.f}, .zOrder = 5.f},
    Velocity2D{.linear = direction * 800.f},
    TrailRenderer{
        .width = 3.f,
        .lifetime = 0.2f,
        .colorStart = {1.f, 1.f, 0.5f, 1.f},    // yellow-white
        .colorEnd = {1.f, 0.5f, 0.f, 0.f},       // orange, faded
        .minDistance = 4.f,
        .maxPoints = 32,
        .zOrder = 4.f
    }
);
```

### Long, dramatic trail

A slow-moving object with a long, persistent tail:

```cpp
TrailRenderer{
    .width = 8.f,
    .lifetime = 3.0f,
    .colorStart = {1.f, 0.f, 0.5f, 0.8f},   // magenta
    .colorEnd = {0.5f, 0.f, 1.f, 0.f},       // purple, faded
    .minDistance = 1.f,
    .maxPoints = 128
}
```

## How it works

The trail system manages a ring buffer of points per entity through the `TrailSystemResource`:

1. **Update phase** (PostUpdate):
   - Ages all existing trail points by adding delta time.
   - Removes points whose age exceeds `lifetime`.
   - If the entity has moved at least `minDistance` pixels since the last recorded point, a new point is added at the entity's current position.

2. **Render phase** (Render):
   - For each pair of consecutive trail points, draws a line segment.
   - The color is interpolated between `colorStart` and `colorEnd` based on each point's age relative to `lifetime`.
   - The line width tapers from `width` at the newest point toward 0 at the oldest.

When an entity with `TrailRenderer` is despawned, the trail system automatically cleans up its stored data.

## Complete example

```cpp
#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/AssetServer.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/TrailRenderer.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/Time.hpp>

#include <cmath>

using namespace drift;

struct Orbiter { float angle; float radius; float speed; };

struct GameState : public Resource {
    TextureHandle tex;
};

void setup(ResMut<GameState> game, ResMut<AssetServer> assets, Commands& cmd) {
    game->tex = assets->load<Texture>("assets/dot.png");

    cmd.spawn().insert(Transform2D{}, Camera{.zoom = 1.f, .active = true});

    // Spawn orbiting entities with trails
    for (int i = 0; i < 5; ++i) {
        float hue = static_cast<float>(i) / 5.f;
        ColorF color = {
            0.5f + 0.5f * std::sin(hue * 6.283f),
            0.5f + 0.5f * std::sin(hue * 6.283f + 2.094f),
            0.5f + 0.5f * std::sin(hue * 6.283f + 4.189f),
            1.f
        };

        cmd.spawn().insert(
            Transform2D{},
            Sprite{.texture = game->tex, .origin = {4.f, 4.f}, .zOrder = 1.f},
            TrailRenderer{
                .width = 4.f,
                .lifetime = 1.5f,
                .colorStart = color,
                .colorEnd = {color.r, color.g, color.b, 0.f},
                .maxPoints = 96
            },
            Orbiter{
                .angle = i * 1.257f,
                .radius = 80.f + i * 40.f,
                .speed = 2.f - i * 0.2f
            }
        );
    }
}

void moveOrbiters(Res<Time> time, QueryMut<Transform2D, Orbiter> orbiters) {
    orbiters.iter([&](Transform2D& t, Orbiter& o) {
        o.angle += o.speed * time->delta;
        t.position = {
            std::cos(o.angle) * o.radius,
            std::sin(o.angle) * o.radius
        };
    });
}

int main() {
    App app;
    app.setConfig({.title = "Trail Renderer", .width = 800, .height = 600});
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameState>();
    app.startup<setup>();
    app.update<moveOrbiters>();
    return app.run();
}
```

## Notes

- `TrailRenderer` only works on entities that move. A stationary entity will accumulate a single point that fades away.
- The `minDistance` field prevents the ring buffer from filling up with redundant points when the entity barely moves. Increase it for fast objects to keep the trail smooth with fewer points.
- Trail rendering uses `drawLine` calls internally, which are not batched the same way sprites are. Hundreds of trail-bearing entities are fine, but thousands may affect performance.
- To temporarily disable a trail without removing the component, you can set `lifetime` to `0` -- existing points will expire immediately and no new points will be recorded.
