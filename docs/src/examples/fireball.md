# Fireball

**Source:** `examples/fireball/main.cpp`

A particle effects showcase. Drag the mouse to aim, release to launch a fireball that trails particles as it arcs through gravity, then explodes into a shower of sparks at the screen edge.

## What It Does

Click and drag to define a launch direction. On release, a fireball entity spawns at the drag start position with velocity proportional to the drag distance. The fireball is affected by gravity (490 px/s^2), trailing bright additive particles as it flies. When it hits any screen boundary, it despawns and spawns a one-shot explosion emitter.

## Particle Emitter as ECS Component

The fireball entity attaches a `ParticleEmitter` component directly:

```cpp
cmd.spawn().insert(
    Transform2D{.position = game->dragStart},
    Sprite{.texture = game->fireballTex, .origin = {8.f, 8.f},
           .tint = Color{255, 180, 50, 255}, .zOrder = 5.f},
    Fireball{.velocity = dir * 2.f},
    ParticleEmitter{.config = makeTrailConfig(game->particleTex)});
```

The emitter moves with the entity automatically because the particle system reads the entity's `Transform2D`.

## EmitterConfig

### Trail Config

The trail uses velocity inheritance so particles initially move in the fireball's direction, then slow down via drag:

```cpp
EmitterConfig c;
c.texture = tex;
c.spawnRate = 80.f;
c.maxParticles = 200;
c.lifetime = {0.3f, 0.6f};
c.speed = {20.f, 60.f};
c.angle = {0.f, 6.283f};
c.sizeStart = {6.f, 10.f};
c.sizeEnd = {1.f, 2.f};
c.colorStart = {1.f, 0.9f, 0.2f, 1.f};   // bright yellow
c.colorEnd = {1.f, 0.3f, 0.0f, 0.f};      // orange, fade to invisible
c.blendMode = ParticleBlendMode::Additive;
c.drag = 2.f;
c.velocityInheritance = 1.f;
```

Key fields:
- **`velocityInheritance`** -- particles inherit the emitter's velocity at spawn time, creating a natural trailing effect.
- **`drag`** -- decelerates particles over their lifetime.
- **`blendMode = Additive`** -- particles brighten overlapping areas, giving a glowing fire look.
- **`colorStart` / `colorEnd`** -- gradient from bright yellow to transparent orange.
- **`sizeStart` / `sizeEnd`** -- particles shrink as they age.

### Explosion Config

The explosion spawns a high burst of particles that scatter outward and fall with gravity:

```cpp
c.spawnRate = 5000.f;        // very high to fill quickly
c.maxParticles = 300;
c.gravity = {0.f, 300.f};    // sparks fall downward
c.drag = 0.5f;
c.duration = 0.1f;           // spawn for ~100ms then stop
c.looping = false;
c.boundsMin = {-HALF_W, -HALF_H};
c.boundsMax = { HALF_W,  HALF_H};
```

Key fields:
- **`duration` + `looping = false`** -- the emitter runs for 100ms, spawns all its particles, then stops. The entity remains alive while particles fade out, then the particle system cleans it up.
- **`gravity`** -- a constant acceleration applied to all particles (Vec2).
- **`boundsMin` / `boundsMax`** -- particles that leave this rectangle are killed early.

## Procedural Textures

Both the particle texture (4x4 white square) and the fireball sprite (16x16 filled circle) are generated from pixel arrays at startup:

```cpp
uint8_t white[4 * 4 * 4];
memset(white, 255, sizeof(white));
game->particleTex = renderer->createTextureFromPixels(white, 4, 4);
```

## Game Logic

The fireball is moved manually each frame (not via physics) with simple Euler integration:

```cpp
fireballs.iterWithEntity([&](EntityId id, Transform2D& t, Fireball& fb) {
    fb.velocity.y += GRAVITY * dt;
    t.position += fb.velocity * dt;

    if (out of bounds) {
        cmd.spawn().insert(
            Transform2D{.position = pos},
            ParticleEmitter{.config = makeExplosionConfig(game->particleTex)});
        cmd.entity(id).despawn();
    }
});
```

## Concepts Demonstrated

| Concept | Where |
|---------|-------|
| `ParticleEmitter` as ECS component | Attached directly to fireball entity |
| `EmitterConfig` customization | Trail and explosion configurations |
| `velocityInheritance` | Particles inherit emitter movement |
| `ParticleBlendMode::Additive` | Glowing fire effect |
| Particle `gravity` and `drag` | Physics-like particle behavior |
| `duration` + `looping = false` | One-shot burst emitters |
| Particle `boundsMin` / `boundsMax` | Spatial particle culling |
| Procedural textures | `createTextureFromPixels` |
| `iterWithEntity` | Accessing `EntityId` in query callbacks |
| `renderer->drawLine` / `drawCircle` | Debug/aim visualization |
