#pragma once

// Persistent key-value save data stored as one JSON file per save slot.
// On desktop the file lives in the per-user directory from SDL_GetPrefPath
// (for example ~/.local/share/<org>/<app>/ on Linux, or <org>\<app> under
// %APPDATA% on Windows). Values are any type nlohmann::json can convert.
//
//   save->set("highscore", 1200);
//   save->flush();
//   int best = save->get("highscore", 0);

#include <drift/Resource.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace drift {

class SaveData : public Resource {
public:
    DRIFT_RESOURCE(SaveData)

    SaveData() = default;
    // Opens slot `slot` in the per-user directory for org/app.
    SaveData(std::string_view org, std::string_view app, std::string_view slot = "save");

    // Use the per-user directory for org/app; loads the slot if it exists.
    bool open(std::string_view org, std::string_view app, std::string_view slot = "save");
    // Use an explicit file path; loads it if it exists. Parent directories
    // are created on flush().
    bool openFile(const std::filesystem::path& file);

    // Re-read the file, discarding unsaved changes. Returns false if the
    // file exists but cannot be parsed (the data is then left empty).
    bool load();
    // Write to disk (to a temporary file, then renamed over the old one,
    // so a crash mid-write keeps the previous save). Returns false on error.
    bool flush();

    template<typename T>
    T get(std::string_view key, T fallback) const {
        auto it = data_.find(key);
        if (it == data_.end()) return fallback;
        try { return it->template get<T>(); } catch (...) { return fallback; }
    }

    template<typename T>
    void set(std::string_view key, T&& value) {
        data_[std::string(key)] = std::forward<T>(value);
        dirty_ = true;
    }

    bool has(std::string_view key) const { return data_.find(key) != data_.end(); }
    void erase(std::string_view key);
    void clear();

    // The whole document, for nested structures.
    nlohmann::json& json() { dirty_ = true; return data_; }
    const nlohmann::json& json() const { return data_; }

    bool dirty() const { return dirty_; }
    const std::filesystem::path& path() const { return path_; }
    bool isOpen() const { return !path_.empty(); }

    // Directory SDL_GetPrefPath gives for org/app (empty on failure).
    static std::filesystem::path userDirectory(std::string_view org, std::string_view app);

private:
    std::filesystem::path path_;
    nlohmann::json data_ = nlohmann::json::object();
    bool dirty_ = false;
};

} // namespace drift
