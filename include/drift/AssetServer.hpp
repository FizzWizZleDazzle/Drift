#pragma once

#include <drift/Resource.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>

#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace drift {

// Asset type tags for load<T>()
struct Texture {};
struct Sound {};
struct Font {};

// One asset loaded from a file. `kind` names the asset type ("texture",
// "sound", "font", or any kind a plugin tracks); `param` is extra load
// data such as a font's pixel size.
struct AssetRecord {
    std::string kind;
    uint32_t id = 0;
    std::string path;
    int param = 0;
};

class AssetServer : public Resource {
public:
    AssetServer();
    ~AssetServer() override;

    DRIFT_RESOURCE(AssetServer)
    bool mainThreadOnly() const override { return true; }

    // Registration (called by plugins during build)
    void setTextureLoader(std::function<TextureHandle(const char*)> fn);
    void setSoundLoader(std::function<SoundHandle(const char*)> fn);
    void setFontLoader(std::function<FontHandle(const char*, int)> fn);

    // Generic API
    template<typename T, typename... Args>
    auto load(const char* path, Args&&... args) {
        if constexpr (std::is_same_v<T, Texture>) {
            return loadTexture(path);
        } else if constexpr (std::is_same_v<T, Sound>) {
            return loadSound(path);
        } else if constexpr (std::is_same_v<T, Font>) {
            return loadFont(path, std::forward<Args>(args)...);
        }
    }

    // Named methods. Loading the same path again returns the same handle.
    TextureHandle loadTexture(const char* path);
    SoundHandle loadSound(const char* path);
    FontHandle loadFont(const char* path, int sizePx);

    // The file an asset was loaded from; nullptr for assets made in code.
    const char* pathOf(TextureHandle h) const;
    const char* pathOf(SoundHandle h) const;
    const char* pathOf(FontHandle h) const;
    int fontSize(FontHandle h) const;

    // Name an asset made in code so scenes and lookups can refer to it.
    // The name does not need to be a real file.
    void add(const char* path, TextureHandle h);

    // ---- Any asset kind ----
    // Plugins with their own asset types (scenes, for example) record what
    // they load here so tools and hot reload see every asset in one place.
    void track(const char* kind, uint32_t id, const char* path, int param = 0);
    void untrack(const char* kind, uint32_t id);
    uint32_t find(const char* kind, const char* path, int param = 0) const;   // 0 if not loaded
    const char* pathOf(const char* kind, uint32_t id) const;
    const std::vector<AssetRecord>& records() const;

    // Reloaders refresh an asset in place from its file, keeping its handle.
    using Reloader = std::function<bool(uint32_t id, const std::string& path)>;
    void setReloader(const char* kind, Reloader fn);
    bool canReload(const std::string& kind) const;
    // Reload every asset loaded from `path`; true if any reloaded.
    bool reload(const char* path);

private:
    const AssetRecord* findRecord(const char* kind, uint32_t id) const;

    std::function<TextureHandle(const char*)> textureLoader_;
    std::function<SoundHandle(const char*)> soundLoader_;
    std::function<FontHandle(const char*, int)> fontLoader_;

    std::vector<AssetRecord> records_;
    std::vector<std::pair<std::string, Reloader>> reloaders_;
};

} // namespace drift
