# Particles

Drift provides a rich ECS-based particle system built around two types: the `ParticleEmitter` component and the `EmitterConfig` struct. Attach a `ParticleEmitter` to any entity with a `Transform2D` to emit particles from that entity's position.

## Components

### ParticleEmitter

```cpp
struct ParticleEmitter {
    EmitterConfig config;
    bool playing = true;
    bool oneShot = false;       // auto-destroy entity when finished
};
```

**Header:** `#include <drift/components/ParticleEmitter.hpp>`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `config` | `EmitterConfig` | (see below) | All particle behavior settings. |
| `playing` | `bool` | `true` | Whether the emitter is actively spawning particles. |
| `oneShot` | `bool` | `false` | When `true`, the entity is automatically despawned after the emitter finishes and all particles have died. Useful for explosions and impact effects. |

### EmitterConfig

`EmitterConfig` controls every aspect of particle behavior. All fields have sensible defaults, so you only need to set the ones you care about.

**Header:** `#include <drift/particles/EmitterConfig.hpp>`

#### Texture

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `texture` | `TextureHandle` | (invalid) | Texture used for each particle. A small white square works well -- tint with `colorStart`/`colorEnd`. |
| `srcRect` | `Rect` | `{}` | Source rectangle within the texture. `{}` uses the full texture. |

#### Emission

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `shape` | `EmissionShape` | `Point` | Where particles spawn. Options: `Point`, `Circle`, `Ring`, `Box`, `Line`. |
| `shapeRadius` | `float` | `0` | Radius for `Circle` and `Ring` shapes. |
| `shapeExtents` | `Vec2` | `{0,0}` | Half-size for `Box` shape, or direction vector for `Line` shape. |
| `spawnRate` | `float` | `10` | Particles emitted per second. |
| `maxParticles` | `int32_t` | `256` | Maximum alive particles. Once reached, new particles are not spawned until old ones die. |
| `bursts` | `vector<BurstEntry>` | `{}` | Timed burst emissions (see below). |

#### Lifetime and motion

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `lifetime` | `ValueRange` | `{0.5, 1.5}` | Random lifetime range in seconds. |
| `speed` | `ValueRange` | `{10, 50}` | Initial speed range in pixels per second. |
| `angle` | `ValueRange` | `{0, 2*PI}` | Emission angle range in radians. `{0, 6.283}` emits in all directions. |
| `velocityInheritance` | `float` | `0` | Fraction of the emitter's own velocity added to each particle (0 to 1). |

#### Forces

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `gravity` | `Vec2` | `{0, 0}` | Gravity acceleration applied to particles (pixels/s^2). |
| `drag` | `float` | `0` | Velocity damping. Each frame: `velocity *= (1 - drag * dt)`. |

#### Size over lifetime

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `sizeStart` | `ValueRange` | `{4, 4}` | Initial particle size range. |
| `sizeEnd` | `ValueRange` | `{1, 1}` | Final particle size range. Size interpolates linearly over the particle's lifetime. |

#### Rotation

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `initialRotation` | `ValueRange` | `{0, 0}` | Starting rotation range in radians. |
| `angularVelocity` | `ValueRange` | `{0, 0}` | Rotation speed range in radians per second. |

#### Color over lifetime

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `colorStart` | `ColorF` | `{1,1,1,1}` | Starting color (RGBA, 0-1 range). |
| `colorEnd` | `ColorF` | `{1,1,1,0}` | Ending color. The alpha channel fading to 0 creates a natural fade-out. |

#### Rendering

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `blendMode` | `ParticleBlendMode` | `Alpha` | `Alpha` for standard blending, `Additive` for glowing/fire effects. |
| `zOrder` | `float` | `10` | Draw order for the particle layer. |

#### Duration and looping

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `duration` | `float` | `-1` | How long the emitter spawns particles. `-1` means infinite. |
| `looping` | `bool` | `true` | Whether the emitter restarts after `duration` expires. |

#### Space and pre-warm

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `space` | `ParticleSpace` | `World` | `World` = particles stay where spawned. `Local` = particles move with the emitter. |
| `preWarmTime` | `float` | `0` | Simulates this many seconds at startup so particles are already spread out on frame 1. |

#### Bounds

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `boundsMin` | `Vec2` | `{0, 0}` | Minimum corner of the bounding area. |
| `boundsMax` | `Vec2` | `{0, 0}` | Maximum corner. When `boundsMin == boundsMax`, bounds are disabled. Particles outside the bounds are clamped. |

#### Seed

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `seed` | `uint32_t` | `0` | RNG seed for deterministic particle behavior. `0` means random. |

## Emission shapes

The `shape` field controls where new particles appear relative to the emitter position:

| Shape | Parameters | Description |
|-------|-----------|-------------|
| `Point` | none | All particles spawn at the emitter's position. |
| `Circle` | `shapeRadius` | Particles spawn at random positions inside a circle. |
| `Ring` | `shapeRadius` | Particles spawn on the circumference of a circle. |
| `Box` | `shapeExtents` | Particles spawn randomly inside a rectangle. `shapeExtents` is the half-size. |
| `Line` | `shapeExtents` | Particles spawn along a line. `shapeExtents` defines the direction and length. |

## Bursts

`BurstEntry` lets you emit a large number of particles at specific times:

```cpp
struct BurstEntry {
    float time = 0.f;       // seconds into emitter lifecycle
    int32_t count = 10;     // particles per burst
    int32_t cycles = 1;     // 0 = infinite repeats
    float interval = 0.f;   // seconds between cycles
};
```

Example -- a repeating burst every 0.5 seconds:

```cpp
config.bursts.push_back(BurstEntry{
    .time = 0.f,
    .count = 20,
    .cycles = 0,        // infinite
    .interval = 0.5f
});
```

## ValueRange

Several config fields use `ValueRange` for randomized values:

```cpp
struct ValueRange {
    float min = 0.f, max = 0.f;
};
```

You can construct it with a single value (both min and max equal) or a range:

```cpp
ValueRange{5.f}          // always 5
ValueRange{2.f, 8.f}     // random between 2 and 8
```

## Basic usage

### Simple particle trail

```cpp
void setup(ResMut<RendererResource> renderer, Commands& cmd) {
    // Create a small white texture for particles
    uint8_t white[4 * 4 * 4];
    memset(white, 255, sizeof(white));
    TextureHandle tex = renderer->createTextureFromPixels(white, 4, 4);

    EmitterConfig config;
    config.texture = tex;
    config.spawnRate = 50.f;
    config.lifetime = {0.5f, 1.0f};
    config.speed = {10.f, 30.f};
    config.colorStart = {0.2f, 0.6f, 1.f, 1.f};
    config.colorEnd = {0.2f, 0.6f, 1.f, 0.f};

    cmd.spawn().insert(
        Transform2D{.position = {400.f, 300.f}},
        ParticleEmitter{.config = config}
    );
}
```

### Fire effect

```cpp
EmitterConfig fire;
fire.texture = particleTex;
fire.spawnRate = 100.f;
fire.maxParticles = 500;
fire.lifetime = {0.3f, 0.8f};
fire.speed = {30.f, 80.f};
fire.angle = {4.0f, 5.4f};            // upward cone
fire.sizeStart = {8.f, 12.f};
fire.sizeEnd = {2.f, 4.f};
fire.colorStart = {1.f, 0.9f, 0.2f, 1.f};   // bright yellow
fire.colorEnd = {1.f, 0.2f, 0.0f, 0.f};     // orange, fade out
fire.blendMode = ParticleBlendMode::Additive;
fire.drag = 1.f;
```

### Explosion (one-shot)

```cpp
EmitterConfig explosion;
explosion.texture = particleTex;
explosion.spawnRate = 5000.f;           // burst everything quickly
explosion.maxParticles = 300;
explosion.lifetime = {0.8f, 2.0f};
explosion.speed = {80.f, 300.f};
explosion.angle = {0.f, 6.283f};       // all directions
explosion.sizeStart = {8.f, 14.f};
explosion.sizeEnd = {2.f, 4.f};
explosion.colorStart = {1.f, 0.95f, 0.3f, 1.f};
explosion.colorEnd = {0.8f, 0.2f, 0.0f, 0.f};
explosion.gravity = {0.f, 300.f};
explosion.drag = 0.5f;
explosion.blendMode = ParticleBlendMode::Additive;
explosion.duration = 0.1f;             // spawn for 100ms
explosion.looping = false;

cmd.spawn().insert(
    Transform2D{.position = hitPos},
    ParticleEmitter{.config = explosion, .oneShot = true}
);
```

### Snow

```cpp
EmitterConfig snow;
snow.texture = particleTex;
snow.shape = EmissionShape::Box;
snow.shapeExtents = {640.f, 0.f};       // wide horizontal box
snow.spawnRate = 40.f;
snow.maxParticles = 500;
snow.lifetime = {3.f, 6.f};
snow.speed = {5.f, 20.f};
snow.angle = {1.2f, 1.9f};              // mostly downward
snow.sizeStart = {2.f, 5.f};
snow.sizeEnd = {2.f, 5.f};
snow.colorStart = {1.f, 1.f, 1.f, 0.8f};
snow.colorEnd = {1.f, 1.f, 1.f, 0.f};
snow.drag = 0.3f;
```

### Ring emission shape

```cpp
EmitterConfig ring;
ring.texture = particleTex;
ring.shape = EmissionShape::Ring;
ring.shapeRadius = 50.f;
ring.spawnRate = 60.f;
ring.lifetime = {0.5f, 1.0f};
ring.speed = {5.f, 15.f};
ring.sizeStart = {4.f, 6.f};
ring.sizeEnd = {1.f, 2.f};
ring.colorStart = {0.f, 1.f, 0.5f, 1.f};
ring.colorEnd = {0.f, 1.f, 0.5f, 0.f};
```

## Controlling emitters at runtime

Toggle emission on and off via queries:

```cpp
void toggleParticles(Res<InputResource> input,
                     QueryMut<ParticleEmitter> emitters) {
    if (input->keyPressed(Key::P)) {
        emitters.iter([](ParticleEmitter& pe) {
            pe.playing = !pe.playing;
        });
    }
}
```

## Pre-warming

Set `preWarmTime` to simulate the emitter for a number of seconds at startup. This avoids the "empty start" look when the emitter first appears:

```cpp
config.preWarmTime = 2.f;   // particles are already spread out on frame 1
```

## Notes

- `ParticleEmitter` requires a `Transform2D` on the same entity to define where particles spawn.
- When `oneShot` is `true` and `looping` is `false`, the entity is despawned after the emitter's duration expires and all particles have died. This is the standard pattern for explosions and impacts.
- The additive blend mode (`ParticleBlendMode::Additive`) creates a glowing effect where overlapping particles become brighter. Use it for fire, sparks, and energy effects.
- `velocityInheritance` transfers the emitter entity's motion to newly spawned particles. A value of `1.0` means particles inherit the full velocity of the emitter, which creates natural-looking trails behind moving objects.
- Particle rendering is batched internally for performance. Thousands of particles are typically fine.
