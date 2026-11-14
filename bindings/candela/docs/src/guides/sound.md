# Sound and Music

Drift plays sound effects, music and positional sound through a mixer of buses. Load a sound once, then play it from any system.

## Before you start

- Put sound files in the game folder, for example `assets/jump.wav`; everything under `assets/` goes into the web page.
- Browsers keep a page silent until the player clicks, taps or presses a key on it. Sounds played before that are skipped, so start the music from a title screen that waits for a key; see [Publishing](publishing.md#sound).
- Positional sound needs an entity with an `AudioListener`; see [Sounds from entities](#sounds-from-entities).

This program makes its sounds from samples, so it runs without any files: a blip on Space, and a hum that pans as its circle drifts past the listener. [Run it in the browser](../play/gallery/sound/blips/) and click it first.

```rust
{{#include ../../gallery/sound/blips/main.cdl:load}}

{{#include ../../gallery/sound/blips/main.cdl:play}}
```

<details>
<summary>The whole program</summary>

```rust
{{#include ../../gallery/sound/blips/main.cdl:all}}
```

</details>

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

`sound(path)` reads WAV, OGG and MP3, and decodes the whole file at load; it works in field defaults, so a resource loads its own sounds.

A missing file logs `Failed to read sound 'assets/jump.wav'` and throws `host_fn_error`. In a resource's field default, as above, that stops the whole game before it starts: the page shows "The game could not start" with `Sfx: building its defaults raised host_fn_error: ... drift_load_failed: assets/jump.wav`. Check the path when you see it. To keep going without the sound, load it in a Startup system inside `try` and `catch e` instead (`catch "drift_load_failed"` does not match). `assets.create_sound(samples, rate)` makes a sound from samples in -1 to 1, for generated blips.

`play()` plays at full volume on the sfx bus. `play_with` takes a `PlayOptions` for the rest:

```rust
sfx.coin.play_with(PlayOptions { volume: 0.6, pitch: 1.2, .. });
```

Both return a `Voice`, one playback you can `stop`, `pause`, `resume`, fade with `set_volume(v, fade)`, or move with `set_pan` and `set_pitch`. `playing()` says whether it is still going. Keep the voice in a resource to control a looping sound later:

```rust
@resource
struct Engine { hum: Voice = Voice { .. } }

@system(schedule: Schedule::Startup)
fn start_engine(sfx: Sfx, engine: Engine) {
    engine.hum = sfx.engine.play_with(PlayOptions { looping: true, volume: 0.4, .. });
}

@system
fn rev(input: Input, engine: Engine) {
    engine.hum.set_pitch(1.0 + input.axis("throttle") * 0.5);
}
```

| `PlayOptions` field | Default | Meaning |
|---------------------|---------|---------|
| `volume` | 1.0 | 0 to 1 |
| `pitch` | 1.0 | Playback speed: 2.0 is an octave up and twice as fast, 0.5 an octave down |
| `looping` | false | Plays again when it ends, until stopped |
| `pan` | 0.0 | -1 left to 1 right |
| `bus` | the sfx bus | The bus it plays on |
| `fade_in` | 0.0 | Seconds to fade in |
| `priority` | 0 | When every voice is busy, a sound with a higher priority takes the place of a lower one | `sound.set_max_instances(n)` keeps a sound from piling up, such as footsteps; starting one more fades out the oldest.

## Music

`music(path)` loads a track that decodes while it plays, so long files cost little memory; like `sound`, it works in field defaults. `Audio` plays one music track at a time, on the music bus:

```rust
@resource
struct Tracks { theme: Sound = music("assets/theme.ogg"), boss: Sound = music("assets/boss.ogg") }

@system(schedule: Schedule::Startup)
fn start_music(tracks: Tracks, audio: Audio) {
    audio.play_music(tracks.theme, 0.0, true);
}
```

Playing another track crossfades over the seconds you pass: `audio.play_music(tracks.boss, 2.0, true)`. `stop_music`, `pause_music` and `resume_music` take a fade time too.

`play_playlist(tracks, crossfade, looping, shuffle)` plays a list of tracks one after another, gapless with a crossfade of 0, and `next_track()` skips ahead.

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

| `AudioSource` field | Default | Meaning |
|---------------------|---------|---------|
| `sound` | none | The sound to play |
| `volume`, `pitch`, `looping`, `bus`, `priority` | 1.0, 1.0, false, sfx, 0 | As in `PlayOptions` |
| `autoplay` | true | Starts when the entity is spawned |
| `play` | false | Set it to start the sound again |
| `spatial` | false | Volume and pan follow the position relative to the listener |
| `min_distance`, `max_distance` | 64.0, 1024.0 | Pixels: full volume inside the first, silent past the second |
| `rolloff` | `Rolloff::Linear` | How the volume falls between the two |

With `spatial` on, the sound gets quieter with distance from the listener, silent past `max_distance` and at full volume inside `min_distance`, and pans with its horizontal offset. The listener is the first entity with an active `AudioListener`, usually the camera or the player; without one, spatial sources play unchanged. The engine reads the source's volume, pitch and looping every frame, so change a playing source by writing its component.

## Without an audio device

When no output device opens, `audio.has_device()` is `false` and sounds advance silently, so the game runs the same on a machine with no sound card.

Browsers start sound only after the player clicks or presses a key on the page; until then sounds advance silently.

Every example in this book makes its sounds from samples with `assets.create_sound`, so it runs without sound files; [Breakout](../examples/breakout.md) plays a square-wave blip at different pitches. See [`Audio`](../reference/resources.md#audio), [`Sound`](../reference/assets.md#sound) and [`AudioSource`](../reference/components.md#audiosource) for every method and field.
