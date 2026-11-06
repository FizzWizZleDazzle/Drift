# Audio

**Source:** `examples/audio/main.cpp`

Spatial sound sources, mixer buses and music in one scene. Every sound is synthesized in code with `AudioResource::createSound`, so the example ships no audio files.

## What It Shows

- Three looping `AudioSource` entities (an engine hum, a chime and a drone) with `spatial` on, all on a user-created `ambience` bus. Each is drawn with its `minDistance` and `maxDistance` rings, and a line to the listener whose thickness follows the gain.
- An `AudioListener` entity you move around; the panel lists the gain and pan `spatialize` computes for each source.
- Bus controls for master, music, sfx and ambience: volume sliders, mute, pause, and a two-second fade out.
- Music: two arpeggio tracks with two-second crossfades, an intro that hands over to a looping track, and a crossfaded playlist with a Next button.
- A blip on the sfx bus with random pitch and a cap of four simultaneous instances.

## Controls

| Input | Action |
|-------|--------|
| WASD | Move the listener |
| Left mouse drag | Move a source |
| Space | Play a blip |
| Panel | Bus volumes, mute, pause, music |

## Running

```bash
cmake --build build --target audio
./build/audio
```
