# Rendering

The `RendererResource` draws everything you see: sprites, shapes, text, tilemaps and particles. You queue draws during the frame; at the end of the frame the renderer sorts them by layer and z-order, batches them, renders every camera, applies post effects, draws the HUD layer, and presents the result.

**Header:** `#include <drift/resources/RendererResource.hpp>`

**Plugin:** `RendererPlugin` (included in `DefaultPlugins`)

## Automatic Sprite Rendering

With `DefaultPlugins`, every entity with a `Sprite` and a `Transform2D` is drawn for you. Use the `RendererResource` directly for shapes, debug visuals, custom meshes, or anything that is not an entity.

```cpp
void setup(Commands& cmd) {
    cmd.spawn().insert(
        Transform2D{.position = {400.f, 300.f}},
        Sprite{.texture = myTexture, .zOrder = 1.f}
    );
}
```

Draw from systems registered with `app.render<...>()`. Systems that use the renderer always run on the main thread, because the graphics context belongs to it.

## Textures

```cpp
TextureHandle loadTexture(const char* path, TextureOptions options = {});
bool          reloadTexture(TextureHandle texture, const char* path);
TextureHandle createTextureFromPixels(const uint8_t* rgba, int w, int h, TextureOptions options = {});
void          updateTexture(TextureHandle texture, int x, int y, int w, int h, const uint8_t* rgba);
void          setTextureOptions(TextureHandle texture, TextureOptions options);
void          destroyTexture(TextureHandle texture);
Vec2          textureSize(TextureHandle texture) const;
TextureHandle whiteTexture() const;
```

`loadTexture` reads PNG, JPG, BMP, TGA and GIF files. `TextureOptions` picks the sampling: `TextureFilter::Nearest` (the default, crisp pixel art) or `Linear` (smooth scaling), and `TextureWrap::Clamp`, `Repeat` or `Mirror`.

```cpp
TextureHandle bg = renderer->loadTexture("assets/clouds.png",
                                         {TextureFilter::Linear, TextureWrap::Repeat});
```

`updateTexture` replaces a region of an existing texture, which suits procedural textures, fog-of-war masks and minimaps drawn on the CPU.

`reloadTexture(texture, path)` reads an image file into an existing texture. The handle stays valid, so everything drawing it shows the new image, and the size may change. Hot reload uses it; see [Asset Server](asset-server.md#hot-reload).

## Sprites

`draw` takes a `SpriteDraw`; every field except `texture` has a default:

```cpp
renderer->draw({.texture = ship, .position = pos, .rotation = angle,
                .origin = {16.f, 16.f}, .zOrder = 5.f, .blend = BlendMode::Additive});
```

| Field | Meaning |
|-------|---------|
| `texture` | Texture to draw |
| `position` | World position of the origin |
| `srcRect` | Region of the texture in pixels; empty means the whole texture |
| `scale`, `rotation` | Scale and rotation (radians) around `origin` |
| `origin` | Pivot in source pixels, from the top-left |
| `tint` | Color multiplied with the texture |
| `flip` | `Flip::H`, `Flip::V`, or both |
| `zOrder` | Higher draws on top within a layer |
| `blend` | `BlendMode` (see below) |
| `material` | Custom shader material; see [Shaders and Effects](shaders-and-effects.md) |
| `layer` | Render layer, 0..30 for the world or `ScreenLayer` for the HUD |

The `drawSprite` overloads are shorthands for common cases, and `drawSpriteBatch` draws many same-texture quads centered on their positions, which particle systems use.

### Blend Modes

| Mode | Use |
|------|-----|
| `Alpha` | Normal transparency |
| `Additive` | Fire, glows, light |
| `Multiply` | Shadows, tinting, light maps |
| `Screen` | Soft brightening |
| `Premultiplied` | Textures with premultiplied alpha |
| `Opaque` | No blending |

### Nine-Slice

`drawNineSlice` stretches a panel texture to any size while keeping its corners intact. `borders` is `{left, top, right, bottom}` in source pixels.

```cpp
renderer->drawNineSlice(panelTex, Rect{20, 20, 300, 120}, {}, Vec4{8, 8, 8, 8});
```

### Meshes

`drawMesh` draws arbitrary textured triangles: terrain strips, deformable sprites, ribbons, water surfaces.

```cpp
RendererResource::MeshVertex v[3] = {
    {{0, 0}, {0, 0}}, {{100, 0}, {1, 0}}, {{50, 80}, {0.5f, 1}}};
uint32_t idx[3] = {0, 1, 2};
renderer->drawMesh(tex, v, 3, idx, 3);
```

## Shapes

Shapes use the same queue as sprites, so they sort and batch with everything else. Thickness is in world units.

```cpp
void drawLine(Vec2 start, Vec2 end, Color color, float thickness = 1, float zOrder = 0);
void drawPolyline(const Vec2* points, int32_t count, Color color, float thickness = 1,
                  bool closed = false, float zOrder = 0);
void drawRect(Rect rect, Color color, float thickness = 1, float zOrder = 0);
void drawRectFilled(Rect rect, Color color, float zOrder = 0);
void drawRoundedRectFilled(Rect rect, float radius, Color color, float zOrder = 0);
void drawCircle(Vec2 center, float radius, Color color, float thickness = 1,
                int segments = 0, float zOrder = 0);
void drawCircleFilled(Vec2 center, float radius, Color color, int segments = 0, float zOrder = 0);
void drawArc(Vec2 center, float radius, float startAngle, float endAngle, Color color,
             float thickness = 1, float zOrder = 0);
void drawTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Color color, float zOrder = 0);
void drawPolygonFilled(const Vec2* points, int32_t count, Color color, float zOrder = 0);
```

Polylines use mitered joins. `drawPolygonFilled` accepts concave polygons. Passing `segments = 0` picks a segment count from the radius.

## Layers and the HUD

Every draw has a layer. World layers 0..30 are drawn by cameras whose `layerMask` includes them; higher layers draw on top of lower ones. `ScreenLayer` is drawn last, in logical screen coordinates, without a camera, after post effects. Use it for HUDs and menus.

```cpp
void drawHud(ResMut<RendererResource> r) {
    r->setLayer(ScreenLayer);
    r->drawRectFilled({10, 10, 200 * health, 16}, Color::red());
    r->setLayer(0);
}
```

`setLayer` applies to the immediate draw calls that follow; `SpriteDraw` and the `Sprite` component carry their own `layer`. `pushClipRect` / `popClipRect` clip immediate draws to a rect in logical screen pixels, which scrolling HUD panels need.

## Cameras

Most games use the `Camera` component (see [Camera](../components/camera.md)); the first active camera that draws to the screen drives the renderer's default camera. For direct control:

```cpp
CameraHandle getDefaultCamera() const;
CameraHandle createCamera(const CameraDesc& desc = {});
void         setCamera(CameraHandle camera, const CameraDesc& desc);
CameraDesc   camera(CameraHandle camera) const;
void         destroyCamera(CameraHandle camera);
Vec2         screenToWorld(Vec2 screenPos) const;   // default camera
Vec2         worldToScreen(Vec2 worldPos) const;
Vec2         screenToWorld(CameraHandle camera, Vec2 screenPos) const;
Rect         cameraWorldBounds(CameraHandle camera) const;
```

A `CameraDesc` has a position, zoom, rotation, a normalized `viewport`, an `order`, a `layerMask`, an optional render-target `target`, and clear settings. Cameras render in ascending `order`. Draws outside a camera's visible bounds are culled.

```cpp
Vec2 world = renderer->screenToWorld(input->mousePosition());
```

## Render Targets

A render target is a texture you can render into and then draw like any other texture: minimaps, picture-in-picture, mirrors, portals, or a low-resolution layer.

```cpp
TextureHandle minimap = renderer->createRenderTarget(256, 160, {TextureFilter::Linear});
// Point a camera at it:
cmd.spawn().insert(Transform2D{}, Camera{.zoom = 0.2f, .active = true, .order = -1,
                                         .target = minimap, .clear = true});
// Then draw it on the HUD:
renderer->draw({.texture = minimap, .position = {1000, 16}, .layer = ScreenLayer});
```

## Resolution and Scale Modes

The game draws in a logical resolution (`Config::width` / `height`). The scale mode decides how that resolution maps onto the window:

| `ScaleMode` | Result |
|-------------|--------|
| `Letterbox` | Keeps the aspect ratio; bars fill the rest. The default. |
| `Stretch` | Fills the window, distorting the aspect ratio. |
| `Integer` | Pixel perfect: renders at exactly the logical size and scales by whole numbers. |
| `Expand` | Keeps pixels square and grows the logical size to fill the window, so wider screens see more of the world. |

```cpp
app.setConfig({.width = 320, .height = 180, .scaleMode = ScaleMode::Integer});
renderer->setScaleMode(ScaleMode::Letterbox);   // change at runtime
```

`logicalWidth()` and `logicalHeight()` report the size in effect this frame. Mouse positions from `InputResource` are already in logical coordinates, bars included. `windowToLogical` performs the same conversion for your own window coordinates.

## Clear Color

```cpp
renderer->setClearColor(ColorF{0.1f, 0.1f, 0.15f, 1.0f});
```

## Debugging

```cpp
renderer->captureScreenshot("shot.png");   // saves the next presented frame
int32_t draws   = renderer->drawCalls();    // last frame
int32_t sprites = renderer->spriteCount();  // last frame
const char* api = renderer->backendName();  // "gl" or "webgpu"
```

## Backends

Drift has two renderer backends. Games draw the same way on both, and the output is the same.

| Backend | Name | Desktop | Browser | Build option |
|---------|------|---------|---------|--------------|
| OpenGL | `"gl"` | OpenGL 3.3 | WebGL2 | `DRIFT_BACKEND_GL` (on by default) |
| WebGPU | `"webgpu"` | Vulkan, Metal or Direct3D 12 through [wgpu-native](https://github.com/gfx-rs/wgpu-native) | WebGPU | `DRIFT_BACKEND_WEBGPU` (off by default) |

OpenGL is the default and runs everywhere. Build with WebGPU when you want the modern native graphics APIs on desktop, or WebGPU in browsers that support it.

When a build includes more than one backend, choose with `Config::renderer` or the `DRIFT_RENDERER` environment variable; an empty choice picks OpenGL. If the requested backend cannot start (no WebGPU adapter, a browser without WebGPU), Drift logs why and falls back to the next one, so a WebGPU build still runs on machines and browsers that only have OpenGL or WebGL2.

```cpp
app.setConfig({.title = "My Game", .renderer = "webgpu"});
```

```bash
DRIFT_RENDERER=webgpu ./build/render_showcase
```

The WebGPU backend runs WGSL shaders only; see [Shaders and Effects](shaders-and-effects.md) for how to write shaders that work on both backends.

In the browser, a frame that has to wait for the GPU (taking a screenshot, or creating a shader during the render phase) presents what it has drawn so far; the ImGui overlay and anything else drawn later in that frame are skipped.
