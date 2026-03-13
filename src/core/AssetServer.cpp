#include <drift/AssetServer.h>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/AudioResource.hpp>
#include <drift/resources/FontResource.hpp>

namespace drift {

AssetServer::AssetServer(RendererResource* renderer, AudioResource* audio, FontResource* font)
    : renderer_(renderer), audio_(audio), font_(font)
{
}

AssetServer::~AssetServer() = default;

TextureHandle AssetServer::loadTexture(const char* path) {
    if (!renderer_) return {};
    return renderer_->loadTexture(path);
}

void AssetServer::destroyTexture(TextureHandle texture) {
    if (renderer_) renderer_->destroyTexture(texture);
}

void AssetServer::getTextureSize(TextureHandle texture, int32_t* w, int32_t* h) const {
    if (renderer_) renderer_->getTextureSize(texture, w, h);
}

SoundHandle AssetServer::loadSound(const char* path) {
    if (!audio_) return {};
    return audio_->loadSound(path);
}

PlayingSoundHandle AssetServer::playSound(SoundHandle sound, float volume, float pan) {
    if (!audio_) return {};
    return audio_->playSound(sound, volume, pan);
}

FontHandle AssetServer::loadFont(const char* path, int sizePx) {
    if (!font_) return {};
    return font_->loadFont(path, sizePx);
}

} // namespace drift
