# Spatial Queries, Pathfinding and Y-Sort

Three tools for games with many things on a map: a spatial hash to find what is near a point, grid A* to find a way around obstacles, and Y-sort to draw top-down scenes in the right order.

## Spatial hash

**Headers:** `#include <drift/gameplay/SpatialHash.hpp>`, `#include <drift/plugins/SpatialPlugin.hpp>`

**Plugin:** `SpatialPlugin` (included in `DefaultPlugins`)

Give an entity `SpatialBounds` and it appears in the `SpatialIndex` resource, which is rebuilt at the start of every frame from the positions the previous frame ended with.

```cpp
cmd.spawn().insert(Transform2D{pos}, Sprite{...}, SpatialBounds{.halfSize = {8.f, 8.f}});

void aggro(Res<SpatialIndex> index, Query<Transform2D, With<Player>> players) {
    players.iter([&](const Transform2D& p) {
        for (EntityId e : index->queryRadius(p.position, 150.f)) {
            // e is within 150 px of the player
        }
    });
}
```

| Query | Returns |
|-------|---------|
| `queryRect(rect)` | Entities whose bounds overlap the rect |
| `queryRadius(center, radius)` | Entities whose bounds come within `radius` of `center` |
| `nearest(point, maxDistance, ignore)` | The closest entity within `maxDistance`, or `InvalidEntityId` |

Each entity appears at most once per query. Queries are const and safe from systems running in parallel. Bounds are a box of `halfSize` around `Transform2D::position + offset`; a zero `halfSize` indexes the point. Pick a cell size near the typical query radius (64 by default) with `index->setCellSize(128.f)` in a startup system.

`SpatialHash` also works on its own for anything that is not an entity (bullets in a pool, particles): `clear()`, `insert(id, rect)` each frame, then query.

## Grid pathfinding

**Header:** `#include <drift/gameplay/Pathfinding.hpp>`

`NavGrid` holds the cost of entering each cell; `findPath` runs A* over it.

```cpp
NavGrid grid(40, 30, /*cellSize=*/32.f);
grid.setBlocked({5, 3});
grid.setCost({6, 3}, 4.f);                  // mud: four times as expensive

std::vector<GridPoint> path = findPath(grid, grid.worldToCell(from), grid.worldToCell(to));
for (GridPoint c : path) {
    Vec2 waypoint = grid.cellToWorld(c);    // cell center
}
```

- The path includes the start and goal cells. It is empty when there is no path or when start or goal is blocked or outside the grid.
- A cost of 1 is normal ground; a cost of 0 or less blocks the cell. Diagonal steps cost `sqrt(2)` times the cell cost.
- `pathCost(grid, path)` sums the cost of a path.
- `cellSize` and `origin` place the grid in the world for `worldToCell` and `cellToWorld`.

`PathOptions` controls the search:

| Field | Default | Meaning |
|-------|---------|---------|
| `diagonal` | `NoCornerCutting` | `Never` (4 directions), `Always`, `NoCornerCutting` (both side cells must be open), `AllowOneCorner` (one side cell must be open) |
| `heuristicWeight` | 1 | 1 finds the cheapest path; higher values search fewer cells for a slightly longer path |
| `maxExpanded` | 0 | Give up after expanding this many cells (0 = no limit) |

```cpp
auto path = findPath(grid, a, b, {.diagonal = DiagonalMode::Never});
```

To walk a path, tween through the waypoints (see the [toolkit example](../examples/toolkit.md)) or steer toward the next cell each frame with `moveTowards`.

## Y-sort

**Header:** `#include <drift/plugins/YSortPlugin.hpp>`

**Plugin:** `YSortPlugin` (included in `DefaultPlugins`)

In top-down games, things lower on the screen should draw in front. Add `YSort` to a sprite entity and its `Sprite::zOrder` is set every frame from its y position:

```
zOrder = baseZ + (position.y + pivotY) * scale
```

```cpp
cmd.spawn().insert(Transform2D{pos},
                   Sprite{.texture = tree, .origin = {16.f, 60.f}},   // origin at the trunk base
                   YSort{.baseZ = 1.f});
```

Set the sprite's `origin` (or `pivotY`) at the feet, so a character walking behind a tall tree is hidden by it. The default `scale` maps 1000 pixels of height to 1.0 of `zOrder`; keep the result between the `zOrder` bands you use for ground and overlays, or put Y-sorted sprites on their own render layer.
