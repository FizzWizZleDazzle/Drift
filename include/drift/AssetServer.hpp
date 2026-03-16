#pragma once

#include <drift/Resource.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>

#include <functional>
#include <type_traits>

namespace drift {

// Asset type tags for load<T>()
struct Texture {};
struct Sound {};
struct Font {};

class AssetServer : public Resource {
public:
    AssetServer();
    ~AssetServer() override;

    DRIFT_RESOURCE(AssetServer)

    // Registration (called by plugins during build)
    void setTextureLoader(std::function<TextureHandle(const char*)> fn);
    void setSoundLoader(std::function<SoundHandle(const char*)> fn);
    void setFontLoader(std::function<FontHandle(const char*, int)> fn);

    // Generic API
    template<typename T, typename... Args>
    auto load(const char* path, Args&&... args) {
        if constexpr (std::is_same_v<T, Texture>) {
            return textureLoader_ ? textureLoader_(path) : TextureHandle{};
        } else if constexpr (std::is_same_v<T, Sound>) {
            return soundLoader_ ? soundLoader_(path) : SoundHandle{};
        } else if constexpr (std::is_same_v<T, Font>) {
            return fontLoader_ ? fontLoader_(path, std::forward<Args>(args)...) : FontHandle{};
        }
    }

    // Named methods
    TextureHandle loadTexture(const char* path);
    SoundHandle loadSound(const char* path);
    FontHandle loadFont(const char* path, int sizePx);

private:
    std::function<TextureHandle(const char*)> textureLoader_;
    std::function<SoundHandle(const char*)> soundLoader_;
    std::function<FontHandle(const char*, int)> fontLoader_;
};

} // namespace drift
