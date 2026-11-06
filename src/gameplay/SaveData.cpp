#include <drift/gameplay/SaveData.hpp>
#include <drift/Log.hpp>

#include <SDL3/SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <fstream>
#include <sstream>
#include <system_error>

namespace drift {

namespace {

// On the web the save directory is in Emscripten's in-memory filesystem.
// Persisting it needs that directory mounted as IDBFS at startup and a
// sync after each write; this hook is where the sync goes. The mount and
// the initial load (FS.syncfs(true, ...)) are not wired up yet.
void syncPersistentStorage() {
#ifdef __EMSCRIPTEN__
    EM_ASM(
        if (typeof FS !== 'undefined' && FS.syncfs) {
            FS.syncfs(false, function(err) { if (err) console.error('drift: save sync failed', err); });
        }
    );
#endif
}

} // namespace

SaveData::SaveData(std::string_view org, std::string_view app, std::string_view slot) {
    open(org, app, slot);
}

std::filesystem::path SaveData::userDirectory(std::string_view org, std::string_view app) {
    std::string o(org), a(app);
    char* pref = SDL_GetPrefPath(o.c_str(), a.c_str());
    if (!pref) {
        DRIFT_LOG_ERROR("SaveData: SDL_GetPrefPath failed: %s", SDL_GetError());
        return {};
    }
    std::filesystem::path dir(pref);
    SDL_free(pref);
    return dir;
}

bool SaveData::open(std::string_view org, std::string_view app, std::string_view slot) {
    auto dir = userDirectory(org, app);
    if (dir.empty()) return false;
    return openFile(dir / (std::string(slot) + ".json"));
}

bool SaveData::openFile(const std::filesystem::path& file) {
    path_ = file;
    return load();
}

bool SaveData::load() {
    data_ = nlohmann::json::object();
    dirty_ = false;
    if (path_.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) return true;   // new save
    std::ifstream in(path_, std::ios::binary);
    if (!in) {
        DRIFT_LOG_ERROR("SaveData: cannot read %s", path_.string().c_str());
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    auto parsed = nlohmann::json::parse(ss.str(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        DRIFT_LOG_ERROR("SaveData: %s is not a JSON object; starting empty", path_.string().c_str());
        return false;
    }
    data_ = std::move(parsed);
    return true;
}

bool SaveData::flush() {
    if (path_.empty()) {
        DRIFT_LOG_ERROR("SaveData: flush() before open()");
        return false;
    }
    std::error_code ec;
    if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path(), ec);

    std::filesystem::path tmp = path_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            DRIFT_LOG_ERROR("SaveData: cannot write %s", tmp.string().c_str());
            return false;
        }
        out << data_.dump(2);
        if (!out.good()) {
            DRIFT_LOG_ERROR("SaveData: write failed for %s", tmp.string().c_str());
            return false;
        }
    }
    std::filesystem::rename(tmp, path_, ec);
    if (ec) {
        DRIFT_LOG_ERROR("SaveData: cannot replace %s: %s", path_.string().c_str(), ec.message().c_str());
        return false;
    }
    dirty_ = false;
    syncPersistentStorage();
    return true;
}

void SaveData::erase(std::string_view key) {
    auto it = data_.find(key);
    if (it != data_.end()) {
        data_.erase(it);
        dirty_ = true;
    }
}

void SaveData::clear() {
    data_ = nlohmann::json::object();
    dirty_ = true;
}

} // namespace drift
