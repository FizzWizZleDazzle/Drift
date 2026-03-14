#pragma once

#include <drift/Resource.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>

#include <functional>

namespace drift {

class App;

class RendererResource : public Resource {
public:
    explicit RendererResource(App& app);
    ~RendererResource() override;

    DRIFT_RESOURCE(RendererResource)

    // Frame lifecycle
    void beginFrame();
    void endFrame();
    void present();

    // Clear color
    void setClearColor(ColorF color);

    // Textures
    TextureHandle loadTexture(const char* path);
    TextureHandle createTextureFromPixels(const uint8_t* pixels, int w, int h);
    void destroyTexture(TextureHandle texture);
    void getTextureSize(TextureHandle texture, int32_t* w, int32_t* h) const;

    // Sprite drawing
    void drawSprite(TextureHandle texture, Vec2 position, float zOrder);
    void drawSprite(TextureHandle texture, Vec2 position, Rect srcRect, float zOrder);
    void drawSprite(TextureHandle texture, Vec2 position, Rect srcRect = {},
                    Vec2 scale = Vec2::one(), float rotation = 0.f,
                    Vec2 origin = {}, Color tint = Color::white(),
                    Flip flip = Flip::None, float zOrder = 0.f,
                    bool additive = false);

    // Bulk sprite push for particle systems (avoids per-particle function call overhead)
    void drawSpriteBatch(TextureHandle texture, Rect srcRect,
                         const Vec2* positions, const float* sizes,
                         const float* rotations, const Color* tints,
                         int32_t count, float zOrder, bool additive = false);

    // Primitive drawing
    void drawRect(Rect rect, Color color);
    void drawRectFilled(Rect rect, Color color);
    void drawLine(Vec2 start, Vec2 end, Color color, float thickness = 1.f);
    void drawCircle(Vec2 center, float radius, Color color, int segments = 32);

    // Camera
    CameraHandle getDefaultCamera() const;
    CameraHandle createCamera();
    void destroyCamera(CameraHandle camera);
    void setActiveCamera(CameraHandle camera);
    void setCameraPosition(CameraHandle camera, Vec2 position);
    void setCameraZoom(CameraHandle camera, float zoom);
    void setCameraRotation(CameraHandle camera, float rotation);
    Vec2 getCameraPosition(CameraHandle camera) const;
    float getCameraZoom(CameraHandle camera) const;
    Vec2 screenToWorld(CameraHandle camera, Vec2 screenPos) const;
    Vec2 worldToScreen(CameraHandle camera, Vec2 worldPos) const;

    // Stats
    int32_t drawCalls() const;
    int32_t spriteCount() const;

    // Internal access for other subsystems
    void* getGPUDevice() const;
    void* getWindow() const;

    // Overlay callbacks (used by UIResource for ImGui rendering)
    using PrePassFn = std::function<void(void* cmdBuf)>;
    using InPassFn  = std::function<void(void* cmdBuf, void* renderPass)>;
    void setPrePassCallback(PrePassFn fn);
    void setInPassCallback(InPassFn fn);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
