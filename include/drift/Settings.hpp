#pragma once

// drift.toml: one file of settings for the whole game. The engine binds its
// own sections ([project], [window], [render], [app]); every plugin binds its
// own the same way, and a game can bind sections of its own or read any value
// by path.
//
//   struct GameRules : Resource {
//       DRIFT_RESOURCE(GameRules)
//       int32_t lives = 3;
//       static void describe(reflect::TypeBuilder<GameRules>& t) {
//           t.field("lives", &GameRules::lives);
//       }
//   };
//   GameRules& rules = app.settings<GameRules>("rules");   // [rules] lives = 5

#include <drift/Resource.hpp>
#include <drift/Reflect.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace drift {

struct SettingsOptions {
    // Keys of the section that only take effect on restart. Live reload
    // logs a change to one of them instead of applying it.
    std::vector<std::string> restart;
};

class Settings : public Resource {
public:
    Settings();
    ~Settings() override;
    DRIFT_RESOURCE(Settings)

    // ---- Reading any value ----
    // `path` is dotted: "rules.lives", "level.spawn.rate". Values set by
    // --set and DRIFT_<SECTION>_<KEY> are seen here too. A missing value or
    // one of another type returns `fallback`.
    bool has(const char* path) const;
    int64_t getInt(const char* path, int64_t fallback = 0) const;
    double getFloat(const char* path, double fallback = 0.0) const;
    bool getBool(const char* path, bool fallback = false) const;
    std::string getString(const char* path, const std::string& fallback = {}) const;
    // A value or a whole table as JSON text, with --set and environment
    // values applied; an empty string when nothing is there. Scripting hosts
    // read sections this way.
    std::string valueJson(const char* path) const;

    // The file that was read, or an empty string when there was none.
    const std::string& file() const;

    // ---- Where values come from ----
    // Read --config <path> and --set section.key=value from the command
    // line. Other arguments are left alone. Call before anything reads a
    // setting (App::args does).
    void parseArgs(int argc, char** argv);
    // Use this file instead of looking for drift.toml.
    void setFile(const std::string& path);
    // Find and read the file, once. Bindings made earlier receive its values.
    void load();
    bool loaded() const;

    // ---- Binding sections to structs ----
    // Copies the section's values into `target` (a struct described by
    // `type`) now if the file is loaded, else when it is, then applies
    // environment and command-line overrides. Binding the same section to
    // the same target again only adds `options`.
    void bind(const char* section, const reflect::TypeInfo& type, void* target,
              SettingsOptions options = {});
    // Applies environment and command-line overrides to every bound section
    // again, so they win over values code set after binding.
    void applyOverrides();
    // Called after a reload changed keys of `section` (their names). Works
    // for sections bound to a struct and for sections read raw.
    void onChange(const char* section, std::function<void(const std::vector<std::string>&)> fn);
    // Logs keys in bound sections that no field reads (typos).
    void warnUnknownKeys() const;

    // ---- Reloading ----
    // Reads the file again and applies the keys whose value changed in it,
    // except keys an override pins. Returns false if the file could not be
    // read or parsed; the previous values stay.
    bool reload();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace drift
