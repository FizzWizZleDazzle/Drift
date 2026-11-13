#include <drift/gameplay/SaveData.hpp>
#include <drift/Log.hpp>

#include <SDL3/SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace drift {

namespace {

#ifdef __EMSCRIPTEN__
// On the web a slot is a localStorage entry named after its path, so it
// outlives the page and needs no file system sync. Reads and writes are
// synchronous: when flush() returns true the save is stored. Storage can be
// missing (a sandboxed iframe, storage blocked by the user) or full; then
// flush() fails and the save lives only as long as the page.

// The stored text of `key`, or null when there is none. The caller frees it.
EM_JS(char*, drift_storage_read, (const char* key), {
    try {
        var v = window.localStorage.getItem(UTF8ToString(key));
        return v === null ? 0 : stringToNewUTF8(v);
    } catch (e) {
        return 0;
    }
});

EM_JS(int, drift_storage_write, (const char* key, const char* text), {
    try {
        window.localStorage.setItem(UTF8ToString(key), UTF8ToString(text));
        return 1;
    } catch (e) {
        console.error('drift: cannot store the save: ' + e);
        return 0;
    }
});

std::string storageKey(const std::filesystem::path& path) { return "drift:" + path.generic_string(); }
#endif

enum class Slot { Missing, Read, Unreadable };

// Reads the stored slot at `path` into `out`.
Slot readSlot(const std::filesystem::path& path, std::string& out) {
#ifdef __EMSCRIPTEN__
    char* text = drift_storage_read(storageKey(path).c_str());
    if (!text) return Slot::Missing;
    out = text;
    std::free(text);
    return Slot::Read;
#else
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return Slot::Missing;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        DRIFT_LOG_ERROR("SaveData: cannot read %s", path.string().c_str());
        return Slot::Unreadable;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return Slot::Read;
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
    std::string text;
    Slot slot = readSlot(path_, text);
    if (slot == Slot::Missing) return true;   // new save
    if (slot == Slot::Unreadable) return false;
    auto parsed = nlohmann::json::parse(text, nullptr, false);
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
#ifdef __EMSCRIPTEN__
    if (!drift_storage_write(storageKey(path_).c_str(), data_.dump(2).c_str())) {
        DRIFT_LOG_ERROR("SaveData: the browser did not store %s", path_.string().c_str());
        return false;
    }
    dirty_ = false;
    return true;
#else
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
    return true;
#endif
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
