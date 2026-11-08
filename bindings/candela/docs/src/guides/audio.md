# Audio

Drift plays sound effects, music and positional sound through a mixer of buses. Load a sound once, then play it from any system.

## Sound effects

Load sounds in startup and keep them in a resource:

```rust
@resource
struct Sfx {
    jump: Sound = sound("assets/jump.wav"),
    coin: Sound = sound("assets/coin.ogg"),
}

@system
fn jump_sound(input: Input, sfx: Sfx) {
    if input.action_pressed("jump") {
        sfx.jump.play();
    }
}
```

`sound(path)` reads WAV, OGG and MP3, and decodes the whole file at load; it works in field defaults, so a resource loads its own sounds. `assets.create_sound(samples, rate)` makes a sound from samples in -1 to 1, for generated blips.

`play()` plays at full volume on the sfx bus. `play_with` takes a `PlayOptions` for the rest:

```rust
sfx.coin.play_with(PlayOptions { volume: 0.6, pitch: random::float_range(0.9, 1.1), .. });
```

Both return a `Voice`, one playback you can `stop`, `pause`, `resume`, fade with `set_volume(v, fade)`, or move with `set_pan` and `set_pitch`. `playing()` says whether it is still going. When every voice is busy, a sound with a higher `priority` takes the place of a lower one.

## Music

`assets.music(path)` loads a track that decodes while it plays, so long files cost little memory. `Audio` plays one music track at a time, on the music bus:

```rust
@system(schedule: Schedule::Startup)
fn start_music(assets: Assets, audio: Audio) {
    audio.play_music(assets.music("assets/theme.ogg"), 0.0, true);
}
```

Playing another track crossfades over the seconds you pass. `stop_music`, `pause_music` and `resume_music` take a fade time too.

## Buses

Every sound plays on a bus. `music` and `sfx` sit under `master`, and a bus's volume, mute and pause apply to everything below it. Make your own with `audio.bus(name, parent)`, for example a `voice` bus under `master` with its own slider in the options menu.

The built-in buses start at the volumes in `drift.toml`, from 0 to 1:

```toml
[audio]
master_volume = 0.8
music_volume = 0.5
sfx_volume = 1.0
```

Change them while running with `set_volume(v, fade)`, for example to duck the music under a dialog:

```rust
audio.music_bus().set_volume(0.3, 0.5);
```

Pausing the sfx bus pauses the game's sounds for a pause menu while the music keeps playing.

## Sounds from entities

An `AudioSource` plays a sound from an entity, and keeps playing it while the entity lives:

```rust
cmd.spawn([at(400.0, 200.0), AudioSource { sound: sfx.fire, looping: true, autoplay: true,
                                           spatial: true, min_distance: 64.0, max_distance: 900.0, .. }]);
```

With `spatial` on, the sound gets quieter with distance from the listener, silent past `max_distance` and at full volume inside `min_distance`, and pans with its horizontal offset. The listener is the entity with an `AudioListener`, or the main camera without one. The engine reads the source's volume, pitch and looping every frame, so change a playing source by writing its component.

## Without an audio device

When no output device opens, `audio.has_device()` is `false` and sounds advance silently, so the game runs the same on a machine with no sound card.

See [Engine Resources](../reference/resources.md#audio) for every method.
