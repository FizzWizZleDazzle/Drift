# Particles and Effects

Particles, trails, 2D lights and screen effects make a game feel alive. All of them are components or calls on `Effects`, and the engine runs them; your systems only start and stop them.

## Particle emitters

A `ParticleEmitter` component emits particles from its entity for as long as it is `playing`:

```rust
@system(schedule: Schedule::Startup)
fn torch(cmd: Commands) {
    cmd.spawn([
        at(200.0, 300.0),
        ParticleEmitter {
            rate: 40.0,
            lifetime_min: 0.4, lifetime_max: 0.8,
            speed_min: 20.0, speed_max: 60.0,
            angle_min: -2.0, angle_max: -1.1,       // radians; -Y is up
            size_start: 6.0, size_end: 1.0,
            color_start: rgb(255, 200, 80), color_end: rgba(255, 60, 20, 0),
            gravity: vec2(0.0, -40.0),
            blend: Blend::Additive,
            ..
        },
    ]);
}
```

- `shape` picks where particles start: `EmitShape::Point`, `Circle` and `Ring` (with `shape_radius`), `Box` (with `shape_extents` as half-size) or `Line` (with `shape_extents` as direction).
- `max_particles` caps how many are alive at once.
- `duration` with `looping: false` makes an emitter that stops by itself.
- `texture` draws each particle with an image (`frame` picks part of it); without one they are soft squares tinted by the colors.
- `size_start_max`, `rotation_min`/`max` and `spin_min`/`max` vary each particle; `local: true` keeps particles moving with the emitter; `prewarm` starts an emitter as if it had already run that many seconds, so a torch is lit from the first frame.
- `bursts` fire many particles at once at set times: `Burst { time: 0.0, count: 30, cycles: 3, interval: 0.5 }` fires 30 three times, half a second apart.

Turn an emitter on and off by writing `playing`, and start one over, bursts and all, with `effects.restart_particles(emitter)`:

```rust
@system
fn thrusters(ship: (Ship, ParticleEmitter), input: Input) {
    let (_, emitter) = ship;
    emitter.playing = input.action_held("thrust");
}
```

## Bursts

For a one-off puff, an explosion or a pickup sparkle, call `burst` on `Effects`. It spawns an emitter, fires `count` particles at once and removes it when the last one dies:

```rust
@system
fn explode(hit: Contact<Bullet, Rock>, places: Lookup<Transform>, effects: Effects) {
    if places.get(hit.b) is Some(t) {
        effects.burst(t.position, ParticleEmitter {
            speed_min: 80.0, speed_max: 220.0, lifetime_max: 0.6,
            color_start: rgb(255, 240, 200), color_end: rgba(255, 80, 0, 0), ..
        }, 40);
    }
}
```

## Trails

A `Trail` leaves a fading ribbon behind a moving entity: bullets, swords, comets.

```rust
cmd.spawn([at(x, y), Velocity { linear: vec2(400.0, 0.0), .. },
           Trail { width: 6.0, lifetime: 0.3, color_start: rgb(120, 200, 255), .. }]);
```

## Lights

Lighting turns on when `drift.toml` has a `[lighting]` section. The scene starts at the `ambient` color and each light adds to it:

```toml
[lighting]
ambient = [0.1, 0.1, 0.18]
```

Give an entity a `PointLight` to light everything around it, or a `SpotLight` for a cone:

```rust
cmd.spawn([at(400.0, 300.0), PointLight { color: rgb(255, 180, 90), intensity: 1.5, radius: 300.0, .. }]);
cmd.spawn([at(100.0, 100.0), SpotLight { radius: 450.0, direction: 0.8, angle: 0.6, softness: 0.3, .. }]);
```

A `LightOccluder` blocks light from every light with `shadows` on, and casts a hard shadow. `OccluderShape::FromCollider` takes the shape of the entity's collider, so walls cast shadows with one extra component:

```rust
cmd.spawn([at(300.0, 200.0), Rectangle { size: vec2(64.0, 64.0), .. },
           BoxCollider { half_size: vec2(32.0, 32.0), .. },
           LightOccluder { shape: OccluderShape::FromCollider, .. }]);
```

Change the ambient light while the game runs with `effects.set_ambient(color)`, for a day and night cycle or a room whose lights go out, and switch lighting off and on with `effects.set_lighting(on)`. `OccluderShape::Polygon` blocks light with up to 16 `points` of your own.

Light only darkens the scene or leaves it as it is: a sprite under full white light shows its own colors. For a glowing light source, pair the light with an additive sprite and bloom.

## Post effects

Post effects run on the whole picture after every camera has drawn, before the HUD. Each call on `Effects` adds one and returns an `Effect`:

```rust
@resource
struct Looks { crt: drift::Effect }

@system(schedule: Schedule::Startup)
fn looks(cmd: Commands, effects: Effects) {
    effects.bloom(0.7, 1.0);
    effects.vignette(0.5, 0.75);
    cmd.insert_resource(Looks { crt: effects.crt(0.15, 0.35) });
}

@system
fn toggle_crt(input: Input, looks: Looks) {
    if input.key_pressed(Key::F2) {
        looks.crt.set_enabled(!looks.crt.enabled());
    }
}
```

| Call | Effect |
|------|--------|
| `bloom(threshold, intensity)` | Bright parts glow |
| `vignette(intensity, radius)` | Darker edges |
| `crt(curvature, scanlines)` | Curved screen with scanlines |
| `grayscale(amount)`, `sepia(amount)` | Color to gray or brown, 0 to 1 |
| `pixelate(size)` | Blocks of `size` pixels |
| `blur(radius, iterations)` | Blurs the whole picture, for pause menus |

Each kind of effect has its own place in the chain (bloom before color grading, CRT near the end), whatever order you add them in; `set_order` on an `Effect` moves it, and `remove()` deletes it. For every setting of an effect, add its settings struct and change it later with `set`:

```rust
let glow = effects.add(Bloom { threshold: 0.5, intensity: 1.4, levels: 6, tint: rgb(255, 230, 200), .. });
glow.set(Bloom { threshold: 0.5, intensity: 2.5, .. });
effects.add(ColorGrading { saturation: 0.6, brightness: 0.9, .. });
```

See [Engine Components](../reference/resources.md#bloom) for every field of the settings structs.

## Shake, flash and fade

```rust
effects.shake(camera, 8.0);              // a camera entity; the shake dies away
effects.flash(rgb(255, 255, 255), 0.2);  // full white, back to normal over 0.2 s
effects.fade_to(rgb(0, 0, 0), 1.0, 0.5); // fade to black over 0.5 s, and stay
effects.fade_to(rgb(0, 0, 0), 0.0, 0.5); // fade back in
```

`fade_to` with an `amount` between 0 and 1 holds the screen part of the way to the color, for a dim pause screen.

The [Effects Showcase](../examples/effects.md) puts emitters, bursts, flashes, post effects, a trail and tweens on one screen. See [`ParticleEmitter`](../reference/components.md#particleemitter) and [`Effects`](../reference/resources.md#effects) for every field and method.
