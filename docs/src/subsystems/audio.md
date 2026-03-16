# Audio

The `AudioResource` handles sound effects and music playback. Sound effects are loaded into memory and can be played with volume and panning control. Music streams from disk and only one track plays at a time.

**Header:** `#include <drift/resources/AudioResource.hpp>`

**Plugin:** `AudioPlugin` (included in `DefaultPlugins`)

## Sound Effects

### Loading

```cpp
SoundHandle loadSound(const char* path);
void        destroySound(SoundHandle sound);
```

`loadSound` loads a WAV, OGG, or MP3 file into memory and returns a `SoundHandle`. Load sounds once in a startup system and reuse the handle.

```cpp
struct GameAudio : public Resource {
    DRIFT_RESOURCE(GameAudio)
    SoundHandle jump;
    SoundHandle shoot;
    SoundHandle hit;
};

void loadAudio(ResMut<AudioResource> audio, ResMut<GameAudio> ga) {
    ga->jump  = audio->loadSound("assets/audio/jump.wav");
    ga->shoot = audio->loadSound("assets/audio/shoot.wav");
    ga->hit   = audio->loadSound("assets/audio/hit.ogg");
}
```

### Playing

```cpp
PlayingSoundHandle playSound(SoundHandle sound, float volume = 1.f, float pan = 0.f);
void stopSound(PlayingSoundHandle playing);
void setSoundVolume(PlayingSoundHandle playing, float volume);
void setSoundPan(PlayingSoundHandle playing, float pan);
bool isSoundPlaying(PlayingSoundHandle playing) const;
```

`playSound` plays a loaded sound and returns a `PlayingSoundHandle` that refers to that specific playback instance. You can have multiple instances of the same sound playing simultaneously.

- **volume**: `0.0` (silent) to `1.0` (full). Values above `1.0` amplify the sound.
- **pan**: `-1.0` (left) to `1.0` (right). `0.0` is center.

```cpp
void playerJump(Res<InputResource> input, ResMut<AudioResource> audio,
                Res<GameAudio> ga) {
    if (input->keyPressed(Key::Space)) {
        audio->playSound(ga->jump, 0.8f);
    }
}
```

### Controlling Active Playback

The `PlayingSoundHandle` lets you adjust or stop a sound after it starts playing:

```cpp
void engineSound(ResMut<AudioResource> audio, ResMut<GameState> state,
                 Query<Velocity2D, With<PlayerTag>> query) {
    query.iter([&](const Velocity2D& vel) {
        float speed = vel.linear.length();

        if (!state->enginePlaying) {
            state->engineSound = audio->playSound(state->engineLoop, 0.0f);
            state->enginePlaying = true;
        }

        // Volume scales with speed
        float vol = std::clamp(speed / 500.f, 0.f, 1.f);
        audio->setSoundVolume(state->engineSound, vol);
    });
}
```

## Music

Music is streamed from disk. Only one music track can play at a time -- calling `playMusic` while music is already playing stops the previous track.

```cpp
bool playMusic(const char* path, bool loop = true);
void stopMusic();
void pauseMusic();
void resumeMusic();
void setMusicVolume(float volume);
bool isMusicPlaying() const;
```

- **path**: Path to a WAV, OGG, or MP3 file.
- **loop**: If `true` (default), the track loops indefinitely.
- **volume**: `0.0` to `1.0`.

### Example: Background Music

```cpp
void startMusic(ResMut<AudioResource> audio) {
    audio->playMusic("assets/music/theme.ogg");
    audio->setMusicVolume(0.5f);
}
```

### Example: Pause Menu

```cpp
void togglePause(Res<InputResource> input, ResMut<AudioResource> audio,
                 ResMut<GameState> state) {
    if (input->keyPressed(Key::Escape)) {
        state->paused = !state->paused;
        if (state->paused) {
            audio->pauseMusic();
        } else {
            audio->resumeMusic();
        }
    }
}
```

## Complete Example

A typical audio setup with sound effects and music:

```cpp
struct GameAudio : public Resource {
    DRIFT_RESOURCE(GameAudio)
    SoundHandle jump;
    SoundHandle coin;
    SoundHandle explosion;
};

void setupAudio(ResMut<AudioResource> audio, ResMut<GameAudio> ga) {
    ga->jump      = audio->loadSound("assets/sfx/jump.wav");
    ga->coin      = audio->loadSound("assets/sfx/coin.wav");
    ga->explosion = audio->loadSound("assets/sfx/explosion.ogg");

    audio->playMusic("assets/music/level1.ogg");
    audio->setMusicVolume(0.4f);
}

void collectCoin(ResMut<AudioResource> audio, Res<GameAudio> ga,
                 EventReader<SensorStart> sensors) {
    for (auto& event : sensors) {
        // Play coin sound with slight random panning for spatial feel
        float pan = (rand() % 100 - 50) / 100.f;
        audio->playSound(ga->coin, 0.7f, pan);
    }
}

void onExplosion(ResMut<AudioResource> audio, Res<GameAudio> ga) {
    audio->playSound(ga->explosion, 1.0f);
}

int main() {
    App app;
    app.setConfig({.title = "Audio Demo", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.initResource<GameAudio>();
    app.startup<setupAudio>("setup_audio");
    app.update<collectCoin>("collect_coin");
    return app.run();
}
```
