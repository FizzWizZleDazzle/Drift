#include <drift/AssetServer.h>

namespace drift {

AssetServer::AssetServer() = default;
AssetServer::~AssetServer() = default;

void AssetServer::setTextureLoader(std::function<TextureHandle(const char*)> fn) {
    textureLoader_ = std::move(fn);
}

void AssetServer::setSoundLoader(std::function<SoundHandle(const char*)> fn) {
    soundLoader_ = std::move(fn);
}

void AssetServer::setFontLoader(std::function<FontHandle(const char*, int)> fn) {
    fontLoader_ = std::move(fn);
}

TextureHandle AssetServer::loadTexture(const char* path) {
    return textureLoader_ ? textureLoader_(path) : TextureHandle{};
}

SoundHandle AssetServer::loadSound(const char* path) {
    return soundLoader_ ? soundLoader_(path) : SoundHandle{};
}

FontHandle AssetServer::loadFont(const char* path, int sizePx) {
    return fontLoader_ ? fontLoader_(path, sizePx) : FontHandle{};
}

} // namespace drift
