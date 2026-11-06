# Text

**Source:** `examples/text/main.cpp`

Every text feature on one screen, drawn with the built-in font and a small bitmap font.

## What It Shows

- The nine `align` / `valign` combinations, each label sitting on its anchor dot.
- One paragraph wrapped to a width, left, centered, and right aligned, framed by the box `measureText` returns.
- The built-in font from 10 px to 64 px.
- UTF-8 text in French, German, Polish, Czech, Russian, and Greek, plus kerning pairs and typographic punctuation.
- Outline, drop shadow, both together, and inline `[color=...]` tags.
- A 5x7 pixel font loaded from an AngelCode BMFont file, drawn at whole multiples of its size, outlined, and falling back to the built-in font for characters it lacks.
- `Text` entities: a centered title on `ScreenLayer`, a label in world space that rotates and moves with the camera, and a counter whose string changes every frame.

## Controls

| Key | Action |
|-----|--------|
| Arrow keys | Move the camera |
| Q / E | Zoom out / in |
| W | Switch the wrap width |
| F12 | Save `text.png` |

## Running

```bash
cmake --build build --target text
cd examples/text && ../../build/text
```

The pixel font is `assets/pixel5x7.fnt` with its page `assets/pixel5x7.png`, made for this example.
