#include "Test.hpp"

#include <drift/Hierarchy.hpp>
#include <drift/World.hpp>

using namespace drift;

// ---------------------------------------------------------------------------
// Hierarchy
// ---------------------------------------------------------------------------

namespace {

EntityId spawnAt(World& w, Vec2 p, float rot = 0.f, Vec2 scale = {1.f, 1.f}) {
    EntityId e = w.createEntity();
    w.set(e, w.componentRegistry().get<Transform2D>(), Transform2D{p, rot, scale});
    return e;
}

} // namespace

TEST(hierarchy_links_stay_consistent) {
    World w;
    EntityId a = spawnAt(w, {0, 0}), b = spawnAt(w, {10, 0}), c = spawnAt(w, {20, 0});
    CHECK(setParent(w, b, a));
    CHECK(setParent(w, c, b));
    CHECK(parentOf(w, c) == b);
    CHECK(parentOf(w, a) == InvalidEntityId);
    CHECK(childrenOf(w, a).size() == 1);
    CHECK(descendantsOf(w, a).size() == 2);
    // No loops: a cannot go under its own grandchild.
    CHECK(!setParent(w, a, c));
    CHECK(!setParent(w, a, a));
    // Moving c under a takes it off b.
    CHECK(setParent(w, c, a));
    CHECK(childrenOf(w, b).empty());
    CHECK(childrenOf(w, a).size() == 2);
    CHECK(removeParent(w, c));
    CHECK(parentOf(w, c) == InvalidEntityId);
    CHECK(childrenOf(w, a).size() == 1);
}

TEST(hierarchy_world_transforms) {
    World w;
    const float pi = 3.14159265f;
    EntityId root = spawnAt(w, {100, 50}, pi / 2.f, {2.f, 2.f});
    EntityId child = spawnAt(w, {10, 0}, 0.25f);
    setParent(w, child, root);
    GlobalTransform2D g = worldTransformOf(w, child);
    // (10, 0) scaled by 2 and turned a quarter turn clockwise on screen.
    CHECK_NEAR(g.position.x, 100.f, 1e-3);
    CHECK_NEAR(g.position.y, 70.f, 1e-3);
    CHECK_NEAR(g.rotation, pi / 2.f + 0.25f, 1e-5);
    CHECK_NEAR(g.scale.x, 2.f, 1e-6);
    Vec2 back = worldToLocal(w, root, g.position);
    CHECK_NEAR(back.x, 10.f, 1e-3);
    CHECK_NEAR(back.y, 0.f, 1e-3);
    Vec2 there = localToWorld(w, root, {0, 5});
    CHECK_NEAR(there.x, 90.f, 1e-3);
    CHECK_NEAR(there.y, 50.f, 1e-3);
}

TEST(hierarchy_mirrored_parent_mirrors_children) {
    World w;
    EntityId root = spawnAt(w, {0, 0}, 0.f, {-1.f, 1.f});
    EntityId child = spawnAt(w, {10, 5}, 0.5f);
    setParent(w, child, root);
    GlobalTransform2D g = worldTransformOf(w, child);
    CHECK_NEAR(g.position.x, -10.f, 1e-4);
    CHECK_NEAR(g.position.y, 5.f, 1e-4);
    CHECK_NEAR(g.rotation, -0.5f, 1e-6);
    CHECK_NEAR(g.scale.x, -1.f, 1e-6);
}

TEST(hierarchy_keep_world_reparents_in_place) {
    World w;
    EntityId a = spawnAt(w, {100, 100}, 1.0f, {2.f, 2.f});
    EntityId b = spawnAt(w, {-40, 30}, -0.3f, {0.5f, 1.5f});
    EntityId item = spawnAt(w, {7, 3}, 0.2f);
    setParent(w, item, a);
    GlobalTransform2D before = worldTransformOf(w, item);
    CHECK(setParent(w, item, b, true));
    GlobalTransform2D after = worldTransformOf(w, item);
    CHECK_NEAR(after.position.x, before.position.x, 1e-3);
    CHECK_NEAR(after.position.y, before.position.y, 1e-3);
    CHECK_NEAR(after.rotation, before.rotation, 1e-5);
    CHECK(removeParent(w, item, true));
    GlobalTransform2D root = worldTransformOf(w, item);
    CHECK_NEAR(root.position.x, before.position.x, 1e-3);
    CHECK_NEAR(root.position.y, before.position.y, 1e-3);
}

TEST(hierarchy_despawn_takes_the_subtree) {
    World w;
    EntityId a = spawnAt(w, {0, 0}), b = spawnAt(w, {0, 0}), c = spawnAt(w, {0, 0}), d = spawnAt(w, {0, 0});
    setParent(w, b, a);
    setParent(w, c, b);
    setParent(w, d, a);
    despawnRecursive(w, b);
    CHECK(!w.isAlive(b));
    CHECK(!w.isAlive(c));
    CHECK(w.isAlive(a));
    CHECK(w.isAlive(d));
    CHECK(childrenOf(w, a).size() == 1);
    despawnRecursive(w, a);
    CHECK(!w.isAlive(d));
}

// ---------------------------------------------------------------------------
// Procedural animation
// ---------------------------------------------------------------------------

#include <drift/gameplay/ProceduralAnimation.hpp>
#include <drift/math/Vector.hpp>

#include <cmath>
#include <vector>

TEST(procanim_rotate_towards_takes_the_short_arc) {
    // From just below +pi to just above -pi is a short step across the seam.
    float r = rotateTowards(3.0f, -3.0f, 0.1f);
    CHECK_NEAR(r, 3.1f, 1e-5);
    CHECK_NEAR(rotateTowards(0.f, 0.05f, 0.1f), 0.05f, 1e-6);
    CHECK_NEAR(rotateTowards(0.f, -1.f, 0.25f), -0.25f, 1e-6);
}

TEST(procanim_springs_settle) {
    float x = 0.f, v = 0.f;
    for (int i = 0; i < 240; ++i) springStep(x, v, 10.f, 200.f, 2.f * std::sqrt(200.f), 1.f / 60.f);
    CHECK_NEAR(x, 10.f, 1e-2);
    // Critical damping does not overshoot.
    float y = 0.f, w = 0.f, peak = 0.f;
    for (int i = 0; i < 240; ++i) {
        springStep(y, w, 1.f, 100.f, 20.f, 1.f / 60.f);
        peak = std::max(peak, y);
    }
    CHECK(peak <= 1.0001f);
    // A long frame stays stable.
    float z = 0.f, u = 0.f;
    springStep(z, u, 1.f, 5000.f, 10.f, 0.5f);
    CHECK(std::fabs(z) < 10.f);
    float a = 3.0f, av = 0.f;
    for (int i = 0; i < 240; ++i) angleSpringStep(a, av, -3.0f, 100.f, 20.f, 1.f / 60.f);
    CHECK_NEAR(wrapAngle(a), -3.0f, 1e-3);
    CHECK(a > 3.0f);   // went across the seam, not the long way round
}

TEST(procanim_two_bone_ik) {
    TwoBonePose p = twoBoneIk({0, 0}, {10, 0}, 8.f, 8.f, 1.f);
    CHECK(p.reached);
    CHECK_NEAR(p.end.x, 10.f, 1e-3);
    CHECK_NEAR(p.end.y, 0.f, 1e-3);
    CHECK_NEAR((p.joint - Vec2{0, 0}).length(), 8.f, 1e-3);
    CHECK_NEAR((p.end - p.joint).length(), 8.f, 1e-3);
    CHECK(p.joint.y > 0.f);   // bend > 0: clockwise on screen, below the +x line
    TwoBonePose q = twoBoneIk({0, 0}, {10, 0}, 8.f, 8.f, -1.f);
    CHECK(q.joint.y < 0.f);
    TwoBonePose far = twoBoneIk({0, 0}, {100, 0}, 8.f, 8.f, 1.f);
    CHECK(!far.reached);
    CHECK_NEAR(far.end.x, 16.f, 1e-2);
}

TEST(procanim_fabrik_reaches_and_keeps_lengths) {
    std::vector<Vec2> j = {{0, 0}, {10, 0}, {20, 0}, {30, 0}};
    std::vector<float> len = {10, 10, 10};
    CHECK(solveFabrik(j, len, {15, 15}));
    CHECK_NEAR(j[0].x, 0.f, 1e-6);
    CHECK_NEAR((j[3] - Vec2{15, 15}).length(), 0.f, 0.5);
    for (int i = 0; i < 3; ++i) CHECK_NEAR((j[i + 1] - j[i]).length(), 10.f, 1e-3);
    // Out of reach: the chain points straight at the target.
    CHECK(!solveFabrik(j, len, {0, 100}));
    CHECK_NEAR(j[3].y, 30.f, 0.1);
    // Limits: no joint may bend more than 0.2 rad, so a target behind cannot be reached.
    std::vector<Vec2> k = {{0, 0}, {10, 0}, {20, 0}, {30, 0}};
    std::vector<float> lo = {-0.2f, -0.2f, -0.2f}, hi = {0.2f, 0.2f, 0.2f};
    FabrikOptions o;
    o.minAngle = lo;
    o.maxAngle = hi;
    solveFabrik(k, len, {-10, 5}, o);
    float prev = 0.f;
    for (int i = 0; i < 3; ++i) {
        float a = angle(k[i + 1] - k[i]);
        CHECK(std::fabs(angleDelta(prev, a)) <= 0.2f + 1e-4f);
        prev = a;
    }
}

TEST(procanim_verlet_hangs_and_keeps_segments) {
    std::vector<Vec2> pts, prev;
    for (int i = 0; i < 6; ++i) pts.push_back({float(i) * 10.f, 0.f});
    prev = pts;
    for (int f = 0; f < 1200; ++f) stepVerlet(pts, prev, {0, 0}, 10.f, 1.f / 60.f);
    CHECK_NEAR(pts[0].x, 0.f, 1e-6);
    // Hanging straight down after it settles.
    CHECK_NEAR(pts[5].x, 0.f, 1.0);
    CHECK_NEAR(pts[5].y, 50.f, 1.0);
    for (int i = 0; i < 5; ++i) CHECK_NEAR((pts[i + 1] - pts[i]).length(), 10.f, 0.5);
    // A stiff chain resists bending but still falls and hangs: stiffness
    // adds no energy.
    std::vector<Vec2> tail, tailPrev;
    for (int i = 0; i < 6; ++i) tail.push_back({float(i) * 10.f, 0.f});
    tailPrev = tail;
    VerletOptions stiff;
    stiff.stiffness = 0.8f;
    float highest = 1e9f;
    for (int f = 0; f < 1200; ++f) {
        stepVerlet(tail, tailPrev, {0, 0}, 10.f, 1.f / 60.f, stiff);
        highest = std::min(highest, tail[5].y);
    }
    CHECK(highest > -1.f);   // never swings up past where it started
    CHECK_NEAR(tail[5].y, 50.f, 2.0);
    for (int i = 0; i < 5; ++i) CHECK_NEAR((tail[i + 1] - tail[i]).length(), 10.f, 1.0);

    VerletOptions o;
    o.ground = 20.f;
    for (int f = 0; f < 120; ++f) stepVerlet(pts, prev, {0, 0}, 10.f, 1.f / 60.f, o);
    for (auto& p : pts) CHECK(p.y <= 20.f + 1e-4f);
}

TEST(procanim_verlet_stiffness_keeps_a_tail_straighter) {
    // A gravity-free tail dragged back and forth curls behind its anchor;
    // the stiffer it is, the further its tip stays from its base.
    auto reach = [](float stiffness) {
        std::vector<Vec2> p, q;
        for (int i = 0; i < 14; ++i) p.push_back({-7.f * float(i), 0.f});
        q = p;
        VerletOptions o;
        o.gravity = {0, 0};
        o.damping = 0.9f;
        o.stiffness = stiffness;
        float sum = 0.f;
        int count = 0;
        for (int f = 0; f < 600; ++f) {
            float t = float(f) / 60.f;
            Vec2 a{80.f * std::sin(Pi * t), 25.f * std::sin(2.f * Pi * t)};
            stepVerlet(p, q, a, 7.f, 1.f / 60.f, o);
            if (f > 240) {
                sum += (p[13] - p[0]).length();
                ++count;
            }
        }
        return sum / float(count);
    };
    float loose = reach(0.f), half = reach(0.5f), full = reach(1.f);
    CHECK(half > loose + 5.f);
    CHECK(full > half + 3.f);
}

TEST(procanim_foot_steps_when_it_strays) {
    Foot f;
    f.position = f.from = f.to = {0, 0};
    StepOptions o;
    o.distance = 10.f;
    o.duration = 0.1f;
    o.height = 5.f;
    o.lead = 0.f;
    CHECK(!f.update({5, 0}, o, 1.f / 60.f));   // close enough: stays planted
    CHECK(f.planted());
    CHECK_NEAR(f.position.x, 0.f, 1e-6);
    f.update({20, 0}, o, 0.05f);               // strays: steps, halfway and lifted
    CHECK(!f.planted());
    CHECK_NEAR(f.position.x, 10.f, 1e-3);
    CHECK_NEAR(f.position.y, -5.f, 1e-3);
    CHECK(f.update({20, 0}, o, 0.05f));        // lands
    CHECK_NEAR(f.position.x, 20.f, 1e-4);
    Foot g;
    g.position = g.to = {0, 0};
    CHECK(!g.update({50, 0}, o, 0.01f, false));   // not allowed to step yet
    CHECK(g.planted());
}
