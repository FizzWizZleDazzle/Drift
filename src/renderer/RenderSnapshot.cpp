#include <drift/RenderSnapshot.hpp>
#include <drift/World.hpp>

#include <string>

namespace drift {

void RenderSnapshot::extract(World& world, const ComponentRegistry& registry) {
    // Build query expressions from registry instead of hardcoded strings
    ComponentId transformId = registry.get<Transform2D>();
    ComponentId spriteId    = registry.get<Sprite>();
    ComponentId cameraId    = registry.get<Camera>();

    std::string spriteExpr = "#" + std::to_string(transformId) + ", #" + std::to_string(spriteId);
    std::string cameraExpr = "#" + std::to_string(transformId) + ", #" + std::to_string(cameraId);

    // Extract sprites
    auto& buf = writeSpriteBuffer();
    buf.clear();

    QueryIter spriteIter = world.queryIter(spriteExpr.c_str());
    while (world.queryNext(&spriteIter)) {
        auto* transforms = static_cast<Transform2D*>(
            world.queryField(&spriteIter, 0, sizeof(Transform2D)));
        auto* sprites = static_cast<Sprite*>(
            world.queryField(&spriteIter, 1, sizeof(Sprite)));

        for (int32_t i = 0; i < spriteIter.count; ++i) {
            buf.push_back({transforms[i], sprites[i]});
        }
    }
    world.queryFini(&spriteIter);

    // Extract active camera
    auto& cam = writeCameraBuffer();
    cam = {};

    QueryIter camIter = world.queryIter(cameraExpr.c_str());
    while (world.queryNext(&camIter)) {
        auto* transforms = static_cast<Transform2D*>(
            world.queryField(&camIter, 0, sizeof(Transform2D)));
        auto* cameras = static_cast<Camera*>(
            world.queryField(&camIter, 1, sizeof(Camera)));

        for (int32_t i = 0; i < camIter.count; ++i) {
            if (cameras[i].active) {
                cam.position = transforms[i].position;
                cam.zoom = cameras[i].zoom;
                cam.rotation = cameras[i].rotation;
                cam.found = true;
                break;
            }
        }
        if (cam.found) break;
    }
    world.queryFini(&camIter);
}

const std::vector<SpriteEntry>& RenderSnapshot::sprites() const {
    return readSpriteBuffer();
}

const CameraSnapshot& RenderSnapshot::camera() const {
    return readCameraBuffer();
}

void RenderSnapshot::swap() {
    writeIdx_ = 1 - writeIdx_;
}

} // namespace drift
