#pragma once

#include <drift/Resource.hpp>
#include <drift/Types.hpp>
#include <drift/ComponentRegistry.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>

#include <vector>

namespace drift {

class World;

struct SpriteEntry {
    Transform2D transform;
    Sprite sprite;
};

struct CameraSnapshot {
    Vec2 position = {};
    float zoom = 1.f;
    float rotation = 0.f;
    bool found = false;
};

class RenderSnapshot : public Resource {
public:
    DRIFT_RESOURCE(RenderSnapshot)

    // Called during Extract phase: queries World for Sprite+Transform2D
    // and the active Camera+Transform2D
    void extract(World& world, const ComponentRegistry& registry);

    // Read access for the renderer (returns the read buffer)
    const std::vector<SpriteEntry>& sprites() const;
    const CameraSnapshot& camera() const;

    // Swap buffers (called after extract, before render)
    void swap();

private:
    std::vector<SpriteEntry> spriteBuffers_[2];
    CameraSnapshot cameraBuffers_[2];
    int writeIdx_ = 0;

    std::vector<SpriteEntry>& writeSpriteBuffer() { return spriteBuffers_[writeIdx_]; }
    const std::vector<SpriteEntry>& readSpriteBuffer() const { return spriteBuffers_[1 - writeIdx_]; }
    CameraSnapshot& writeCameraBuffer() { return cameraBuffers_[writeIdx_]; }
    const CameraSnapshot& readCameraBuffer() const { return cameraBuffers_[1 - writeIdx_]; }
};

} // namespace drift
