// Data files for Candela games: JSON, TOML and CSV files read into JSON
// that the host turns into values of the type the script asks for, plain
// text files, and the language the player reads.

#include "services.hpp"

#include <drift/App.hpp>
#include <drift/Log.hpp>
#include <drift/Settings.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

using nlohmann::json;

namespace drift::candela {

namespace {

#ifdef __EMSCRIPTEN__
// The browser's preferred language ("en-US"); the caller frees it.
EM_JS(char*, drift_browser_language, (), {
    var l = (typeof navigator !== 'undefined' && (navigator.language || (navigator.languages || [])[0])) || "";
    return stringToNewUTF8(l);
});
#endif

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    // A UTF-8 byte order mark, as spreadsheet programs write.
    if (out.rfind("\xEF\xBB\xBF", 0) == 0) out.erase(0, 3);
    return true;
}

std::string extension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string e = path.substr(dot + 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

// RFC 4180 CSV: fields separated by commas, quoted with "..." where they hold
// commas, quotes ("") or line breaks. The first row names the columns; every
// other row becomes an object of column -> text. Empty lines are skipped.
json csvRows(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false, any = false;
    auto endField = [&] {
        row.push_back(field);
        field.clear();
        any = true;
    };
    auto endRow = [&] {
        if (any || !field.empty()) endField();
        if (!row.empty() && !(row.size() == 1 && row[0].empty())) rows.push_back(row);
        row.clear();
        any = false;
    };
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (quoted) {
            if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            endField();
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            endRow();
        } else {
            field += c;
        }
    }
    endRow();
    json out = json::array();
    if (rows.empty()) return out;
    const auto& header = rows[0];
    for (size_t r = 1; r < rows.size(); ++r) {
        json o = json::object();
        for (size_t c = 0; c < header.size(); ++c) o[header[c]] = c < rows[r].size() ? rows[r][c] : "";
        out.push_back(std::move(o));
    }
    return out;
}

// The file at `path` as JSON text, read as `format` (json, toml, csv or
// text; empty: by the file's extension, text when it has none of those).
// Empty when the file is missing or malformed; the reason is logged, except
// for a missing file when `format` starts with "?" (looking for a file that
// may not be there).
std::string readData(const std::string& path, std::string format) {
    bool optional = !format.empty() && format[0] == '?';
    if (optional) format.erase(0, 1);
    std::string text;
    if (!readFile(path, text)) {
        if (!optional) DRIFT_LOG_ERROR("Candela: cannot read %s", path.c_str());
        return "";
    }
    if (format.empty()) format = extension(path);
    if (format == "json") {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded()) {
            DRIFT_LOG_ERROR("Candela: %s is not valid JSON", path.c_str());
            return "";
        }
        return j.dump();
    }
    if (format == "toml") {
        std::string out, error;
        if (!tomlToJson(text, out, error)) {
            DRIFT_LOG_ERROR("Candela: %s: %s", path.c_str(), error.c_str());
            return "";
        }
        return out;
    }
    if (format == "csv") return csvRows(text).dump();
    return json(text).dump();
}

// "en_US.UTF-8" and "en-us" -> "en-US".
std::string tidyLanguage(std::string l) {
    l = l.substr(0, l.find('.'));
    l = l.substr(0, l.find('@'));
    for (char& c : l) {
        if (c == '_') c = '-';
    }
    size_t dash = l.find('-');
    for (size_t i = 0; i < l.size(); ++i) {
        auto u = static_cast<unsigned char>(l[i]);
        l[i] = static_cast<char>(dash != std::string::npos && i > dash ? std::toupper(u) : std::tolower(u));
    }
    return l == "c" || l == "posix" ? "" : l;
}

std::string systemLanguage() {
#ifdef __EMSCRIPTEN__
    char* l = drift_browser_language();
    std::string out = l ? l : "";
    std::free(l);
    return tidyLanguage(out);
#else
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* v = std::getenv(var);
        if (v && *v) return tidyLanguage(v);
    }
    return "";
#endif
}

} // namespace

bool dataService(App& app, const std::string& f, const json& a, json& out) {
    if (f == "data_read") {
        out = readData(a[0].get<std::string>(), a[1].get<std::string>());
        return true;
    }
    if (f == "system_language") {
        out = systemLanguage();
        return true;
    }
    if (f == "setting_text") {
        out = app.settingsStore().valueJson(a[0].get<std::string>().c_str());
        return true;
    }
    return false;
}

} // namespace drift::candela
