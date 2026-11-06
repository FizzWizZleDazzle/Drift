#pragma once

// Tweens: animate an entity's Transform2D, Sprite tint/alpha or Camera zoom,
// or any float through a callback, over time with an easing curve. A tween
// is a sequence of steps (tweens, delays, callbacks) that can loop or
// ping-pong. Finished tweens send a TweenFinished event.
//
//   tweens->on(entity).moveTo({200, 0}, 0.5f, Ease::OutBack)
//                     .delay(0.2f)
//                     .fadeTo(0.f, 0.3f)
//                     .start();

#include <drift/Types.hpp>
#include <drift/Resource.hpp>
#include <drift/Events.hpp>
#include <drift/gameplay/Easing.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace drift {

class World;

using TweenId = uint32_t;
constexpr TweenId InvalidTweenId = 0;

enum class TweenLoop : uint8_t {
    Restart,   // jump back to the first step each cycle
    Yoyo,      // play forward, then backward, alternating
};

// Sent when a tween completes all its cycles (or is stopped with complete = true).
struct TweenFinished {
    TweenId id = InvalidTweenId;
    EntityId entity = InvalidEntityId;
};

enum class TweenProperty : uint8_t {
    Position,     // Transform2D::position
    Rotation,     // Transform2D::rotation
    Scale,        // Transform2D::scale
    Tint,         // Sprite::tint (all four channels)
    Alpha,        // Sprite::tint.a, as 0..1
    CameraZoom,   // Camera::zoom
};

class Tweens;

// Builds a tween step by step; start() hands it to Tweens. Steps run one
// after another. Each property step starts from the entity's value at the
// moment the step begins.
class TweenBuilder {
public:
    TweenBuilder& moveTo(Vec2 to, float duration, Ease ease = Ease::OutQuad);
    TweenBuilder& moveBy(Vec2 delta, float duration, Ease ease = Ease::OutQuad);
    TweenBuilder& rotateTo(float radians, float duration, Ease ease = Ease::OutQuad);
    TweenBuilder& rotateBy(float radians, float duration, Ease ease = Ease::OutQuad);
    TweenBuilder& scaleTo(Vec2 to, float duration, Ease ease = Ease::OutQuad);
    TweenBuilder& tintTo(Color to, float duration, Ease ease = Ease::Linear);
    TweenBuilder& fadeTo(float alpha, float duration, Ease ease = Ease::Linear);
    TweenBuilder& zoomTo(float zoom, float duration, Ease ease = Ease::OutQuad);

    // Any float: `apply` receives the eased value each frame.
    TweenBuilder& value(float from, float to, float duration, Ease ease,
                        std::function<void(float)> apply);
    TweenBuilder& delay(float seconds);
    // Runs `fn` when the sequence reaches this step.
    TweenBuilder& call(std::function<void()> fn);

    // Play the whole sequence `times` times in total; -1 repeats forever.
    TweenBuilder& loop(int32_t times = -1, TweenLoop mode = TweenLoop::Restart);
    TweenBuilder& yoyo(int32_t times = -1) { return loop(times, TweenLoop::Yoyo); }

    // Hand the tween to Tweens; it begins on the next update.
    TweenId start();

    TweenBuilder(TweenBuilder&&) = default;
    ~TweenBuilder();

private:
    friend class Tweens;

    struct Step {
        enum class Kind : uint8_t { Property, Value, Delay, Call } kind = Kind::Delay;
        TweenProperty property = TweenProperty::Position;
        float from[4] = {};
        float to[4] = {};
        bool relative = false;   // `to` is an offset from the start value
        bool captured = false;   // `from` read from the entity
        float duration = 0.f;
        Ease ease = Ease::Linear;
        std::function<void(float)> onValue;
        std::function<void()> onCall;
    };

    struct Tween {
        TweenId id = InvalidTweenId;
        EntityId target = InvalidEntityId;
        std::vector<Step> steps;
        int32_t loops = 1;
        TweenLoop mode = TweenLoop::Restart;
        int32_t cyclesDone = 0;
        int32_t stepIndex = 0;
        float stepTime = 0.f;
        bool stepStarted = false;
        bool forward = true;
        bool paused = false;
        bool dead = false;
    };

    TweenBuilder(Tweens* owner, EntityId target);
    TweenBuilder& property(TweenProperty p, const float* to, bool relative, float duration, Ease ease);

    Tweens* owner_ = nullptr;
    Tween tween_;
    bool started_ = false;
};

class Tweens : public Resource {
public:
    DRIFT_RESOURCE(Tweens)

    // Tween properties of `target`.
    TweenBuilder on(EntityId target);
    // A tween with no entity (value / delay / call steps only).
    TweenBuilder sequence();

    // Stop a tween. With complete = true the remaining steps jump to their
    // end values and TweenFinished is sent.
    void stop(TweenId id, bool complete = false);
    // Stop every tween on `target`.
    void stopAll(EntityId target);
    void pause(TweenId id);
    void resume(TweenId id);
    bool isActive(TweenId id) const;
    size_t activeCount() const;

    // Multiplies the frame delta for every tween (0 freezes all of them).
    float timeScale = 1.f;

    // Advance all tweens by dt seconds and write to the world. TweenPlugin
    // calls this once per frame; call it yourself to drive tweens manually.
    void update(float dt, World& world, Events<TweenFinished>* finished);

private:
    friend class TweenBuilder;
    using Tween = TweenBuilder::Tween;
    using Step = TweenBuilder::Step;

    TweenId add(Tween&& t);
    Tween* find(TweenId id);
    const Tween* find(TweenId id) const;
    // Returns false when the target entity is gone.
    bool beginStep(Tween& t, Step& s, World& world);
    bool applyStep(Tween& t, Step& s, float progress, World& world);
    void advance(Tween& t, float dt, World& world, Events<TweenFinished>* finished);
    void finishAll(Tween& t, World& world);

    std::vector<Tween> active_;
    std::vector<Tween> pending_;       // started since the last update
    std::vector<TweenId> completing_;  // stop(id, true) requests
    TweenId nextId_ = 1;
};

} // namespace drift
