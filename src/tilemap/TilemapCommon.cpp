#include "tilemap/TilemapData.hpp"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdlib>

namespace drift {

// ---------------------------------------------------------------------------
// TilemapProperties
// ---------------------------------------------------------------------------

const TilemapProperty* TilemapProperties::find(std::string_view name) const {
    for (const TilemapProperty& p : items) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

std::string TilemapProperties::getString(std::string_view name, std::string_view fallback) const {
    const TilemapProperty* p = find(name);
    return p ? p->text : std::string(fallback);
}

int32_t TilemapProperties::getInt(std::string_view name, int32_t fallback) const {
    const TilemapProperty* p = find(name);
    return p ? static_cast<int32_t>(p->number) : fallback;
}

float TilemapProperties::getFloat(std::string_view name, float fallback) const {
    const TilemapProperty* p = find(name);
    return p ? static_cast<float>(p->number) : fallback;
}

bool TilemapProperties::getBool(std::string_view name, bool fallback) const {
    const TilemapProperty* p = find(name);
    return p ? p->number != 0.0 : fallback;
}

Color TilemapProperties::getColor(std::string_view name, Color fallback) const {
    const TilemapProperty* p = find(name);
    return p ? tilemap::parseColor(p->text, fallback) : fallback;
}

namespace tilemap {

std::string readFile(const std::string& path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return {};
    std::string out(static_cast<const char*>(data), size);
    SDL_free(data);
    return out;
}

std::string directoryOf(const std::string& path) {
    auto sep = path.find_last_of("/\\");
    return sep == std::string::npos ? std::string() : path.substr(0, sep + 1);
}

// "#RRGGBB" or "#AARRGGBB" (Tiled puts alpha first).
Color parseColor(const std::string& text, Color fallback) {
    std::string s = text;
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() != 6 && s.size() != 8) return fallback;
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return fallback;
    }
    auto byte = [&](size_t i) {
        return static_cast<uint8_t>(std::strtoul(s.substr(i, 2).c_str(), nullptr, 16));
    };
    if (s.size() == 6) return Color(byte(0), byte(2), byte(4), 255);
    return Color(byte(2), byte(4), byte(6), byte(0));
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace tilemap
} // namespace drift
