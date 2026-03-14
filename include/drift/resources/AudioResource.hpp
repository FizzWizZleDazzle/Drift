#pragma once

#include <drift/Resource.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>

namespace drift {

class AudioResource : public Resource {
public:
    AudioResource();
    ~AudioResource() override;

    DRIFT_RESOURCE(AudioResource)

    void update();

    // Sound effects
    SoundHandle loadSound(const char* path);
    void destroySound(SoundHandle sound);
    PlayingSoundHandle playSound(SoundHandle sound, float volume = 1.f, float pan = 0.f);
    void stopSound(PlayingSoundHandle playing);
    void setSoundVolume(PlayingSoundHandle playing, float volume);
    void setSoundPan(PlayingSoundHandle playing, float pan);
    bool isSoundPlaying(PlayingSoundHandle playing) const;

    // Music
    bool playMusic(const char* path, bool loop = true);
    void stopMusic();
    void pauseMusic();
    void resumeMusic();
    void setMusicVolume(float volume);
    bool isMusicPlaying() const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
