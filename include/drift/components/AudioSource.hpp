#pragma once

#include <drift/Handle.hpp>
#include <drift/Types.hpp>
#include <drift/resources/AudioResource.hpp>

#include <algorithm>
#include <cmath>

namespace drift {

// How a spatial source gets quieter between minDistance and maxDistance.
enum class Rolloff {
    Linear,  // straight line from full volume to silence
    Inverse, // falls off quickly near the source, like real sound; silent at maxDistance
};

// Plays a sound from an entity. With `spatial`, volume and pan follow the
// entity's position relative to the AudioListener.
struct AudioSource {
    SoundHandle sound;
    BusHandle bus;               // invalid = the sfx bus
    float volume = 1.f;
    float pitch = 1.f;
    bool loop = false;
    bool autoplay = true;        // start when the component first appears
    bool spatial = false;
    float minDistance = 64.f;    // full volume inside this radius
    float maxDistance = 1024.f;  // silent beyond this radius
    Rolloff rolloff = Rolloff::Linear;
    int priority = 0;
    bool playRequested = false;  // set to start (or restart) playback; cleared when started
    PlayingSoundHandle instance; // the current playback, set by AudioPlugin
};

// Marks the entity whose position spatial sources are heard from, usually
// the camera. The first active listener wins.
struct AudioListener {
    bool active = true;
};

struct SpatialMix {
    float gain = 1.f;
    float pan = 0.f;
};

// Distance attenuation and stereo pan for a source heard from a listener.
// Pan is the horizontal offset divided by maxDistance, so a source halfway
// to maxDistance on the right plays at pan 0.5.
inline SpatialMix spatialize(Vec2 listener, Vec2 source, const AudioSource& s) {
    float dx = source.x - listener.x;
    float dy = source.y - listener.y;
    float d = std::sqrt(dx * dx + dy * dy);
    float minD = std::max(0.f, s.minDistance);
    float maxD = std::max(minD + 1e-3f, s.maxDistance);

    SpatialMix m;
    if (d <= minD) {
        m.gain = 1.f;
    } else if (d >= maxD) {
        m.gain = 0.f;
    } else if (s.rolloff == Rolloff::Linear) {
        m.gain = 1.f - (d - minD) / (maxD - minD);
    } else {
        float ref = std::max(minD, 1e-3f);
        float atMax = ref / maxD;
        m.gain = (ref / d - atMax) / (1.f - atMax);
    }
    m.pan = std::clamp(dx / maxD, -1.f, 1.f);
    return m;
}

} // namespace drift
