#pragma once

// Procedural animation: solvers that pose limbs, chains and ropes from
// targets every frame instead of playing authored frames.
//
//   twoBoneIk     an arm or leg of two bones reaching for a point
//   solveFabrik   a chain of any length reaching for a point, with joint limits
//   stepVerlet    a rope, tail or strip that swings under gravity
//   Foot          a foot that stays planted and steps when it strays
//
// Angles are radians from +x toward +y (clockwise on screen, where y points
// down). The small helpers they build on (wrapAngle, angleDelta, lerpAngle,
// smoothDamp, damp) are in drift/math/Vector.hpp.

#include <drift/Types.hpp>

#include <span>

namespace drift {

// `current` turned toward `target` along the shorter arc by at most
// `maxStep` radians; never overshoots. The result is `target` exactly once
// within reach, otherwise `current` plus the step (not wrapped).
float rotateTowards(float current, float target, float maxStep);

// A damped spring of a float, a Vec2 or an angle: `stiffness` pulls toward
// the target, `damping` slows the motion. Damping 2 * sqrt(stiffness) is
// critical: the fastest approach with no overshoot; less wobbles. Stable at
// any frame time (it substeps long frames).
void springStep(float& value, float& velocity, float target, float stiffness, float damping, float dt);
void springStep(Vec2& value, Vec2& velocity, Vec2 target, float stiffness, float damping, float dt);
// The same along the shorter arc; `value` is left unwrapped.
void angleSpringStep(float& value, float& velocity, float target, float stiffness, float damping, float dt);

// ---- Two-bone IK ----

struct TwoBonePose {
    Vec2 joint;            // the elbow or knee
    Vec2 end;              // the hand or foot: the target, or as close as the bones reach
    float upperAngle = 0;  // world angle of the root -> joint bone
    float lowerAngle = 0;  // world angle of the joint -> end bone
    bool reached = false;  // false when the target was out of reach
};

// Bones of `upperLength` and `lowerLength` from `root` toward `target`.
// `bend` > 0 puts the joint clockwise of the root -> target line on screen
// (to the right when walking along it), < 0 counter-clockwise.
TwoBonePose twoBoneIk(Vec2 root, Vec2 target, float upperLength, float lowerLength, float bend);

// ---- FABRIK chains ----

struct FabrikOptions {
    int iterations = 10;
    float tolerance = 0.5f;          // stop once the end is this close
    // Optional per-joint limits, one pair per segment: segment i may turn
    // between minAngle[i] and maxAngle[i] relative to segment i - 1 (segment
    // 0 relative to `baseAngle`). Empty spans mean no limits.
    std::span<const float> minAngle;
    std::span<const float> maxAngle;
    float baseAngle = 0.f;
};

// Moves joints[1..n] so joints[n] reaches for `target` while joints[0] stays
// where it is; segment i runs from joints[i] to joints[i + 1] and keeps
// lengths[i]. Returns true when the end got within `tolerance`.
bool solveFabrik(std::span<Vec2> joints, std::span<const float> lengths, Vec2 target, const FabrikOptions& options = {});

// ---- Verlet ropes ----

struct VerletOptions {
    Vec2 gravity = {0.f, 400.f};
    float damping = 0.99f;     // velocity kept per 1/60 s
    int iterations = 8;        // constraint passes; more is stiffer
    float stiffness = 0.f;     // 0 to 1: resistance to bending, for tails and hair that keep their shape
    float ground = 1e30f;      // points stay above this y
    bool pinEnd = false;       // hold the last point at `endTarget` too
    Vec2 endTarget;
};

// One step of a chain of points: points[0] is held at `anchor`, the rest
// swing with their velocity (points - previous), gravity and damping, then
// distance constraints keep segment i at `segmentLength`. `previous` must
// be as long as `points`.
void stepVerlet(std::span<Vec2> points, std::span<Vec2> previous, Vec2 anchor, float segmentLength, float dt,
                const VerletOptions& options = {});

// ---- Stepping feet ----

struct StepOptions {
    float distance = 30.f;   // step when the foot is this far from its rest target
    float duration = 0.15f;  // seconds a step takes
    float height = 12.f;     // how high the foot lifts mid-step
    float lead = 0.3f;       // land this fraction of `distance` past the rest target, toward where it moved
    Vec2 up = {0.f, -1.f};   // the direction the foot lifts
};

// A foot that stays planted where it landed and steps to its rest target
// (where it would stand under the body) once it strays too far.
struct Foot {
    Vec2 position;           // where to draw the foot this frame
    Vec2 from, to;           // the current step
    float progress = 1.f;    // 0 to 1 through a step; 1 when planted

    bool planted() const { return progress >= 1.f; }

    // Advances the foot. A planted foot starts a step when `canStep` and it
    // is more than `distance` from `rest`. Returns true on the frame it
    // lands.
    bool update(Vec2 rest, const StepOptions& options, float dt, bool canStep = true);
};

} // namespace drift
