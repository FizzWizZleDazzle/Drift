#include <drift/RenderSnapshot.hpp>
#include <drift/World.hpp>
#include <drift/components/GlobalTransform.hpp>

#include <algorithm>
#include <string>

namespace drift {

namespace {

// Entities with a GlobalTransform2D (children in a hierarchy) render at
// their composed world transform; others use Transform2D directly.
Transform2D worldTransform(const Transform2D* local, const GlobalTransform2D* global, int32_t i) {
    if (global) return {global[i].position, global[i].rotation, global[i].scale};
    return local[i];
}

} // namespace

void RenderSnapshot::extract(World& world, const ComponentRegistry& registry,
                             const CameraPlacer& placeCameras, const SpriteFilter& keep) {
    ComponentId transformId = registry.get<Transform2D>();
    ComponentId spriteId    = registry.get<Sprite>();
    ComponentId cameraId    = registry.get<Camera>();
    ComponentId globalId    = registry.getByName("GlobalTransform2D");

    std::string optionalGlobal = globalId ? ", ?#" + std::to_string(globalId) : "";
    std::string spriteExpr = "#" + std::to_string(transformId) + ", #" + std::to_string(spriteId) + optionalGlobal;
    std::string cameraExpr = "#" + std::to_string(transformId) + ", #" + std::to_string(cameraId) + optionalGlobal;

    // Cameras first, so they can be placed before sprites are chosen.
    auto& cams = writeCameraBuffer();
    cams.clear();

    QueryIter camIter = world.queryIter(cameraExpr.c_str());
    while (world.queryNext(&camIter)) {
        auto* transforms = static_cast<Transform2D*>(
            world.queryField(&camIter, 0, sizeof(Transform2D)));
        auto* cameras = static_cast<Camera*>(
            world.queryField(&camIter, 1, sizeof(Camera)));
        auto* globals = globalId ? static_cast<GlobalTransform2D*>(
            world.queryFieldOptional(&camIter, 2, sizeof(GlobalTransform2D))) : nullptr;

        for (int32_t i = 0; i < camIter.count; ++i) {
            if (!cameras[i].active) continue;
            cams.push_back({worldTransform(transforms, globals, i).position, cameras[i]});
        }
    }
    world.queryFini(&camIter);

    std::stable_sort(cams.begin(), cams.end(), [](const CameraSnapshot& a, const CameraSnapshot& b) {
        return a.camera.order < b.camera.order;
    });
    if (placeCameras) placeCameras(cams);

    auto& buf = writeSpriteBuffer();
    buf.clear();

    QueryIter spriteIter = world.queryIter(spriteExpr.c_str());
    while (world.queryNext(&spriteIter)) {
        auto* transforms = static_cast<Transform2D*>(
            world.queryField(&spriteIter, 0, sizeof(Transform2D)));
        auto* sprites = static_cast<Sprite*>(
            world.queryField(&spriteIter, 1, sizeof(Sprite)));
        auto* globals = globalId ? static_cast<GlobalTransform2D*>(
            world.queryFieldOptional(&spriteIter, 2, sizeof(GlobalTransform2D))) : nullptr;

        for (int32_t i = 0; i < spriteIter.count; ++i) {
            if (!sprites[i].visible) continue;
            Transform2D t = worldTransform(transforms, globals, i);
            if (keep && !keep(t, sprites[i])) continue;
            buf.push_back({t, sprites[i]});
        }
    }
    world.queryFini(&spriteIter);
}

const std::vector<SpriteEntry>& RenderSnapshot::sprites() const {
    return readSpriteBuffer();
}

const std::vector<CameraSnapshot>& RenderSnapshot::cameras() const {
    return readCameraBuffer();
}

void RenderSnapshot::swap() {
    writeIdx_ = 1 - writeIdx_;
}

} // namespace drift
