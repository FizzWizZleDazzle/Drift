# Vector Helpers and Intersections

Free functions for the vector math and overlap tests most 2D games need. They work on the [math types](types.md) and live in the `drift` namespace.

## Vector helpers

**Header:** `#include <drift/math/Vector.hpp>`

### Scalars

| Function | Result |
|----------|--------|
| `lerp(a, b, t)` | `a + (b - a) * t` |
| `inverseLerp(a, b, v)` | `t` such that `lerp(a, b, t) == v` |
| `remap(v, inLo, inHi, outLo, outHi)` | `v` mapped from one range to another |
| `clamp(v, lo, hi)`, `clamp01(v)` | Clamped value |
| `approx(a, b, epsilon)` | Equal within `epsilon` (default `1e-5`) |
| `sign(v)` | -1, 0 or 1 |
| `moveTowards(current, target, maxDelta)` | Steps toward `target`, never past it |
| `smoothDamp(current, target, velocity, smoothTime, dt)` | Critically damped spring toward `target`; `velocity` carries state between frames |
| `damp(current, target, rate, dt)` | Frame-rate independent exponential approach |
| `wrapAngle(a)`, `angleDelta(a, b)`, `lerpAngle(a, b, t)` | Angles in radians, along the shorter arc |
| `toRadians`, `toDegrees` | Unit conversion |

`Pi` and `Tau` are defined as `float` constants.

### Vec2

| Function | Result |
|----------|--------|
| `dot(a, b)`, `cross(a, b)` | Dot product, and the z of the 3D cross product |
| `perp(v)` | `v` rotated +90 degrees: `(-y, x)` |
| `rotate(v, radians)` | Rotated vector |
| `angle(v)` | Angle from +X, matching `Transform2D::rotation` |
| `angleBetween(a, b)` | Signed angle from `a` to `b` |
| `fromAngle(radians, length)` | Vector pointing at an angle |
| `distance(a, b)`, `distanceSq(a, b)` | Distance between points |
| `lerp(a, b, t)`, `clamp(v, lo, hi)`, `approx(a, b)` | Component-wise versions of the scalar functions |
| `clampLength(v, max)` | `v` shortened to at most `max` |
| `reflectVector(v, n)`, `project(v, onto)` | Reflection about a unit normal; projection onto a vector |
| `moveTowards(a, b, maxDistance)`, `smoothDamp(...)`, `damp(...)` | Vector versions |
| `hadamard(a, b)` | Component-wise product |

The header also adds unary `-v`, `v / s`, `v /= s`, `==` and `!=` for `Vec2`, and `lerp` for `Color` and `ColorF`.

Screen coordinates point Y down, so a positive `cross(a, b)` means `b` is clockwise from `a` on screen.

```cpp
// Follow the mouse smoothly, whatever the frame rate.
static Vec2 vel;
t.position = smoothDamp(t.position, mouseWorld, vel, 0.15f, time->delta);

// Face the direction of travel.
t.rotation = angle(velocity);
```

## Intersections

**Header:** `#include <drift/math/Intersect.hpp>`

Shapes: `Rect` (axis-aligned box), `Circle { Vec2 center; float radius; }`, `Segment { Vec2 a, b; }`, and rays given as an origin and a direction.

| Function | Result |
|----------|--------|
| `overlaps(x, y)` | Any pair of `Rect`, `Circle` and `Segment` |
| `contains(shape, point)` | Point inside a `Rect` or `Circle` |
| `intersection(rectA, rectB)` | The overlapping `Rect`, if any |
| `penetration(a, b)` | Smallest push that moves rect `a` out of rect `b` |
| `intersect(segA, segB)` | Crossing point of two segments, if any |
| `intersect(segment, rect / circle)` | First hit along the segment as a `RayHit` |
| `raycast(origin, dir, rect / circle / segment, maxT)` | First hit along a ray as a `RayHit` |
| `closestPoint(rect / segment, p)` | Nearest point on the shape |
| `distance(segment, p)` | Distance from a point to a segment |

`RayHit` has `t` (distance along the ray in units of `dir`'s length; 0..1 for segments), `point` and the surface `normal`. Results that may miss are `std::optional`.

```cpp
// Line of sight: does the segment from guard to player cross any wall?
Segment sight{guard, player};
bool seen = std::none_of(walls.begin(), walls.end(),
                         [&](const Rect& w) { return overlaps(sight, w); });

// Bullet against a target circle this frame.
if (auto hit = raycast(pos, vel * dt, Circle{target, 12.f}, 1.f)) {
    spawnSpark(hit->point, hit->normal);
}
```

These are pure geometry. For physical bodies, use the [physics](../components/physics.md) components and `PhysicsResource` raycasts.
