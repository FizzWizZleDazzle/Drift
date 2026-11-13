#include <drift/procgen/TilemapFill.hpp>

namespace drift {

void fillTilemapLayer(TilemapResource& maps, TilemapHandle map, int32_t layer, const Grid& grid,
                      std::span<const uint32_t> tileForValue, TileCoord at) {
    for (int32_t y = 0; y < grid.height; ++y)
        for (int32_t x = 0; x < grid.width; ++x) {
            int32_t v = grid.get(x, y);
            uint32_t tile = v >= 0 && static_cast<size_t>(v) < tileForValue.size() ? tileForValue[v] : 0u;
            maps.setTile(map, layer, at.x + x, at.y + y, tile);
        }
}

void fillIntGridLayer(TilemapResource& maps, TilemapHandle map, int32_t layer, const Grid& grid, TileCoord at) {
    for (int32_t y = 0; y < grid.height; ++y)
        for (int32_t x = 0; x < grid.width; ++x) maps.setIntGrid(map, layer, at.x + x, at.y + y, grid.get(x, y));
}

Grid gridFromTilemapLayer(const TilemapResource& maps, TilemapHandle map, int32_t layer) {
    Grid g(maps.mapWidth(map), maps.mapHeight(map));
    for (int32_t y = 0; y < g.height; ++y)
        for (int32_t x = 0; x < g.width; ++x) g.set(x, y, static_cast<int32_t>(maps.getTile(map, layer, x, y)));
    return g;
}

} // namespace drift
