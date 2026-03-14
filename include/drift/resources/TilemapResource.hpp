#pragma once

#include <drift/Resource.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>

namespace drift {

class RendererResource;

struct TilemapObject {
    const char* name;
    const char* type;
    Rect rect;
    int32_t id;
};

class TilemapResource : public Resource {
public:
    explicit TilemapResource(RendererResource& renderer);
    ~TilemapResource() override;

    DRIFT_RESOURCE(TilemapResource)

    TilemapHandle loadTilemap(const char* path);
    void destroyTilemap(TilemapHandle map);
    void drawTilemap(TilemapHandle map);
    int32_t getTile(TilemapHandle map, int32_t layer, int32_t x, int32_t y) const;
    void setTile(TilemapHandle map, int32_t layer, int32_t x, int32_t y, int32_t tileId);
    int32_t layerCount(TilemapHandle map) const;
    int32_t mapWidth(TilemapHandle map) const;
    int32_t mapHeight(TilemapHandle map) const;
    int32_t tileWidth(TilemapHandle map) const;
    int32_t tileHeight(TilemapHandle map) const;
    int32_t getCollisionRects(TilemapHandle map, Rect* outRects, int32_t maxRects) const;
    int32_t getObjects(TilemapHandle map, const char* layerName,
                       TilemapObject* outObjects, int32_t maxObjects) const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
