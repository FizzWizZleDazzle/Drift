#include <drift/Settings.hpp>
#include <drift/Log.hpp>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>

namespace drift {

using nlohmann::json;

namespace {

json toJson(const toml::node& node, const std::string& path,
            std::unordered_map<std::string, int>& lines) {
    if (node.source().begin.line) lines[path] = static_cast<int>(node.source().begin.line);
    if (auto* t = node.as_table()) {
        json out = json::object();
        for (const auto& [k, v] : *t) {
            std::string key(k.str());
            out[key] = toJson(v, path.empty() ? key : path + "." + key, lines);
        }
        return out;
    }
    if (auto* a = node.as_array()) {
        json out = json::array();
        for (size_t i = 0; i < a->size(); ++i) {
            out.push_back(toJson(*a->get(i), path, lines));
        }
        return out;
    }
    if (auto* s = node.as_string()) return s->get();
    if (auto* i = node.as_integer()) return i->get();
    if (auto* f = node.as_floating_point()) return f->get();
    if (auto* b = node.as_boolean()) return b->get();
    // Dates and times: their TOML text.
    std::ostringstream os;
    node.visit([&](const auto& v) { os << v; });
    return os.str();
}

// A value written on the command line or in an environment variable: TOML
// syntax when it parses (3, 0.5, true, [0, 9.8], "text"), else the raw text.
json parseValueText(const std::string& text) {
    try {
        toml::table t = toml::parse("v = " + text);
        std::unordered_map<std::string, int> ignored;
        if (const toml::node* v = t.get("v")) return toJson(*v, "", ignored);
    } catch (const toml::parse_error&) {
    }
    return text;
}

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t dot = path.find('.', start);
        parts.push_back(path.substr(start, dot - start));
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return parts;
}

const json* lookup(const json& root, const std::string& path) {
    const json* cur = &root;
    for (const auto& part : splitPath(path)) {
        if (!cur->is_object()) return nullptr;
        auto it = cur->find(part);
        if (it == cur->end()) return nullptr;
        cur = &*it;
    }
    return cur;
}

void setPath(json& root, const std::string& path, json value) {
    json* cur = &root;
    for (const auto& part : splitPath(path)) {
        if (!cur->is_object()) *cur = json::object();
        cur = &(*cur)[part];
    }
    *cur = std::move(value);
}

// DRIFT_<PATH>, upper case, dots as underscores: window.width -> DRIFT_WINDOW_WIDTH.
std::string envName(const std::string& path) {
    std::string name = "DRIFT_";
    for (char c : path) {
        name += c == '.' ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return name;
}

std::optional<json> envValue(const std::string& path) {
    const char* v = SDL_getenv(envName(path).c_str());
    if (!v) return std::nullopt;
    return parseValueText(v);
}

bool isArrayOfNumbers(const json& v, size_t minLen, size_t maxLen) {
    if (!v.is_array() || v.size() < minLen || v.size() > maxLen) return false;
    return std::all_of(v.begin(), v.end(), [](const json& e) { return e.is_number(); });
}

// What a field of this type needs, or nullptr when `v` fits.
const char* mismatch(const reflect::TypeInfo& t, const json& v) {
    using reflect::Kind;
    switch (t.kind) {
    case Kind::Bool: return v.is_boolean() ? nullptr : "true or false";
    case Kind::Int8: case Kind::Int16: case Kind::Int32: case Kind::Int64:
    case Kind::UInt8: case Kind::UInt16: case Kind::UInt32: case Kind::UInt64:
        return v.is_number_integer() ? nullptr : "an integer";
    case Kind::Float: case Kind::Double: return v.is_number() ? nullptr : "a number";
    case Kind::String: case Kind::Chars: return v.is_string() ? nullptr : "a string";
    case Kind::Vec2: return isArrayOfNumbers(v, 2, 2) ? nullptr : "two numbers, [x, y]";
    case Kind::Vec4: case Kind::Rect: return isArrayOfNumbers(v, 4, 4) ? nullptr : "four numbers";
    case Kind::Color: case Kind::ColorF:
        return isArrayOfNumbers(v, 3, 4) ? nullptr : "a color, [r, g, b] or [r, g, b, a]";
    case Kind::Enum: {
        if (!v.is_string()) return "the name of a value";
        std::string s = v.get<std::string>();
        for (const auto& ev : t.enumValues) {
            if (SDL_strcasecmp(ev.name.c_str(), s.c_str()) == 0) return nullptr;
        }
        return "the name of a value";
    }
    case Kind::Struct: return v.is_object() ? nullptr : "a table";
    case Kind::Vector: return v.is_array() ? nullptr : "an array";
    default: return nullptr;
    }
}

std::string enumChoices(const reflect::TypeInfo& t) {
    std::string out;
    for (const auto& ev : t.enumValues) {
        if (!out.empty()) out += ", ";
        out += "\"" + ev.name + "\"";
    }
    return out;
}

} // namespace

struct Settings::Impl {
    struct Binding {
        std::string section;
        const reflect::TypeInfo* type = nullptr;
        void* target = nullptr;
        std::set<std::string> restart;
    };

    std::string file;
    bool fileGiven = false;
    bool isLoaded = false;
    bool reloading = false;   // the file changed; --set keys were reported at start
    json data = json::object();                     // the file
    std::unordered_map<std::string, int> lines;     // "section.key" -> line in the file
    json cli = json::object();                      // --set values
    std::vector<Binding> bindings;
    std::multimap<std::string, std::function<void(const std::vector<std::string>&)>> listeners;

    std::string where(const std::string& path) const {
        auto it = lines.find(path);
        std::string name = file.empty() ? "drift.toml" : file;
        return it == lines.end() ? name : name + ":" + std::to_string(it->second);
    }

    // A command-line or environment value for section.key, if one is set.
    std::optional<json> overrideFor(const std::string& path) const {
        std::optional<json> v = envValue(path);
        if (const json* c = lookup(cli, path)) v = *c;
        return v;
    }

    // Writes one key; false (with a log line) when the value does not fit.
    bool applyKey(const Binding& b, const std::string& key, const json& value,
                  const char* source) {
        for (const auto& f : b.type->fields) {
            if (f.name != key) continue;
            const std::string path = b.section + "." + key;
            if (const char* need = mismatch(*f.type, value)) {
                std::string extra;
                if (f.type->kind == reflect::Kind::Enum) extra = " (" + enumChoices(*f.type) + ")";
                DRIFT_LOG_WARN("%s: %s needs %s%s; keeping %s",
                               source ? source : where(path).c_str(), path.c_str(), need,
                               extra.c_str(), reflect::toJson(*f.type, static_cast<char*>(b.target) + f.offset, {}).dump().c_str());
                return false;
            }
            json v = value;
            if (f.type->kind == reflect::Kind::Enum) {
                // Accept any case: "integer" names ScaleMode::Integer.
                std::string s = v.get<std::string>();
                for (const auto& ev : f.type->enumValues) {
                    if (SDL_strcasecmp(ev.name.c_str(), s.c_str()) == 0) v = ev.name;
                }
            }
            reflect::fromJson(*f.type, static_cast<char*>(b.target) + f.offset, v, {});
            return true;
        }
        return false;   // unknown keys are reported by warnUnknownKeys
    }

    void applyFile(const Binding& b) {
        const json* section = lookup(data, b.section);
        if (!section) return;
        if (!section->is_object()) {
            DRIFT_LOG_WARN("%s: %s should be a table, [%s]", where(b.section).c_str(),
                           b.section.c_str(), b.section.c_str());
            return;
        }
        for (const auto& [key, value] : section->items()) applyKey(b, key, value, nullptr);
    }

    void applyOverrides(const Binding& b) {
        for (const auto& f : b.type->fields) {
            const std::string path = b.section + "." + f.name;
            if (auto v = overrideFor(path)) {
                applyKey(b, f.name, *v, lookup(cli, path) ? "--set" : envName(path).c_str());
            }
        }
    }

    template<typename T, typename Check>
    T read(const char* path, T fallback, Check ok) const {
        std::optional<json> v = overrideFor(path);
        if (!v) {
            if (const json* f = lookup(data, path)) v = *f;
        }
        if (!v || !ok(*v)) return fallback;
        return v->get<T>();
    }

    bool readFile(json& out, std::unordered_map<std::string, int>& outLines) {
        size_t size = 0;
        void* bytes = SDL_LoadFile(file.c_str(), &size);
        if (!bytes) {
            DRIFT_LOG_ERROR("Settings: cannot read '%s': %s", file.c_str(), SDL_GetError());
            return false;
        }
        std::string text(static_cast<const char*>(bytes), size);
        SDL_free(bytes);
        try {
            toml::table t = toml::parse(text, file);
            outLines.clear();
            out = toJson(t, "", outLines);
            return true;
        } catch (const toml::parse_error& e) {
            DRIFT_LOG_ERROR("%s:%u:%u: %s", file.c_str(), unsigned(e.source().begin.line),
                            unsigned(e.source().begin.column), std::string(e.description()).c_str());
            return false;
        }
    }
};

Settings::Settings() : impl_(std::make_unique<Impl>()) {}
Settings::~Settings() = default;

bool Settings::has(const char* path) const {
    return impl_->overrideFor(path) || lookup(impl_->data, path);
}

int64_t Settings::getInt(const char* path, int64_t fallback) const {
    return impl_->read<int64_t>(path, fallback, [](const json& v) { return v.is_number_integer(); });
}

double Settings::getFloat(const char* path, double fallback) const {
    return impl_->read<double>(path, fallback, [](const json& v) { return v.is_number(); });
}

bool Settings::getBool(const char* path, bool fallback) const {
    return impl_->read<bool>(path, fallback, [](const json& v) { return v.is_boolean(); });
}

std::string Settings::getString(const char* path, const std::string& fallback) const {
    return impl_->read<std::string>(path, fallback, [](const json& v) { return v.is_string(); });
}

const std::string& Settings::file() const { return impl_->file; }

void Settings::parseArgs(int argc, char** argv) {
    auto& s = *impl_;
    if (s.isLoaded) {
        DRIFT_LOG_WARN("Settings: command line read after drift.toml was loaded; --config is ignored");
    }
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::string value;
        auto take = [&](const char* flag) {
            std::string f = flag;
            if (arg == f && i + 1 < argc) { value = argv[++i]; return true; }
            if (arg.rfind(f + "=", 0) == 0) { value = arg.substr(f.size() + 1); return true; }
            return false;
        };
        if (take("--config")) {
            if (!s.isLoaded) setFile(value);
        } else if (take("--set")) {
            size_t eq = value.find('=');
            if (eq == std::string::npos || eq == 0) {
                DRIFT_LOG_WARN("Settings: --set needs section.key=value, got '%s'", value.c_str());
                continue;
            }
            setPath(s.cli, value.substr(0, eq), parseValueText(value.substr(eq + 1)));
        }
    }
    if (s.isLoaded) applyOverrides();
}

void Settings::setFile(const std::string& path) {
    impl_->file = path;
    impl_->fileGiven = true;
}

bool Settings::loaded() const { return impl_->isLoaded; }

void Settings::load() {
    auto& s = *impl_;
    if (s.isLoaded) return;
    s.isLoaded = true;

    if (!s.fileGiven) {
        if (const char* env = SDL_getenv("DRIFT_CONFIG")) {
            s.file = env;
            s.fileGiven = true;
        }
    }
    if (!s.fileGiven) {
        // drift.toml in the working directory, else beside the executable.
        std::vector<std::string> candidates = {"drift.toml"};
        if (const char* base = SDL_GetBasePath()) candidates.push_back(std::string(base) + "drift.toml");
        for (const auto& c : candidates) {
            SDL_PathInfo info;
            if (SDL_GetPathInfo(c.c_str(), &info) && info.type == SDL_PATHTYPE_FILE) {
                s.file = c;
                break;
            }
        }
    }

    if (!s.file.empty()) {
        if (s.readFile(s.data, s.lines)) DRIFT_LOG_INFO("Settings: read %s", s.file.c_str());
        else s.data = json::object();
    }
    for (const auto& b : s.bindings) {
        s.applyFile(b);
        s.applyOverrides(b);
    }
}

void Settings::bind(const char* section, const reflect::TypeInfo& type, void* target,
                    SettingsOptions options) {
    auto& s = *impl_;
    for (auto& b : s.bindings) {
        if (b.section != section) continue;
        if (b.target != target) {
            DRIFT_LOG_ERROR("Settings: [%s] is already bound to %s", section, b.type->name.c_str());
            return;
        }
        b.restart.insert(options.restart.begin(), options.restart.end());
        return;
    }
    Impl::Binding b{section, &type, target, {options.restart.begin(), options.restart.end()}};
    s.bindings.push_back(b);
    if (s.isLoaded) {
        s.applyFile(b);
        s.applyOverrides(b);
    }
}

void Settings::applyOverrides() {
    for (const auto& b : impl_->bindings) impl_->applyOverrides(b);
}

void Settings::onChange(const char* section, std::function<void(const std::vector<std::string>&)> fn) {
    impl_->listeners.emplace(section, std::move(fn));
}

void Settings::warnUnknownKeys() const {
    const auto& s = *impl_;
    for (const auto& b : s.bindings) {
        auto known = [&](const std::string& key) {
            return std::any_of(b.type->fields.begin(), b.type->fields.end(),
                               [&](const reflect::Field& f) { return f.name == key; });
        };
        std::string names;
        for (const auto& f : b.type->fields) names += (names.empty() ? "" : ", ") + f.name;
        auto check = [&](const json& root, const char* source) {
            const json* section = lookup(root, b.section);
            if (!section || !section->is_object()) return;
            for (const auto& [key, value] : section->items()) {
                if (known(key)) continue;
                std::string path = b.section + "." + key;
                DRIFT_LOG_WARN("%s: [%s] has no key '%s' (it has %s)",
                               source ? source : s.where(path).c_str(), b.section.c_str(),
                               key.c_str(), names.c_str());
            }
        };
        check(s.data, nullptr);
        if (!s.reloading) check(s.cli, "--set");
    }
}

bool Settings::reload() {
    auto& s = *impl_;
    if (s.file.empty()) return false;
    json next;
    std::unordered_map<std::string, int> nextLines;
    if (!s.readFile(next, nextLines)) return false;
    json prev = std::move(s.data);
    s.data = std::move(next);
    s.lines = std::move(nextLines);

    for (const auto& b : s.bindings) {
        const json* oldSec = lookup(prev, b.section);
        const json* newSec = lookup(s.data, b.section);
        std::set<std::string> keys;
        if (oldSec && oldSec->is_object()) for (const auto& [k, v] : oldSec->items()) keys.insert(k);
        if (newSec && newSec->is_object()) for (const auto& [k, v] : newSec->items()) keys.insert(k);

        std::vector<std::string> changed;
        reflect::Value defaults(*b.type);
        for (const auto& key : keys) {
            const json* o = oldSec && oldSec->is_object() && oldSec->contains(key) ? &(*oldSec)[key] : nullptr;
            const json* n = newSec && newSec->is_object() && newSec->contains(key) ? &(*newSec)[key] : nullptr;
            if (o && n && *o == *n) continue;
            const std::string path = b.section + "." + key;
            if (s.overrideFor(path)) continue;   // pinned by --set or the environment
            auto field = std::find_if(b.type->fields.begin(), b.type->fields.end(),
                                      [&](const reflect::Field& f) { return f.name == key; });
            if (field == b.type->fields.end()) continue;
            if (b.restart.count(key)) {
                DRIFT_LOG_INFO("%s: %s takes effect on restart", s.where(path).c_str(), path.c_str());
                continue;
            }
            if (n) {
                if (!s.applyKey(b, key, *n, nullptr)) continue;
            } else {
                // Removed from the file: back to the default.
                const char* src = static_cast<const char*>(defaults.get()) + field->offset;
                field->type->copy(static_cast<char*>(b.target) + field->offset, src);
            }
            changed.push_back(key);
        }
        if (changed.empty()) continue;
        std::string list;
        for (const auto& k : changed) list += (list.empty() ? "" : ", ") + b.section + "." + k;
        DRIFT_LOG_INFO("Settings: applied %s", list.c_str());
        auto range = s.listeners.equal_range(b.section);
        for (auto it = range.first; it != range.second; ++it) it->second(changed);
    }
    s.reloading = true;
    warnUnknownKeys();
    s.reloading = false;
    return true;
}

} // namespace drift
