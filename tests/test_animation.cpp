#include "Test.hpp"

#include <drift/components/SpriteAnimator.hpp>

#include <string>
#include <vector>

using namespace drift;

// Clip timing: per-frame durations, looping, one-shots with and without a
// clip to fall back to, speed, and seeking.

namespace {

SpriteAnimator heroAnimator() {
    SpritesheetHandle sheet{1};
    SpriteAnimator a;
    a.clips = {
        {"idle", clip(sheet, {0, 1, 2, 3}, 0.15f)},
        {"run", clip(sheet, {8, 9, 10}, 0.1f)},
        {"attack", clip(sheet, {4, 5, 6}, std::vector<float>{0.05f, 0.05f, 0.2f}).once()},
    };
    a.current = "idle";
    return a;
}

} // namespace

TEST(animation_clip_durations) {
    AnimationClip c = clip(SpritesheetHandle{1}, {4, 5, 6}, std::vector<float>{0.05f, 0.05f, 0.2f});
    CHECK_NEAR(c.frameSeconds(0), 0.05f, 1e-6);
    CHECK_NEAR(c.frameSeconds(2), 0.2f, 1e-6);
    CHECK_NEAR(c.length(), 0.3f, 1e-6);
    AnimationClip even = clip(SpritesheetHandle{1}, {0, 1, 2, 3}, 0.15f);
    CHECK(even.seconds.size() == 1);
    CHECK_NEAR(even.frameSeconds(3), 0.15f, 1e-6);
    CHECK_NEAR(even.length(), 0.6f, 1e-5);
    CHECK(even.looping);
    CHECK(!even.once().looping);
}

TEST(animation_loops_with_even_frames) {
    SpriteAnimator a = heroAnimator();
    std::vector<std::string> ended;
    a.advance(0.14f, &ended);
    CHECK(a.frame == 0);
    a.advance(0.02f, &ended);   // 0.16 s
    CHECK(a.frame == 1);
    a.advance(0.45f, &ended);   // 0.61 s: past the 0.6 s clip, back to frame 0
    CHECK(a.frame == 0);
    CHECK_NEAR(a.time(), 0.01f, 1e-4);
    CHECK(ended.empty());
    CHECK(!a.finished("idle"));
}

TEST(animation_per_frame_durations) {
    SpriteAnimator a = heroAnimator();
    CHECK(a.play("attack"));
    std::vector<std::string> ended;
    a.advance(0.04f, &ended);
    CHECK(a.frame == 0);
    a.advance(0.02f, &ended);   // 0.06 s
    CHECK(a.frame == 1);
    a.advance(0.05f, &ended);   // 0.11 s: the long last frame
    CHECK(a.frame == 2);
    a.advance(0.15f, &ended);   // 0.26 s, still inside the 0.2 s last frame
    CHECK(a.frame == 2);
    CHECK(ended.empty());
    a.advance(0.05f, &ended);   // 0.31 s: the clip ended and holds its last frame
    CHECK(a.frame == 2);
    REQUIRE(ended.size() == 1);
    CHECK(ended[0] == "attack");
    CHECK(a.finished("attack"));
    CHECK(a.current == "attack");
    ended.clear();
    a.advance(1.f, &ended);     // holding: no second ending
    CHECK(ended.empty());
    CHECK(a.frame == 2);
}

TEST(animation_play_once_falls_back) {
    SpriteAnimator a = heroAnimator();
    a.advance(0.2f);            // idle frame 1
    CHECK(a.playOnce("attack", "idle"));
    CHECK(a.frame == 0);
    std::vector<std::string> ended;
    a.advance(0.35f, &ended);   // 0.3 s of attack, then 0.05 s into idle
    REQUIRE(ended.size() == 1);
    CHECK(ended[0] == "attack");
    CHECK(a.current == "idle");
    CHECK(a.frame == 0);
    CHECK_NEAR(a.time(), 0.05f, 1e-4);
    CHECK(a.finished("attack"));
    CHECK(a.then.empty());
    // Switching to another clip clears what finished.
    CHECK(a.play("run"));
    CHECK(!a.finished("attack"));
}

TEST(animation_play_once_on_a_looping_clip) {
    SpriteAnimator a = heroAnimator();
    CHECK(a.playOnce("run", ""));
    std::vector<std::string> ended;
    a.advance(0.35f, &ended);
    REQUIRE(ended.size() == 1);
    CHECK(ended[0] == "run");
    CHECK(a.frame == 2);
    CHECK(a.current == "run");
}

TEST(animation_play_does_not_restart) {
    SpriteAnimator a = heroAnimator();
    a.advance(0.32f);
    CHECK(a.frame == 2);
    CHECK(a.play("idle"));
    CHECK(a.frame == 2);        // already playing: unchanged
    CHECK(a.play("run"));
    CHECK(a.frame == 0);
    // A one-shot that ended starts again when played.
    CHECK(a.play("attack"));
    a.advance(1.f);
    CHECK(a.finished("attack"));
    CHECK(a.play("attack"));
    CHECK(a.frame == 0);
    CHECK(!a.finished("attack"));
}

TEST(animation_unknown_clip_is_refused) {
    SpriteAnimator a = heroAnimator();
    CHECK(!a.play("jump"));
    CHECK(!a.playOnce("attack", "jump"));
    CHECK(a.current == "idle");
    CHECK(a.clipNames() == "idle, run, attack");
}

TEST(animation_speed_and_pause) {
    SpriteAnimator a = heroAnimator();
    a.speed = 2.f;
    a.advance(0.1f);            // 0.2 s of clip time
    CHECK(a.frame == 1);
    a.speed = 0.5f;
    a.advance(0.2f);            // 0.1 s more: 0.3 s
    CHECK(a.frame == 2);
    CHECK_NEAR(a.time(), 0.3f, 1e-4);
    a.pause();
    a.advance(1.f);
    CHECK(a.frame == 2);
    a.resume();
    a.speed = 0.f;
    a.advance(1.f);
    CHECK(a.frame == 2);
}

TEST(animation_seek) {
    SpriteAnimator a = heroAnimator();
    a.seek(0.5f);
    CHECK(a.frame == 3);
    CHECK_NEAR(a.time(), 0.5f, 1e-4);
    a.seek(0.65f);              // wraps on a looping clip
    CHECK(a.frame == 0);
    CHECK_NEAR(a.time(), 0.05f, 1e-4);
    a.play("attack");
    a.seek(5.f);                // clamps on a one-shot
    CHECK(a.frame == 2);
    CHECK_NEAR(a.time(), 0.3f, 1e-4);
}
