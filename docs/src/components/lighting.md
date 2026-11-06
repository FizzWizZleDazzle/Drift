# Lighting

2D lights and shadows: point lights, spot lights, an ambient level, and occluders that cast hard shadows. Use it for dark dungeons, night scenes, torches, flashlights and anything where light should stop at walls.

**Headers:** `#include <drift/plugins/LightingPlugin.hpp>` (plugin and settings), `#include <drift/components/Light2D.hpp>` (components)

## Enabling

Lighting is not part of `DefaultPlugins`. Add the plugin after them:

```cpp
app.addPlugins<DefaultPlugins>();
app.addPlugin<LightingPlugin>();
```

Once added, every lit camera starts from the ambient color, so a scene with no lights becomes dark.

## How it looks

Each lit camera gets a light map: it starts at the ambient color, each light adds its color on top, and shadows block what is behind an occluder. The light map is then multiplied over the camera's view. A sprite under full white light shows its own colors; with no light it shows `ambient` times its colors.

Light only darkens or keeps the scene; a light map value above white does not brighten sprites past their own colors. For glowing light sources, pair the light with an additive sprite and the [Bloom](../subsystems/post-effects.md#bloom) post effect.

## PointLight2D

A light that shines in every direction from the entity's `Transform2D` position.

```cpp
cmd.spawn().insert(Transform2D{{200.f, 120.f}},
                   PointLight2D{.color = {1.f, 0.6f, 0.2f}, .intensity = 1.5f, .radius = 300.f});
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `color` | `ColorF` | white | Light color. |
| `intensity` | `float` | `1.f` | Multiplies `color`. |
| `radius` | `float` | `200.f` | Reach in world units; no light past it. |
| `falloff` | `float` | `2.f` | Shape of the fade toward the edge. `1` fades linearly; higher values give a tighter bright core. |
| `castShadows` | `bool` | `true` | Whether occluders block this light. |
| `layerMask` | `uint32_t` | all world layers | Cameras whose `layerMask` shares a bit with it see the light. |

## SpotLight2D

A cone of light. It has every `PointLight2D` field plus the cone:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `direction` | `float` | `0.f` | Radians, `0` points along +x. Added to the entity's rotation, so rotating the entity aims the light. |
| `angle` | `float` | `0.8f` | Full cone angle in radians. |
| `softness` | `float` | `0.2f` | `0` gives a hard cone edge; `1` fades all the way from the center line. |

```cpp
// A flashlight that points where the player faces.
cmd.entity(player).insert(SpotLight2D{.radius = 450.f, .angle = 0.6f, .softness = 0.3f});
```

Lights follow the entity's `GlobalTransform2D` when it has one, so a light can be the child of a moving entity (see [Hierarchy](hierarchy.md)).

## LightOccluder2D

Blocks light and casts a hard shadow from every light with `castShadows`. The shape is in the entity's local space and follows its position, rotation and scale.

```cpp
// A box, a circle and a triangle.
cmd.spawn().insert(Transform2D{{0.f, 0.f}}, LightOccluder2D::box({64.f, 16.f}));
cmd.spawn().insert(Transform2D{{200.f, 0.f}}, LightOccluder2D::circle(24.f));
const Vec2 tri[3] = {{0.f, -40.f}, {36.f, 24.f}, {-36.f, 24.f}};
cmd.spawn().insert(Transform2D{{-200.f, 0.f}}, LightOccluder2D::polygon(tri, 3));

// Take the shape from the entity's BoxCollider2D or CircleCollider2D.
cmd.spawn().insert(Transform2D{{0.f, 200.f}}, RigidBody2D{.type = BodyType::Static},
                   BoxCollider2D{.halfSize = {80.f, 16.f}}, LightOccluder2D::fromCollider());
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `shape` | `OccluderShape` | `Box` | `Box`, `Circle`, `Polygon`, or `FromCollider`. |
| `halfSize` | `Vec2` | `{16, 16}` | Box half extents. |
| `radius` | `float` | `16.f` | Circle radius. |
| `points` / `pointCount` | `Vec2[16]` / `uint8_t` | empty | Polygon outline, either winding, up to 16 points. |
| `offset` | `Vec2` | `{0, 0}` | Shifts the shape from the entity's position. |
| `shadowSelf` | `bool` | `false` | `false` keeps the occluder's own area lit, which suits walls drawn with their own sprite. `true` darkens it too. |
| `layerMask` | `uint32_t` | all world layers | Blocks lights whose `layerMask` shares a bit with it, in cameras whose `layerMask` shares a bit with it. |

`FromCollider` uses the collider's size and offset and ignores the transform's scale, matching the physics shape. Concave polygons work; a shape with more than 16 points needs several occluders.

## LightingSettings

A resource that holds the scene-wide options. Change it from any system.

```cpp
void nightfall(ResMut<LightingSettings> lighting, Res<Time> time) {
    float t = std::min(1.f, static_cast<float>(time->elapsed) / 30.f);
    lighting->ambient = {0.6f - 0.5f * t, 0.6f - 0.5f * t, 0.7f - 0.5f * t, 1.f};
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | `bool` | `true` | `false` turns lighting off; the scene renders unlit. |
| `ambient` | `ColorF` | dark blue | Light level everywhere before any light adds to it. White means no darkening. |
| `layerMask` | `uint32_t` | all world layers | Cameras whose `layerMask` shares a bit with it are lit; others render unlit. |
| `resolutionScale` | `float` | `1.f` | Light map size relative to the camera viewport. `0.5` is cheaper and softens shadow edges. |

## Cameras and layers

Every lit camera has its own light map, sized to its viewport, so split screen views and render-target cameras such as minimaps each get correct lighting for what they show.

To keep a camera unlit, give it a `layerMask` that shares no bit with `LightingSettings::layerMask`. To keep a light or occluder out of one camera, clear the bits of that camera's layers from the light's or occluder's `layerMask`.

Lighting applies to world layers only. The HUD on `ScreenLayer` and post effects are not lit.

## Cost

Lights without shadows, and shadow-casting lights with no occluder in reach, are drawn together in one pass per camera. Each shadow-casting light with an occluder inside its radius adds two passes, and each occluder edge facing away from it adds a few triangles. Lower `resolutionScale` when many shadow-casting lights share the screen.

## Limitations

Shadows are hard-edged. Sprites have no normal maps, so lighting does not show surface relief.
