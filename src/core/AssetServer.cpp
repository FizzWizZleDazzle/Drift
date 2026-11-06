#include <drift/AssetServer.hpp>
#include <drift/Log.hpp>

namespace drift {

namespace {
constexpr const char* kTexture = "texture";
constexpr const char* kSound = "sound";
constexpr const char* kFont = "font";
}

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
    if (!path || !textureLoader_) return {};
    if (uint32_t id = find(kTexture, path)) return TextureHandle{id};
    TextureHandle h = textureLoader_(path);
    if (h) track(kTexture, h.id, path);
    return h;
}

SoundHandle AssetServer::loadSound(const char* path) {
    if (!path || !soundLoader_) return {};
    if (uint32_t id = find(kSound, path)) return SoundHandle{id};
    SoundHandle h = soundLoader_(path);
    if (h) track(kSound, h.id, path);
    return h;
}

FontHandle AssetServer::loadFont(const char* path, int sizePx) {
    if (!path || !fontLoader_) return {};
    if (uint32_t id = find(kFont, path, sizePx)) return FontHandle{id};
    FontHandle h = fontLoader_(path, sizePx);
    if (h) track(kFont, h.id, path, sizePx);
    return h;
}

const char* AssetServer::pathOf(TextureHandle h) const { return pathOf(kTexture, h.id); }
const char* AssetServer::pathOf(SoundHandle h) const { return pathOf(kSound, h.id); }
const char* AssetServer::pathOf(FontHandle h) const { return pathOf(kFont, h.id); }

int AssetServer::fontSize(FontHandle h) const {
    const AssetRecord* r = findRecord(kFont, h.id);
    return r ? r->param : 0;
}

void AssetServer::add(const char* path, TextureHandle h) {
    if (path && h) track(kTexture, h.id, path);
}

void AssetServer::track(const char* kind, uint32_t id, const char* path, int param) {
    if (!kind || !path || !id) return;
    for (auto& r : records_) {
        if (r.kind == kind && r.id == id) {
            r.path = path;
            r.param = param;
            return;
        }
    }
    records_.push_back(AssetRecord{kind, id, path, param});
}

void AssetServer::untrack(const char* kind, uint32_t id) {
    if (!kind) return;
    for (auto it = records_.begin(); it != records_.end(); ++it) {
        if (it->kind == kind && it->id == id) {
            records_.erase(it);
            return;
        }
    }
}

uint32_t AssetServer::find(const char* kind, const char* path, int param) const {
    if (!kind || !path) return 0;
    for (const auto& r : records_) {
        if (r.kind == kind && r.path == path && r.param == param) return r.id;
    }
    return 0;
}

const AssetRecord* AssetServer::findRecord(const char* kind, uint32_t id) const {
    if (!kind || !id) return nullptr;
    for (const auto& r : records_) {
        if (r.kind == kind && r.id == id) return &r;
    }
    return nullptr;
}

const char* AssetServer::pathOf(const char* kind, uint32_t id) const {
    const AssetRecord* r = findRecord(kind, id);
    return r ? r->path.c_str() : nullptr;
}

const std::vector<AssetRecord>& AssetServer::records() const {
    return records_;
}

void AssetServer::setReloader(const char* kind, Reloader fn) {
    if (!kind) return;
    for (auto& r : reloaders_) {
        if (r.first == kind) {
            r.second = std::move(fn);
            return;
        }
    }
    reloaders_.emplace_back(kind, std::move(fn));
}

bool AssetServer::canReload(const std::string& kind) const {
    for (const auto& r : reloaders_) {
        if (r.first == kind && r.second) return true;
    }
    return false;
}

bool AssetServer::reload(const char* path) {
    if (!path) return false;
    bool any = false;
    // Copy: a reloader may track or untrack records.
    std::vector<AssetRecord> matches;
    for (const auto& r : records_) {
        if (r.path == path) matches.push_back(r);
    }
    for (const auto& rec : matches) {
        for (const auto& [kind, fn] : reloaders_) {
            if (kind != rec.kind || !fn) continue;
            if (fn(rec.id, rec.path)) {
                DRIFT_LOG_INFO("Reloaded %s '%s'", rec.kind.c_str(), rec.path.c_str());
                any = true;
            } else {
                DRIFT_LOG_WARN("Reloading %s '%s' failed", rec.kind.c_str(), rec.path.c_str());
            }
        }
    }
    return any;
}

} // namespace drift
