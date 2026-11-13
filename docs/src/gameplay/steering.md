# Steering

Steering behaviors move agents in a lifelike way: chase, flee, arrive and slow down, wander, follow a path, and keep apart in a crowd. Each behavior is a small function that returns a steering force, a change of velocity per second toward one goal. Weight and add the ones an agent needs, cap the total, and integrate.

**Header:** `#include <drift/gameplay/Steering.hpp>`

```cpp
void chase(Res<Time> time, Query<Transform2D, With<Player>> players,
           QueryMut<Transform2D, Velocity2D, Zombie> zombies, Res<SpatialIndex> index) {
    Vec2 target{};
    players.iter([&](const Transform2D& p) { target = p.position; });
    float dt = time->delta;
    zombies.iter([&](Transform2D& t, Velocity2D& v, Zombie& z) {
        std::vector<Vec2> near;   // positions of the entities index->queryRadius(t.position, 32.f) finds
        Vec2 force = steer::arrive(t.position, v.linear, target, z.speed, 48.f)
                   + steer::separation(t.position, near, 24.f, z.speed) * 1.5f;
        v.linear = steer::truncate(v.linear + steer::truncate(force, z.maxForce) * dt, z.speed);
    });
}
```

| Function | Steers |
|----------|--------|
| `seek(pos, vel, target, maxSpeed)` | Toward a point at full speed |
| `flee(pos, vel, threat, maxSpeed, panicRadius)` | Away from a point, while it is within `panicRadius` (0 = always) |
| `arrive(pos, vel, target, maxSpeed, slowRadius)` | Toward a point, slowing down inside `slowRadius` to stop on it |
| `pursue(pos, vel, target, targetVel, maxSpeed)` | Toward where a moving target will be |
| `evade(pos, vel, threat, threatVel, maxSpeed, panicRadius)` | Away from where a moving threat will be |
| `wander(vel, angle, rng, maxSpeed, distance, radius, jitter)` | Aimlessly but smoothly. `angle` is per-agent state you keep between calls |
| `separation(pos, neighbours, radius, maxSpeed)` | Away from neighbours closer than `radius` |
| `cohesion(pos, vel, neighbours, radius, maxSpeed)` | Toward the middle of the neighbours within `radius` |
| `alignment(pos, vel, neighbourPositions, neighbourVelocities, radius, maxSpeed)` | Toward the neighbours' average heading |
| `followPath(pos, vel, path, index, maxSpeed, reachRadius, slowRadius)` | Along waypoints, arriving at the last. `index` is per-agent state, starting at 0 |
| `truncate(v, maxLength)` | `v` shortened to at most `maxLength` |

The functions are plain math on `Vec2` and know nothing about entities; feed them positions from your queries and neighbours from a [`SpatialIndex`](spatial-and-pathfinding.md#spatial-hash) lookup, and write the result to `Velocity2D` (a physics body follows it) or to the position yourself. A neighbour list may include the agent's own position; the crowd behaviors skip it.

Separation, cohesion and alignment together are the classic flocking rules. For agents that must find their way around walls, steer toward the next cell of a [path or flow field](spatial-and-pathfinding.md#flow-fields) and add separation so they do not stack.
