#pragma once

#include <drift/Resource.hpp>
#include <drift/Types.hpp>
#include <drift/ComponentRegistry.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>

#include <functional>
#include <vector>

namespace drift {

class World;

struct SpriteEntry {
    Transform2D transform;
    Sprite sprite;
};

struct CameraSnapshot {
    Vec2 position = {};
    Camera camera;
};

class RenderSnapshot : public Resource {
public:
    DRIFT_RESOURCE(RenderSnapshot)

    // Called during Extract phase: copies the active Camera+Transform2D
    // entities, hands them to placeCameras, then copies the visible
    // Sprite+Transform2D entities that keep accepts (all when it is empty).
    using CameraPlacer = std::function<void(const std::vector<CameraSnapshot>&)>;
    using SpriteFilter = std::function<bool(const Transform2D&, const Sprite&)>;
    void extract(World& world, const ComponentRegistry& registry,
                 const CameraPlacer& placeCameras = {}, const SpriteFilter& keep = {});

    // Read access for the renderer (returns the read buffer)
    const std::vector<SpriteEntry>& sprites() const;
    const std::vector<CameraSnapshot>& cameras() const;

    // Swap buffers (called after extract, before render)
    void swap();

private:
    std::vector<SpriteEntry> spriteBuffers_[2];
    std::vector<CameraSnapshot> cameraBuffers_[2];
    int writeIdx_ = 0;

    std::vector<SpriteEntry>& writeSpriteBuffer() { return spriteBuffers_[writeIdx_]; }
    const std::vector<SpriteEntry>& readSpriteBuffer() const { return spriteBuffers_[1 - writeIdx_]; }
    std::vector<CameraSnapshot>& writeCameraBuffer() { return cameraBuffers_[writeIdx_]; }
    const std::vector<CameraSnapshot>& readCameraBuffer() const { return cameraBuffers_[1 - writeIdx_]; }
};

} // namespace drift
