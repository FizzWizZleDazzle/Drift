#pragma once

#include <drift/Types.hpp>
#include <drift/Handle.hpp>

namespace drift {

// Puts a loaded map in the world. The entity's Transform2D position is the
// map's top-left corner (plus the LDtk level's world offset); its scale
// scales the map. Rotation is not applied.
//
// Layers are drawn bottom to top at zOrder, zOrder + zStep, ... on
// renderLayer, so a sprite with a zOrder between two layers sorts between
// them. Per-layer overrides: TilemapResource::setLayerZOrder and
// setLayerRenderLayer, or the Tiled layer properties "zOrder" and
// "renderLayer".
struct Tilemap {
    TilemapHandle map;
    float         zOrder = 0.f;
    float         zStep = 1.f;
    uint8_t       renderLayer = 0;
    bool          visible = true;
    bool          cull = true;            // skip chunks outside the camera
    CameraHandle  camera;                 // culling and parallax camera; empty = default camera
    bool          colliders = true;       // static colliders from the collision layers
    bool          spawnObjects = true;    // run the object spawners once
    ShapeDef      collider = {};          // friction, restitution, filter of the colliders
};

} // namespace drift
