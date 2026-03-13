#pragma once

#include <drift/Resource.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>

namespace drift {

class RendererResource;
class AudioResource;
class FontResource;

class AssetServer : public Resource {
public:
    AssetServer(RendererResource* renderer, AudioResource* audio, FontResource* font);
    ~AssetServer() override;

    const char* name() const override { return "AssetServer"; }

    // Textures
    TextureHandle loadTexture(const char* path);
    void destroyTexture(TextureHandle texture);
    void getTextureSize(TextureHandle texture, int32_t* w, int32_t* h) const;

    // Sounds
    SoundHandle loadSound(const char* path);
    PlayingSoundHandle playSound(SoundHandle sound, float volume = 1.f, float pan = 0.f);

    // Fonts
    FontHandle loadFont(const char* path, int sizePx);

private:
    RendererResource* renderer_;
    AudioResource* audio_;
    FontResource* font_;
};

} // namespace drift
