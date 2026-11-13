# Procedural Animation

`#include <drift/gameplay/ProceduralAnimation.hpp>`

Solvers that pose limbs, chains and ropes from targets every frame instead of playing authored frames. They are plain functions over `Vec2`; feed them positions from your queries and write the results into `Transform2D` (or place a part in world space with `localTransformFor`, see [Hierarchy](../components/hierarchy.md)). Angles are radians from +x toward +y, which is clockwise on screen.

The small helpers they build on are in `drift/math/Vector.hpp`: `wrapAngle`, `angleDelta`, `lerpAngle`, `smoothDamp`, `damp`, `fromAngle`, `angle` (see [Vector Helpers](../math/utilities.md)).

## Turning and springs

| Function | Does |
|----------|------|
| `rotateTowards(current, target, maxStep)` | Turns toward `target` along the shorter arc by at most `maxStep`; never overshoots |
| `springStep(value, velocity, target, stiffness, damping, dt)` | A damped spring for a `float` or a `Vec2`; `value` and `velocity` are updated in place. Damping `2 * sqrt(stiffness)` is critical. Long frames are substepped, so stiff springs stay stable |
| `angleSpringStep(value, velocity, target, stiffness, damping, dt)` | The same for an angle, the short way round |

```cpp
float headAngle = 0.f, headSpeed = 0.f;
angleSpringStep(headAngle, headSpeed, angle(mouse - head), 120.f, 12.f, dt);
```

## Two-bone IK

`twoBoneIk(root, target, upperLength, lowerLength, bend)` returns a `TwoBonePose`: `joint`, `end`, `upperAngle`, `lowerAngle` (world angles of the two bones) and `reached`. `bend > 0` puts the joint clockwise of the root-to-target line on screen, `bend < 0` counter-clockwise. Out of reach, the limb points straight at the target.

## FABRIK chains

`solveFabrik(joints, lengths, target, options)` moves `joints[1..n]` so the last joint reaches for `target` while `joints[0]` stays put; segment `i` runs from `joints[i]` to `joints[i + 1]` with length `lengths[i]`. Returns true when the end got within `options.tolerance`.

`FabrikOptions`: `iterations` (10), `tolerance` (0.5), and optional joint limits `minAngle`/`maxAngle` (spans, one entry per segment) that keep segment `i` within that range of segment `i - 1`, and segment 0 of `baseAngle`.

```cpp
std::vector<Vec2> joints = {root, root + Vec2{20, 0}, root + Vec2{40, 0}, root + Vec2{60, 0}};
std::vector<float> lengths = {20, 20, 20}, lo(3, -0.5f), hi(3, 0.5f);
FabrikOptions o;
o.minAngle = lo;
o.maxAngle = hi;
solveFabrik(joints, lengths, target, o);
```

## Verlet ropes

`stepVerlet(points, previous, anchor, segmentLength, dt, options)` advances a chain of points one step: `points[0]` is held at `anchor`, the others move with their velocity (`points - previous`) and gravity, then distance constraints keep each link at `segmentLength`. Use it for tails, tentacles, hair, scarves and ropes.

`VerletOptions`: `gravity` ({0, 400}), `damping` (0.99, velocity kept per 1/60 s), `iterations` (8), `stiffness` (0 to 1: resistance to bending, for tails and hair that keep their shape), `ground` (points stay above this y), `pinEnd` and `endTarget` (hold the last point too).

## Stepping feet

`Foot` stays planted while the body moves and steps when it strays. Set `position`, `from` and `to` to the starting point, then call `update(rest, options, dt, canStep)` every frame with `rest`, where the foot would stand under the body now. A planted foot starts a step when `canStep` and it is more than `options.distance` from `rest`; it lands `lead * distance` past `rest` in the direction it moved, after `duration` seconds, lifting `height` along `up` mid-step. `update` returns true on the frame it lands; `planted()` says whether it is down. Pass the other foot's `planted()` as `canStep` to make two legs take turns.

`StepOptions`: `distance` (30), `duration` (0.15), `height` (12), `lead` (0.3), `up` ({0, -1}).

Candela games reach the same solvers through the `drift` module; see the Candela book's Procedural Animation guide.
