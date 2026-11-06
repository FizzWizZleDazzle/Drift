# Render Showcase

**Source:** `examples/render_showcase/main.cpp`

A tour of the renderer with no image assets: every texture is generated in code.

## What It Shows

- Filled and outlined shapes, arcs, a concave star polygon, and a thick animated polyline.
- A grid of rotating sprites under the shapes, sorted by `zOrder`.
- A minimap: a second `Camera` with a render-target `target`, drawn on the HUD inside a nine-slice frame.
- A light map built in a view hook: glow sprites drawn additively in world space, then multiplied over the main camera's view.
- A post effect (vignette and scanlines) written once in GLSL and once in WGSL.
- A HUD on `ScreenLayer`: text, nine-slice panels, and the minimap texture.

## Controls

| Key | Action |
|-----|--------|
| Arrow keys | Move the camera |
| Q / E | Zoom out / in |
| 1 / 2 / 3 / 4 | Scale mode: Letterbox, Stretch, Integer, Expand |
| P | Toggle the post effect |
| L | Toggle the light map |
| F12 | Save `render_showcase.png` |

## Running

```bash
cmake --build build --target render_showcase
cd examples/render_showcase && ../../build/render_showcase
```

The HUD font is DejaVu Sans Mono, shipped in `assets/fonts` with its license.
