# Lighting

**Source:** `examples/lighting/main.cpp`

A dark stone floor lit by moving colored lights, with walls, pillars and a spinning crate casting hard shadows. Every effect in the post effect library can be toggled from the keyboard. All textures are generated in code.

## What It Shows

- `LightingPlugin` added on top of `DefaultPlugins`, with a dark blue `LightingSettings::ambient`.
- Three `PointLight2D` lights orbiting the room, each carrying an additive bulb sprite.
- A `SpotLight2D` that sweeps back and forth by rotating its entity.
- A shadowless white light that follows the mouse.
- Occluders of every shape: brick walls (`box`), pillars (`circle`), a rotating triangle (`polygon`), and a wall whose shadow comes from its `BoxCollider2D` (`fromCollider`).
- Every library post effect added at startup and disabled, then toggled with `PostEffects::setEnabled`. The color grading uses a warm LUT built with `PostEffects::createColorLut`.
- `ScreenFade` driven by `flash` and `fadeTo`.

## Controls

| Key | Action |
|-----|--------|
| Mouse | Move the white light |
| 1 | Bloom |
| 2 | Vignette |
| 3 | CRT |
| 4 | Color grading |
| 5 | Pixelate |
| 6 | Grayscale |
| 7 | Sepia |
| 8 | Blur |
| F | White screen flash |
| G | Fade to black and back |
| L | Lighting on or off |
| F12 | Save `lighting.png` |

## Running

```bash
cmake --build build --target lighting
./build/lighting
```
