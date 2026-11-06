# Shaders and Effects

Custom shaders change how sprites and the whole scene look: water, dissolve, outlines, flashes on hit, CRT screens, color grading. Drift keeps the vertex stage fixed and lets you write the fragment stage, so a shader is one short function.

**Header:** `#include <drift/resources/RendererResource.hpp>`

## Writing a Shader

A shader receives the interpolated vertex data and returns a color. Write it once in WGSL and let the build translate it for OpenGL and WebGL2 (see [Portable Shaders](#portable-shaders)), or write GLSL directly if your game only uses the OpenGL backend. A shader may carry both; the renderer picks the source the active backend understands.

GLSL: write `main()` and assign `o_color`. Drift prepends a prelude that declares everything below.

```glsl
void main() {
    vec4 c = texture(u_texture0, v_uv) * v_color;
    float flash = material.params[0].x;          // 0..1
    o_color = vec4(mix(c.rgb, vec3(1.0), flash), c.a);
}
```

WGSL: write `fn fragment(in: FragmentInput) -> vec4<f32>`.

```wgsl
fn fragment(in: FragmentInput) -> vec4<f32> {
    let c = textureSample(texture0, sampler0, in.uv) * in.color;
    let flash = material.params[0].x;
    return vec4<f32>(mix(c.rgb, vec3<f32>(1.0), flash), c.a);
}
```

### What a Shader Can Read

| GLSL | WGSL | Meaning |
|------|------|---------|
| `v_uv` | `in.uv` | Texture coordinate |
| `v_color` | `in.color` | Vertex color (the sprite's tint) |
| `v_world` | `in.world` | World position of the fragment |
| `u_texture0` | `texture0`, `sampler0` | The texture being drawn |
| `u_texture1..3` | `texture1..3`, `sampler1..3` | Extra textures set on the material |
| `material.params[0..7]` | `material.params[0..7]` | Eight `vec4` values set on the material |
| `frame.time.x` | `frame.time.x` | Seconds since start |
| `frame.time.y` | `frame.time.y` | Frame delta in seconds |
| `frame.time.zw` | `frame.time.zw` | Size of the render target in pixels |
| `frame.view_proj` | `frame.view_proj` | World to clip transform |

## Creating Shaders

From strings:

```cpp
ShaderDesc desc;
desc.glslFragment = flashGLSL;
desc.wgsl = flashWGSL;          // optional
ShaderHandle flash = renderer->createShader(desc);
```

From files: `loadShader("shaders/flash")` reads `shaders/flash.wgsl`, `shaders/flash.frag.glsl` and `shaders/flash.vert.glsl` when they exist.

### Portable Shaders

`drift_add_shaders` translates WGSL shaders to GLSL at build time with [naga](https://github.com/gfx-rs/wgpu/tree/trunk/naga) (`cargo install naga-cli`). For each `<name>.wgsl` it writes `<name>.wgsl`, `<name>.vert.glsl` and `<name>.frag.glsl` into `OUTPUT_DIR`, ready for `loadShader`:

```cmake
drift_add_shaders(my_game
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders
    SHADERS shaders/water.wgsl shaders/dissolve.wgsl)
# Web builds: ship the translated files too.
drift_web_app(my_game ASSETS assets PRELOAD ${CMAKE_CURRENT_BINARY_DIR}/shaders@/shaders)
```

With `OUTPUT_DIR` in the build directory the files sit next to the executable, and the web build preloads them at `/shaders`, so load them relative to `SDL_GetBasePath()` (which is `/` in the browser):

```cpp
std::string path = std::string(SDL_GetBasePath()) + "shaders/water";
ShaderHandle water = renderer->loadShader(path.c_str());
```

The shader body is the same `fn fragment` function shown above; the prelude is added during translation. `examples/render_showcase` uses this for its sprite shader.

Compile errors are logged with the backend name and return an empty handle.

## Materials

A material pairs a shader with its parameters and extra textures. Sprites and draws reference a material; many sprites can share one.

```cpp
MaterialHandle hit = renderer->createMaterial(flash);
renderer->setMaterialParam(hit, 0, {0.8f, 0.f, 0.f, 0.f});
renderer->setMaterialTexture(hit, 1, noiseTexture);   // slots 1..3

cmd.spawn().insert(Transform2D{}, Sprite{.texture = enemyTex, .material = hit});
```

Draws with the same material and texture still batch together.

## Post Effects

Post effects run on the whole scene after every camera has rendered, before the HUD layer. For bloom, vignette, CRT, color grading, blur and other common effects, use the [Post Effect Library](post-effects.md); this section is for writing your own.

The simplest post effect is a material; the scene arrives as `texture0`.

```cpp
int32_t id = renderer->addPostEffect(0, crtMaterial);   // order 0
renderer->removePostEffect(id);
```

Effects run in ascending order, each reading the previous result. For multi-pass effects such as bloom, pass a function instead. It receives the source texture and must draw the final result into `out.target`:

```cpp
renderer->addPostEffect(10, [=](RendererResource& r, TextureHandle src, const ViewInfo& out) {
    r.beginPass(brightTarget, {.clear = true});
    r.drawFullscreen(src, BlendMode::Opaque, brightPassMaterial);
    r.endPass();

    r.beginPass(out.target);
    r.drawFullscreen(src, BlendMode::Opaque);
    r.drawFullscreen(brightTarget, BlendMode::Additive);
    r.endPass();
});
```

## View Hooks

A view hook runs after each camera has drawn its layers and receives that camera's `ViewInfo`: its world-to-clip matrix, visible world bounds, target texture and viewport. Use it for effects that live in world space and must line up with one camera, such as light maps or fog of war.

```cpp
renderer->addViewHook(0, [=](RendererResource& r, const ViewInfo& view) {
    // Draw lights in world space into a light map.
    r.beginPass(lightMap, {.clear = true, .clearColor = {0.2f, 0.2f, 0.3f, 1.f},
                           .viewProj = &view.viewProj});
    for (auto& l : lights) r.draw({.texture = glow, .position = l.pos, .blend = BlendMode::Additive});
    r.endPass();

    // Multiply it over the camera's view.
    r.beginPass(view.target, {.viewportPx = view.viewportPx});
    r.drawFullscreen(lightMap, BlendMode::Multiply);
    r.endPass();
});
```

Inside a pass, draws go straight to the pass target in the order you issue them; they are not sorted.
