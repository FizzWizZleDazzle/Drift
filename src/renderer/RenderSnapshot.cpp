#include <drift/RenderSnapshot.h>
#include <drift/World.hpp>

namespace drift {

void RenderSnapshot::extract(World& world) {
    // Extract sprites
    auto& buf = writeSpriteBuffer();
    buf.clear();

    QueryIter spriteIter = world.queryIter("Transform2D, Sprite");
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

    QueryIter camIter = world.queryIter("Transform2D, Camera");
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
