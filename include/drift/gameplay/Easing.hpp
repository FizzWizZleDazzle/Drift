#pragma once

// Easing curves: map a normalized time t in [0, 1] to progress. Every curve
// returns 0 at t = 0 and 1 at t = 1; Back, Elastic and Spring overshoot in
// between.

#include <cmath>
#include <cstdint>

namespace drift {

enum class Ease : uint8_t {
    Linear,
    InSine, OutSine, InOutSine,
    InQuad, OutQuad, InOutQuad,
    InCubic, OutCubic, InOutCubic,
    InQuart, OutQuart, InOutQuart,
    InQuint, OutQuint, InOutQuint,
    InExpo, OutExpo, InOutExpo,
    InCirc, OutCirc, InOutCirc,
    InBack, OutBack, InOutBack,
    InElastic, OutElastic, InOutElastic,
    InBounce, OutBounce, InOutBounce,
    Spring,        // overshoots and settles, like a damped spring
    Count_,
};

namespace easing {

inline constexpr float kPi = 3.14159265358979323846f;

inline float outBounce(float t) {
    constexpr float n = 7.5625f, d = 2.75f;
    if (t < 1.f / d) return n * t * t;
    if (t < 2.f / d) { t -= 1.5f / d; return n * t * t + 0.75f; }
    if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + 0.9375f; }
    t -= 2.625f / d;
    return n * t * t + 0.984375f;
}

inline float apply(Ease e, float t) {
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    constexpr float c1 = 1.70158f;
    constexpr float c2 = c1 * 1.525f;
    constexpr float c3 = c1 + 1.f;
    constexpr float c4 = (2.f * kPi) / 3.f;
    constexpr float c5 = (2.f * kPi) / 4.5f;
    switch (e) {
        case Ease::Linear:     return t;
        case Ease::InSine:     return 1.f - std::cos(t * kPi * 0.5f);
        case Ease::OutSine:    return std::sin(t * kPi * 0.5f);
        case Ease::InOutSine:  return -(std::cos(kPi * t) - 1.f) * 0.5f;
        case Ease::InQuad:     return t * t;
        case Ease::OutQuad:    return 1.f - (1.f - t) * (1.f - t);
        case Ease::InOutQuad:  return t < 0.5f ? 2.f * t * t : 1.f - std::pow(-2.f * t + 2.f, 2.f) * 0.5f;
        case Ease::InCubic:    return t * t * t;
        case Ease::OutCubic:   return 1.f - std::pow(1.f - t, 3.f);
        case Ease::InOutCubic: return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) * 0.5f;
        case Ease::InQuart:    return t * t * t * t;
        case Ease::OutQuart:   return 1.f - std::pow(1.f - t, 4.f);
        case Ease::InOutQuart: return t < 0.5f ? 8.f * t * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 4.f) * 0.5f;
        case Ease::InQuint:    return t * t * t * t * t;
        case Ease::OutQuint:   return 1.f - std::pow(1.f - t, 5.f);
        case Ease::InOutQuint: return t < 0.5f ? 16.f * t * t * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 5.f) * 0.5f;
        case Ease::InExpo:     return std::pow(2.f, 10.f * t - 10.f);
        case Ease::OutExpo:    return 1.f - std::pow(2.f, -10.f * t);
        case Ease::InOutExpo:  return t < 0.5f ? std::pow(2.f, 20.f * t - 10.f) * 0.5f
                                               : (2.f - std::pow(2.f, -20.f * t + 10.f)) * 0.5f;
        case Ease::InCirc:     return 1.f - std::sqrt(1.f - t * t);
        case Ease::OutCirc:    return std::sqrt(1.f - (t - 1.f) * (t - 1.f));
        case Ease::InOutCirc:  return t < 0.5f ? (1.f - std::sqrt(1.f - 4.f * t * t)) * 0.5f
                                               : (std::sqrt(1.f - std::pow(-2.f * t + 2.f, 2.f)) + 1.f) * 0.5f;
        case Ease::InBack:     return c3 * t * t * t - c1 * t * t;
        case Ease::OutBack:    return 1.f + c3 * std::pow(t - 1.f, 3.f) + c1 * std::pow(t - 1.f, 2.f);
        case Ease::InOutBack:  return t < 0.5f
            ? (std::pow(2.f * t, 2.f) * ((c2 + 1.f) * 2.f * t - c2)) * 0.5f
            : (std::pow(2.f * t - 2.f, 2.f) * ((c2 + 1.f) * (t * 2.f - 2.f) + c2) + 2.f) * 0.5f;
        case Ease::InElastic:  return -std::pow(2.f, 10.f * t - 10.f) * std::sin((t * 10.f - 10.75f) * c4);
        case Ease::OutElastic: return std::pow(2.f, -10.f * t) * std::sin((t * 10.f - 0.75f) * c4) + 1.f;
        case Ease::InOutElastic: return t < 0.5f
            ? -(std::pow(2.f, 20.f * t - 10.f) * std::sin((20.f * t - 11.125f) * c5)) * 0.5f
            : (std::pow(2.f, -20.f * t + 10.f) * std::sin((20.f * t - 11.125f) * c5)) * 0.5f + 1.f;
        case Ease::InBounce:   return 1.f - outBounce(1.f - t);
        case Ease::OutBounce:  return outBounce(t);
        case Ease::InOutBounce: return t < 0.5f ? (1.f - outBounce(1.f - 2.f * t)) * 0.5f
                                                : (1.f + outBounce(2.f * t - 1.f)) * 0.5f;
        case Ease::Spring:
            return (std::sin(t * kPi * (0.2f + 2.5f * t * t * t)) * std::pow(1.f - t, 2.2f) + t) *
                   (1.f + 1.2f * (1.f - t));
        case Ease::Count_: break;
    }
    return t;
}

} // namespace easing

inline float ease(Ease e, float t) { return easing::apply(e, t); }

} // namespace drift
