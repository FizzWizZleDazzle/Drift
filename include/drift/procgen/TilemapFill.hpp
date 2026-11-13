#pragma once

// Grids into tilemap layers and back.
//
//   TilemapHandle map = maps->createTilemap({.width = 80, .height = 50, .tileset = tiles});
//   const uint32_t tiles[] = {1, 2};          // GridFloor -> tile 1, GridWall -> tile 2
//   fillTilemapLayer(*maps, map, 0, cave, tiles);
//   maps->setLayerCollision(map, 0, true, {2});

#include <drift/procgen/Grid.hpp>
#include <drift/resources/TilemapResource.hpp>

#include <cstdint>
#include <span>

namespace drift {

// Writes `grid` into a tile layer with its top-left cell at `at`: a cell
// of value v becomes tile tileForValue[v] (0 leaves the cell empty).
// Values outside the table become empty cells too.
void fillTilemapLayer(TilemapResource& maps, TilemapHandle map, int32_t layer, const Grid& grid,
                      std::span<const uint32_t> tileForValue, TileCoord at = {});

// Writes `grid` into an LDtk IntGrid layer, value for value.
void fillIntGridLayer(TilemapResource& maps, TilemapHandle map, int32_t layer, const Grid& grid,
                      TileCoord at = {});

// The tile ids of a layer (no flip bits) over the map's width x height.
Grid gridFromTilemapLayer(const TilemapResource& maps, TilemapHandle map, int32_t layer);

} // namespace drift
