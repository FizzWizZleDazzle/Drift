#pragma once

// Steering behaviors (Craig Reynolds): small functions that each return a
// steering force, a change of velocity per second, toward one goal. Add the
// forces you want, weighted, cap the sum, and integrate:
//
//   Vec2 force = steer::arrive(pos, vel, target, maxSpeed, 64.f) * 1.f
//              + steer::separation(pos, neighbours, 24.f, maxSpeed) * 1.5f;
//   vel = steer::truncate(vel + steer::truncate(force, maxForce) * dt, maxSpeed);
//
// Everything is plain math on Vec2; feed it positions from your own query,
// a SpatialIndex lookup for neighbours, and write the result to Velocity2D.

#include <drift/Types.hpp>
#include <drift/math/Random.hpp>
#include <drift/math/Vector.hpp>

#include <cmath>
#include <span>

namespace drift::steer {

// `v` shortened to at most `maxLength`.
inline Vec2 truncate(Vec2 v, float maxLength) {
    float len2 = v.lengthSq();
    if (len2 <= maxLength * maxLength || len2 <= 0.f) return v;
    return v * (maxLength / std::sqrt(len2));
}

// Full speed toward the target.
inline Vec2 seek(Vec2 position, Vec2 velocity, Vec2 target, float maxSpeed) {
    return (target - position).normalized() * maxSpeed - velocity;
}

// Full speed away from the threat, while it is within `panicRadius`
// (0 = always).
inline Vec2 flee(Vec2 position, Vec2 velocity, Vec2 threat, float maxSpeed, float panicRadius = 0.f) {
    Vec2 away = position - threat;
    if (panicRadius > 0.f && away.lengthSq() > panicRadius * panicRadius) return {};
    return away.normalized() * maxSpeed - velocity;
}

// Toward the target, slowing down inside `slowRadius` to stop on it.
inline Vec2 arrive(Vec2 position, Vec2 velocity, Vec2 target, float maxSpeed, float slowRadius) {
    Vec2 to = target - position;
    float dist = to.length();
    if (dist < 1e-4f) return -velocity;
    float speed = slowRadius > 0.f && dist < slowRadius ? maxSpeed * dist / slowRadius : maxSpeed;
    return to * (speed / dist) - velocity;
}

// Seek where a moving target will be, judging by its velocity.
inline Vec2 pursue(Vec2 position, Vec2 velocity, Vec2 target, Vec2 targetVelocity, float maxSpeed) {
    float dist = (target - position).length();
    float t = maxSpeed > 0.f ? dist / maxSpeed : 0.f;
    return seek(position, velocity, target + targetVelocity * t, maxSpeed);
}

// Flee from where a moving threat will be.
inline Vec2 evade(Vec2 position, Vec2 velocity, Vec2 threat, Vec2 threatVelocity, float maxSpeed,
                  float panicRadius = 0.f) {
    float dist = (threat - position).length();
    float t = maxSpeed > 0.f ? dist / maxSpeed : 0.f;
    return flee(position, velocity, threat + threatVelocity * t, maxSpeed, panicRadius);
}

// Aimless but smooth wandering. `angle` is the agent's wander state:
// keep it per agent between calls. Each call nudges it by up to `jitter`
// radians and steers toward a point on a circle `distance` ahead.
inline Vec2 wander(Vec2 velocity, float& angle, Rng& rng, float maxSpeed, float distance = 40.f,
                   float radius = 20.f, float jitter = 0.5f) {
    angle += rng.range(-jitter, jitter);
    Vec2 heading = velocity.lengthSq() > 1e-8f ? velocity.normalized() : Vec2{1.f, 0.f};
    Vec2 target = heading * distance + fromAngle(angle, radius);
    return target.normalized() * maxSpeed - velocity;
}

// Away from neighbours closer than `radius`, stronger the closer they are.
// The agent's own position in `neighbours` is skipped.
inline Vec2 separation(Vec2 position, std::span<const Vec2> neighbours, float radius, float maxSpeed) {
    Vec2 push{};
    int count = 0;
    for (Vec2 n : neighbours) {
        Vec2 away = position - n;
        float d2 = away.lengthSq();
        if (d2 <= 1e-8f || d2 > radius * radius) continue;
        push += away * (1.f / d2);   // 1/d falloff on the unit direction
        ++count;
    }
    if (count == 0) return {};
    return push.normalized() * maxSpeed;
}

// Toward the average position of the neighbours within `radius`.
inline Vec2 cohesion(Vec2 position, Vec2 velocity, std::span<const Vec2> neighbours, float radius, float maxSpeed) {
    Vec2 sum{};
    int count = 0;
    for (Vec2 n : neighbours) {
        Vec2 d = n - position;
        float d2 = d.lengthSq();
        if (d2 <= 1e-8f || d2 > radius * radius) continue;
        sum += n;
        ++count;
    }
    if (count == 0) return {};
    return seek(position, velocity, sum * (1.f / float(count)), maxSpeed);
}

// Toward the average heading of the neighbours (velocities paired with
// positions) within `radius`.
inline Vec2 alignment(Vec2 position, Vec2 velocity, std::span<const Vec2> neighbourPositions,
                      std::span<const Vec2> neighbourVelocities, float radius, float maxSpeed) {
    Vec2 sum{};
    int count = 0;
    for (size_t i = 0; i < neighbourPositions.size() && i < neighbourVelocities.size(); ++i) {
        Vec2 d = neighbourPositions[i] - position;
        float d2 = d.lengthSq();
        if (d2 <= 1e-8f || d2 > radius * radius) continue;
        sum += neighbourVelocities[i];
        ++count;
    }
    if (count == 0 || sum.lengthSq() <= 1e-8f) return {};
    return sum.normalized() * maxSpeed - velocity;
}

// Along a path of points: arrive at the last one, seek the others, moving
// on to the next once within `reachRadius`. `index` is the agent's path
// state; keep it per agent, starting at 0.
inline Vec2 followPath(Vec2 position, Vec2 velocity, std::span<const Vec2> path, size_t& index, float maxSpeed,
                       float reachRadius, float slowRadius = 0.f) {
    if (path.empty()) return -velocity;
    if (index >= path.size()) index = path.size() - 1;
    while (index + 1 < path.size() && (path[index] - position).lengthSq() <= reachRadius * reachRadius) ++index;
    if (index + 1 == path.size()) return arrive(position, velocity, path[index], maxSpeed, slowRadius);
    return seek(position, velocity, path[index], maxSpeed);
}

} // namespace drift::steer
