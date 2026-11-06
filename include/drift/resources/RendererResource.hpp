#pragma once

#include <drift/Resource.hpp>
#include <drift/Handle.hpp>
#include <drift/Math.hpp>
#include <drift/Types.hpp>

#include <functional>
#include <string>

namespace drift {

class App;
namespace gfx { class Device; }

// One textured, transformed quad. Every field except texture has a default.
struct SpriteDraw {
    TextureHandle  texture;
    Vec2           position;
    Rect           srcRect = {};            // {} = whole texture
    Vec2           scale = Vec2::one();
    float          rotation = 0.f;          // radians
    Vec2           origin = {};             // pivot in source pixels
    Color          tint = Color::white();
    Flip           flip = Flip::None;
    float          zOrder = 0.f;
    BlendMode      blend = BlendMode::Alpha;
    MaterialHandle material;                // custom shader; empty = default sprite shader
    uint8_t        layer = 0;               // 0..30 world layers, ScreenLayer for HUD
};

// Shader sources. Provide WGSL (portable: translated to GLSL at build time by
// drift_add_shaders) and/or GLSL (OpenGL backend only). See the shaders docs
// for the prelude every shader gets.
struct ShaderDesc {
    std::string wgsl;           // body with `fn fragment(in: FragmentInput) -> vec4<f32>`
    std::string glslFragment;   // raw GLSL main() (prelude added) or a full translated module
    std::string glslVertex;     // optional; only for full translated modules
};

// A camera view. Cameras render world layers in layerMask into a viewport
// of the scene (or of `target` when set) and are drawn in ascending order.
struct CameraDesc {
    Vec2          position = {};
    float         zoom = 1.f;
    float         rotation = 0.f;
    Rect          viewport = {0.f, 0.f, 1.f, 1.f};  // normalized, origin top-left
    int32_t       order = 0;
    uint32_t      layerMask = AllWorldLayers;
    TextureHandle target;                          // render target; empty = the scene
    bool          clear = false;                   // clear the viewport first
    ColorF        clearColor = {0.f, 0.f, 0.f, 1.f};
    bool          enabled = true;
};

// What a view hook or post effect is rendering into.
struct ViewInfo {
    CameraHandle  camera;
    Mat3          viewProj;          // world -> clip for this view
    Rect          worldBounds;       // visible world-space AABB
    TextureHandle target;            // texture the view renders into
    Rect          viewportPx;        // viewport inside target, in pixels
    Vec2          targetSize;        // target size in pixels
};

// Options for an explicit pass inside a view hook or post effect.
struct PassOptions {
    bool        clear = false;
    ColorF      clearColor = {0.f, 0.f, 0.f, 0.f};
    const Mat3* viewProj = nullptr;  // nullptr = target pixel space (origin top-left)
    Rect        viewportPx = {};     // {} = whole target
};

class RendererResource : public Resource {
public:
    explicit RendererResource(App& app);
    ~RendererResource() override;

    DRIFT_RESOURCE(RendererResource)
    bool mainThreadOnly() const override { return true; }

    // Frame lifecycle (driven by RendererPlugin)
    void beginFrame();
    void endFrame();

    void setClearColor(ColorF color);

    // ---- Textures ----
    TextureHandle loadTexture(const char* path, TextureOptions options = {});
    // Re-read an image file into an existing texture; the handle stays valid
    // and the size may change. Used by hot reload.
    bool reloadTexture(TextureHandle texture, const char* path);
    TextureHandle createTextureFromPixels(const uint8_t* rgba, int w, int h, TextureOptions options = {});
    void updateTexture(TextureHandle texture, int x, int y, int w, int h, const uint8_t* rgba);
    void setTextureOptions(TextureHandle texture, TextureOptions options);
    void destroyTexture(TextureHandle texture);
    void getTextureSize(TextureHandle texture, int32_t* w, int32_t* h) const;
    Vec2 textureSize(TextureHandle texture) const;
    TextureHandle whiteTexture() const;

    // Render targets are textures you can draw into (camera target, view
    // hooks) and then draw like any other texture.
    TextureHandle createRenderTarget(int w, int h, TextureOptions options = {});

    // ---- Shaders and materials ----
    // loadShader("shaders/water") finds water.wgsl, water.frag.glsl and
    // water.vert.glsl next to each other and uses what the active backend
    // understands.
    ShaderHandle loadShader(const char* path);
    ShaderHandle createShader(const ShaderDesc& desc);
    void destroyShader(ShaderHandle shader);

    MaterialHandle createMaterial(ShaderHandle shader);
    void setMaterialParam(MaterialHandle material, int index, Vec4 value);   // index 0..7
    void setMaterialTexture(MaterialHandle material, int slot, TextureHandle texture);  // slot 1..3
    void destroyMaterial(MaterialHandle material);

    // ---- Drawing (queued, sorted by layer and zOrder, batched at endFrame) ----
    void draw(const SpriteDraw& sprite);
    void drawSprite(TextureHandle texture, Vec2 position, float zOrder = 0.f);
    void drawSprite(TextureHandle texture, Vec2 position, Rect srcRect, float zOrder);
    void drawSprite(TextureHandle texture, Vec2 position, Rect srcRect,
                    Vec2 scale, float rotation = 0.f, Vec2 origin = {},
                    Color tint = Color::white(), Flip flip = Flip::None,
                    float zOrder = 0.f, BlendMode blend = BlendMode::Alpha);

    // Many same-texture quads centered on their positions (particles).
    void drawSpriteBatch(TextureHandle texture, Rect srcRect,
                         const Vec2* positions, const float* sizes,
                         const float* rotations, const Color* tints,
                         int32_t count, float zOrder, BlendMode blend = BlendMode::Alpha);

    // Nine-slice: corners keep their size, edges and center stretch.
    // borders = {left, top, right, bottom} in source pixels.
    void drawNineSlice(TextureHandle texture, Rect dest, Rect srcRect, Vec4 borders,
                       Color tint = Color::white(), float zOrder = 0.f);

    // Arbitrary textured triangles. Vertex positions are world space (or
    // logical screen space on ScreenLayer); uv in 0..1.
    struct MeshVertex { Vec2 position; Vec2 uv; Color color = Color::white(); };
    void drawMesh(TextureHandle texture, const MeshVertex* vertices, int32_t vertexCount,
                  const uint32_t* indices, int32_t indexCount, float zOrder = 0.f,
                  BlendMode blend = BlendMode::Alpha, MaterialHandle material = {});

    // Shapes. Thickness is in world units.
    void drawLine(Vec2 start, Vec2 end, Color color, float thickness = 1.f, float zOrder = 0.f);
    void drawPolyline(const Vec2* points, int32_t count, Color color, float thickness = 1.f,
                      bool closed = false, float zOrder = 0.f);
    void drawRect(Rect rect, Color color, float thickness = 1.f, float zOrder = 0.f);
    void drawRectFilled(Rect rect, Color color, float zOrder = 0.f);
    void drawRoundedRectFilled(Rect rect, float radius, Color color, float zOrder = 0.f);
    void drawCircle(Vec2 center, float radius, Color color, float thickness = 1.f,
                    int segments = 0, float zOrder = 0.f);
    void drawCircleFilled(Vec2 center, float radius, Color color, int segments = 0, float zOrder = 0.f);
    void drawArc(Vec2 center, float radius, float startAngle, float endAngle, Color color,
                 float thickness = 1.f, float zOrder = 0.f);
    void drawTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Color color, float zOrder = 0.f);
    // Convex or concave simple polygon (ear clipping).
    void drawPolygonFilled(const Vec2* points, int32_t count, Color color, float zOrder = 0.f);

    // Layer used by the immediate draw calls above (draw() uses its own field).
    void setLayer(uint8_t layer);
    uint8_t layer() const;

    // Clip subsequent immediate draws to a rect in logical screen pixels.
    void pushClipRect(Rect rect);
    void popClipRect();

    // ---- Cameras ----
    // The default camera always exists; ECS Camera components drive it and
    // any extra cameras through CameraPlugin.
    CameraHandle getDefaultCamera() const;
    CameraHandle createCamera(const CameraDesc& desc = {});
    void destroyCamera(CameraHandle camera);
    void setCamera(CameraHandle camera, const CameraDesc& desc);
    CameraDesc camera(CameraHandle camera) const;
    void setCameraPosition(CameraHandle camera, Vec2 position);
    void setCameraZoom(CameraHandle camera, float zoom);
    void setCameraRotation(CameraHandle camera, float rotation);
    Vec2 getCameraPosition(CameraHandle camera) const;
    float getCameraZoom(CameraHandle camera) const;
    // Logical screen position <-> world position through a camera.
    Vec2 screenToWorld(CameraHandle camera, Vec2 screenPos) const;
    Vec2 worldToScreen(CameraHandle camera, Vec2 worldPos) const;
    Vec2 screenToWorld(Vec2 screenPos) const;    // default camera
    Vec2 worldToScreen(Vec2 worldPos) const;
    Rect cameraWorldBounds(CameraHandle camera) const;

    // ---- Resolution and window mapping ----
    float logicalWidth() const;
    float logicalHeight() const;
    void setLogicalSize(int w, int h);
    ScaleMode scaleMode() const;
    void setScaleMode(ScaleMode mode);
    // Window pixel position -> logical screen position (accounts for scale mode).
    Vec2 windowToLogical(Vec2 windowPos) const;
    // Rect of the window (in window pixels) the scene is shown in.
    Rect outputRect() const;

    // ---- Extending the frame ----
    // View hooks run after each camera renders its layers. Use them to
    // composite effects that depend on the camera, such as lighting.
    using ViewHook = std::function<void(RendererResource&, const ViewInfo&)>;
    int32_t addViewHook(int32_t order, ViewHook hook);
    void removeViewHook(int32_t id);

    // Post effects run in order on the whole scene after all cameras, before
    // ScreenLayer. Each draws `source` into the current target.
    using PostEffect = std::function<void(RendererResource&, TextureHandle source, const ViewInfo& output)>;
    int32_t addPostEffect(int32_t order, PostEffect effect);
    void removePostEffect(int32_t id);
    // A post effect that runs one material shader over the scene; the
    // scene arrives in texture0.
    int32_t addPostEffect(int32_t order, MaterialHandle material);

    // Only valid inside a view hook or post effect: draws queued between
    // beginPass and endPass go straight to `target`.
    void beginPass(TextureHandle target, const PassOptions& options = {});
    void endPass();
    // Fullscreen quad over the current pass, sampling `source` as texture0.
    void drawFullscreen(TextureHandle source, BlendMode blend = BlendMode::Alpha,
                        MaterialHandle material = {}, Color tint = Color::white());

    // ---- Debugging ----
    // Saves the next presented frame as a PNG.
    void captureScreenshot(const char* path);
    // Counts from the last completed frame.
    int32_t drawCalls() const;
    int32_t spriteCount() const;
    const char* backendName() const;

    // ---- Overlay and backend access ----
    // Runs after the scene is presented to the window (ImGui uses this).
    void setOverlayCallback(std::function<void()> fn);
    gfx::Device* device() const;
    // Backend texture id ImGui can draw (ImTextureID).
    uint64_t imguiTextureId(TextureHandle texture) const;
    void* getWindow() const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
