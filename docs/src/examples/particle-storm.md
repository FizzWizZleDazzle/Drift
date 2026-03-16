# Particle Storm

**Source:** `examples/particle_storm/main.cpp`

A particle system stress test targeting 200--500 simultaneous emitters and 10k--50k live particles, exercising concurrent query performance and the particle cleanup path.

## What It Does

Click to spawn a single emitter orb. Right-click to spawn a cluster of five. Number keys 1--5 spawn wave patterns of 50--100 emitters each arranged in different formations (ring, spiral, grid, rain, vortex). Backspace mass-despawns all emitters at once. Mouse wheel zooms the camera. WASD pans the camera.

Each emitter orb orbits around its spawn point, cycles its color over time, and continuously emits particles. The scene quickly scales to thousands of active particles.

## Emission Shapes

The five patterns each use a different `EmissionShape`:

| Key | Pattern | Shape | Description |
|-----|---------|-------|-------------|
| 1 | Ring | `EmissionShape::Point` | 60 emitters in a circle, particles emit from a single point |
| 2 | Spiral | `EmissionShape::Point` | 50 emitters along an Archimedean spiral |
| 3 | Grid | `EmissionShape::Box` | 80 emitters in rows and columns, particles spawn within a rectangular area |
| 4 | Rain | `EmissionShape::Line` | 70 emitters across the top, particles spawn along a line and fall with gravity |
| 5 | Vortex | `EmissionShape::Circle` | 100 emitters clustered near center, particles spawn within a circular area in local space |

Example configuration for the rain pattern:

```cpp
EmitterConfig c;
c.shape = EmissionShape::Line;
c.shapeExtents = {60.f, 0.f};
c.gravity = {0.f, 80.f};
c.angle = {PI * 0.4f, PI * 0.6f};  // roughly downward
c.boundsMin = {-HALF_W, -HALF_H};
c.boundsMax = {HALF_W, HALF_H};
```

## Additive Blend Mode

Most emitter configs use additive blending for a glowing effect. The grid pattern uses alpha blending for contrast:

```cpp
// Additive (most patterns)
c.blendMode = ParticleBlendMode::Additive;

// Alpha (grid pattern)
c.blendMode = ParticleBlendMode::Alpha;
```

## Color Cycling

A dedicated `colorCycle` system updates emitter colors every frame, rotating hue over time:

```cpp
void colorCycle(Res<Time> time,
                QueryMut<Sprite, ParticleEmitter, ColorPhase> emitters) {
    emitters.iter([&](Sprite& sprite, ParticleEmitter& emitter, ColorPhase& phase) {
        phase.hue += phase.speed * dt;
        emitter.config.colorStart = hueToColor(phase.hue, 1.f);
        emitter.config.colorEnd = hueToColor(phase.hue + 0.15f, 0.f);
    });
}
```

## Concurrent Query Performance

The example deliberately registers multiple systems that run in parallel to stress-test the query system:

- **`orbitUpdate`** -- writes `Transform2D` on orbiters
- **`colorCycle`** -- writes `Sprite` and `ParticleEmitter`
- **`changeMonitor`** -- reads `Changed<Sprite>` concurrently with `colorCycle` writing `Sprite`
- **`positionMonitor`** -- reads `Transform2D` with `With<OrbiterTag>` filter

This exercises the engine's query mutex handling and change-tick synchronization under real contention.

## Local vs. World Space

The vortex pattern uses `ParticleSpace::Local`, meaning particles move relative to the emitter rather than in world space:

```cpp
c.space = ParticleSpace::Local;
```

When the emitter orb orbits, its particles orbit with it, creating a swirling vortex effect.

## Mass Despawn

Pressing Backspace despawns all orbiter entities in a single frame:

```cpp
if (input->keyPressed(Key::Backspace)) {
    orbiters.iterWithEntity([&](EntityId id, Transform2D&) {
        cmd.entity(id).despawn();
    });
}
```

This tests the particle system's cleanup path when hundreds of emitters are removed simultaneously.

## Auto-Cull

Like the Asteroid Field example, the HUD system tracks smoothed FPS and automatically despawns excess emitters when frame rate drops below 60:

```cpp
if (state->smoothFps < 60.f) {
    float pressure = (60.f - state->smoothFps) / 60.f;
    cullTarget = static_cast<int>(1.f + pressure * 15.f);
}
```

## Particle Count Tracking

The HUD reads the actual live particle count from the `ParticleSystemResource`:

```cpp
for (auto& [id, emState] : particles->emitters) {
    particleCount += emState.pool.count;
}
```

## Concepts Demonstrated

| Concept | Where |
|---------|-------|
| `EmissionShape` variants | Point, Box, Line, Circle |
| `ParticleBlendMode::Additive` vs `Alpha` | Per-pattern blend modes |
| `ParticleSpace::Local` | Vortex pattern |
| `Changed<T>` filter | Concurrent change detection |
| `With<T>` filter | Orbiter-only queries |
| Concurrent system execution | 4+ parallel query systems |
| Mass despawn stress test | Backspace clears all emitters |
| `ParticleSystemResource` | Direct particle pool inspection |
| `initialRotation` / `angularVelocity` | Vortex particle spin |
| Procedural textures | White square and circle textures |
