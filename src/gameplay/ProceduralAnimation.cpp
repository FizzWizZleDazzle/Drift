#include <drift/gameplay/ProceduralAnimation.hpp>
#include <drift/math/Vector.hpp>

#include <algorithm>
#include <cmath>

namespace drift {

float rotateTowards(float current, float target, float maxStep) {
    float d = angleDelta(current, target);
    if (std::fabs(d) <= maxStep) return current + d;
    return current + (d > 0.f ? maxStep : -maxStep);
}

namespace {

// Long frames run as several short steps, so a stiff spring stays stable.
constexpr float SpringStep = 1.f / 120.f;

int substeps(float dt) {
    if (dt <= 0.f) return 0;
    return std::min(64, static_cast<int>(std::ceil(dt / SpringStep)));
}

} // namespace

void springStep(float& value, float& velocity, float target, float stiffness, float damping, float dt) {
    int n = substeps(dt);
    float h = n > 0 ? dt / n : 0.f;
    for (int i = 0; i < n; ++i) {
        velocity += (stiffness * (target - value) - damping * velocity) * h;
        value += velocity * h;
    }
}

void springStep(Vec2& value, Vec2& velocity, Vec2 target, float stiffness, float damping, float dt) {
    springStep(value.x, velocity.x, target.x, stiffness, damping, dt);
    springStep(value.y, velocity.y, target.y, stiffness, damping, dt);
}

void angleSpringStep(float& value, float& velocity, float target, float stiffness, float damping, float dt) {
    // Chase the nearest copy of the target angle.
    springStep(value, velocity, value + angleDelta(value, target), stiffness, damping, dt);
}

TwoBonePose twoBoneIk(Vec2 root, Vec2 target, float upperLength, float lowerLength, float bend) {
    TwoBonePose pose;
    Vec2 toTarget = target - root;
    float d = toTarget.length();
    Vec2 dir = d > 1e-6f ? toTarget / d : Vec2{1.f, 0.f};
    float maxReach = upperLength + lowerLength;
    float minReach = std::fabs(upperLength - lowerLength);
    pose.reached = d <= maxReach + 1e-3f && d >= minReach - 1e-3f;
    float dc = clamp(d, minReach + 1e-4f, std::max(minReach + 1e-4f, maxReach - 1e-4f));
    float base = angle(dir);
    float cosA = upperLength > 0.f ? (upperLength * upperLength + dc * dc - lowerLength * lowerLength) /
                                         (2.f * upperLength * dc)
                                   : 1.f;
    float a = std::acos(clamp(cosA, -1.f, 1.f));
    pose.upperAngle = base + (bend < 0.f ? -a : a);
    pose.joint = root + fromAngle(pose.upperAngle, upperLength);
    Vec2 end = root + dir * dc;
    pose.lowerAngle = angle(end - pose.joint);
    pose.end = pose.joint + fromAngle(pose.lowerAngle, lowerLength);
    return pose;
}

bool solveFabrik(std::span<Vec2> joints, std::span<const float> lengths, Vec2 target, const FabrikOptions& o) {
    size_t n = std::min(lengths.size(), joints.size() > 0 ? joints.size() - 1 : 0);
    if (n == 0) return false;
    bool limited = o.minAngle.size() >= n && o.maxAngle.size() >= n;
    Vec2 root = joints[0];
    float tol2 = o.tolerance * o.tolerance;
    for (int iter = 0; iter < std::max(1, o.iterations); ++iter) {
        // Backward: the end on the target, each joint pulled after it.
        joints[n] = target;
        for (size_t i = n; i-- > 0;) {
            Vec2 d = joints[i] - joints[i + 1];
            float len = d.length();
            Vec2 dir = len > 1e-6f ? d / len : Vec2{-1.f, 0.f};
            joints[i] = joints[i + 1] + dir * lengths[i];
        }
        // Forward: the root back in place, each segment kept within its limits.
        joints[0] = root;
        float prev = o.baseAngle;
        for (size_t i = 0; i < n; ++i) {
            Vec2 d = joints[i + 1] - joints[i];
            float a = d.lengthSq() > 1e-12f ? angle(d) : prev;
            if (limited) a = prev + clamp(angleDelta(prev, a), o.minAngle[i], o.maxAngle[i]);
            joints[i + 1] = joints[i] + fromAngle(a, lengths[i]);
            prev = a;
        }
        if ((joints[n] - target).lengthSq() <= tol2) return true;
    }
    return (joints[n] - target).lengthSq() <= tol2;
}

void stepVerlet(std::span<Vec2> points, std::span<Vec2> previous, Vec2 anchor, float segmentLength, float dt,
                const VerletOptions& o) {
    size_t n = std::min(points.size(), previous.size());
    if (n == 0) return;
    dt = clamp(dt, 0.f, 1.f / 20.f);
    float keep = std::pow(clamp(o.damping, 0.f, 1.f), dt * 60.f);
    points[0] = previous[0] = anchor;
    for (size_t i = 1; i < n; ++i) {
        Vec2 v = (points[i] - previous[i]) * keep;
        previous[i] = points[i];
        points[i] = points[i] + v + o.gravity * (dt * dt);
    }
    float stiff = clamp(o.stiffness, 0.f, 1.f);
    for (int iter = 0; iter < std::max(1, o.iterations); ++iter) {
        points[0] = anchor;
        // Alternate the sweep direction so neither end of the chain is favoured.
        for (size_t k = 0; k + 1 < n; ++k) {
            size_t i = iter % 2 == 0 ? k : n - 2 - k;
            Vec2 delta = points[i + 1] - points[i];
            float d = delta.length();
            if (d < 1e-6f) continue;
            Vec2 fix = delta * ((d - segmentLength) / d);
            if (i == 0) {
                points[1] = points[1] - fix;
            } else {
                points[i] = points[i] + fix * 0.5f;
                points[i + 1] = points[i + 1] - fix * 0.5f;
            }
        }
        if (stiff > 0.f) {
            // Bending: push the neighbours of each point apart toward the
            // distance they have when the chain is straight. Both ends move,
            // so stiffness adds no net force and the chain still hangs.
            for (size_t i = 1; i + 1 < n; ++i) {
                Vec2 delta = points[i + 1] - points[i - 1];
                float d = delta.length();
                float rest = 2.f * segmentLength;
                if (d < 1e-6f || d >= rest) continue;
                Vec2 fix = delta * ((d - rest) / d * stiff);
                if (i == 1) {
                    points[2] = points[2] - fix;
                } else {
                    points[i - 1] = points[i - 1] + fix * 0.5f;
                    points[i + 1] = points[i + 1] - fix * 0.5f;
                }
            }
        }
        if (o.pinEnd && n > 1) points[n - 1] = o.endTarget;
        for (size_t i = 1; i < n; ++i) points[i].y = std::min(points[i].y, o.ground);
    }
}

bool Foot::update(Vec2 rest, const StepOptions& o, float dt, bool canStep) {
    if (planted()) {
        if (!canStep || (rest - position).lengthSq() <= o.distance * o.distance) return false;
        from = position;
        Vec2 dir = (rest - position).normalized();
        to = rest + dir * (o.lead * o.distance);
        progress = 0.f;
    }
    progress = o.duration > 0.f ? std::min(1.f, progress + dt / o.duration) : 1.f;
    if (progress >= 1.f) {
        position = to;
        return true;
    }
    float e = progress * progress * (3.f - 2.f * progress);
    position = lerp(from, to, e) + o.up * (o.height * std::sin(Pi * progress));
    return false;
}

} // namespace drift
