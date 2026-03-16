# Asteroid Field

**Source:** `examples/asteroid_field/main.cpp`

A top-down space shooter and stress test targeting 500--2000 simultaneous physics entities with dynamic spawning, destruction, particle effects, trail renderers, and camera shake.

## What It Does

The player controls a ship with WASD and aims with the mouse. Left-click fires bullets; holding the button enables auto-fire. Asteroids spawn continuously from screen edges and drift across the play area. Bullets destroy asteroids, which split into smaller fragments (large splits into 3 medium, medium into 2 small). Explosions emit particles and shake the camera. Press F to toggle frenzy mode (3x spawn rate, faster fire, bullet spread).

## Procedural Textures

All visuals are generated at startup from pixel data -- no image files are needed:

```cpp
game->asteroidTex[0] = makeCircleTexture(renderer.get(), 24, game->rng);
game->bulletTex = makeWhiteSquare(renderer.get(), 4);
game->playerTex = makeDiamondTexture(renderer.get(), 16);
```

The `makeCircleTexture` function creates a rocky-looking circle with randomized gray values and rough alpha edges. `createTextureFromPixels` uploads raw RGBA pixel arrays to the GPU.

## Collision Filters

Collision categories prevent bullets from hitting the player and asteroids from hitting each other:

```cpp
static constexpr uint32_t CAT_PLAYER   = 0x0001;
static constexpr uint32_t CAT_BULLET   = 0x0002;
static constexpr uint32_t CAT_ASTEROID = 0x0004;

// Bullets collide only with asteroids
CircleCollider2D{.radius = 2.f, .filter = {CAT_BULLET, CAT_ASTEROID}}

// Asteroids collide only with bullets
CircleCollider2D{.radius = radius, .filter = {CAT_ASTEROID, CAT_BULLET}}
```

## Trail Renderers

Bullets use the `TrailRenderer` component to draw fading motion trails:

```cpp
TrailRenderer{.width = 2.f,
              .lifetime = 0.25f,
              .colorStart = {1.f, 1.f, 0.8f, 0.7f},
              .colorEnd = {1.f, 0.5f, 0.1f, 0.f},
              .minDistance = 4.f,
              .maxPoints = 32,
              .zOrder = 4.f}
```

## Camera Shake

The camera entity has a `CameraShake` component. On each asteroid destruction, the shake intensity is re-triggered proportional to the asteroid's size:

```cpp
cmd.entity(game->cameraId)
    .insert(CameraShake{.intensity = 1.5f + size * 0.3f,
                        .decay = 6.f,
                        .frequency = 25.f});
```

The camera also uses `CameraFollow` to smoothly track the player:

```cpp
cmd.entity(game->cameraId)
    .insert(CameraFollow{.target = game->playerId, .smoothing = 8.f});
```

## Particle Explosions

When an asteroid is destroyed, a one-shot particle emitter spawns at its position:

```cpp
cmd.spawn()
    .insert(Transform2D{.position = pos},
            ParticleEmitter{.config = makeExplosionConfig(game->particleTex, size),
                            .oneShot = true});
```

The emitter uses additive blending, a short burst duration (0.08s), and drag to slow the sparks.

## Dynamic Spawning and Despawning

Asteroids spawn on a timer. When destroyed, they split into smaller fragments. Out-of-bounds entities are cleaned up every frame:

```cpp
asteroidQuery.iterWithEntity([&](EntityId id, Transform2D& t, Asteroid&) {
    if (t.position.x < -HALF_W - farMargin || ...) {
        cmd.entity(id).despawn();
    }
});
```

## Auto-Cull for Performance

The HUD system tracks a smoothed FPS value. When FPS drops below 60, it automatically despawns excess asteroids to maintain frame rate:

```cpp
if (game->smoothFps < 60.f) {
    float pressure = (60.f - game->smoothFps) / 60.f;
    cullTarget = static_cast<int>(1.f + pressure * 20.f);
}
```

## System Ordering

Systems use explicit `.after()` ordering to ensure correct execution:

```cpp
app.update<asteroidSpawn>("asteroid_spawn");
app.update<playerUpdate>("player_update");
app.update<collisionSystem>("collision_system").after("player_update");
app.update<asteroidCleanup>("asteroid_cleanup").after("collision_system");
```

## Concepts Demonstrated

| Concept | Where |
|---------|-------|
| Large-scale ECS performance | 500--2000 physics entities |
| `CollisionFilter` categories | Bullet/asteroid/player isolation |
| `EventReader<CollisionStart>` | Collision response system |
| `TrailRenderer` component | Bullet motion trails |
| `CameraShake` | Screen shake on destruction |
| `CameraFollow` | Smooth camera tracking |
| `ParticleEmitter` with `oneShot` | Explosion effects |
| Procedural textures | `createTextureFromPixels` |
| `iterWithEntity` | Queries that need the `EntityId` |
| `.after()` system ordering | Sequential dependency chains |
| `Velocity2D` | Physics-driven movement |
| Dynamic spawn/despawn | Continuous entity lifecycle |
