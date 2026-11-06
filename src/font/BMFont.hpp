#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace drift {

// An AngelCode BMFont description (the text .fnt format).
struct BMFontDesc {
    struct Char {
        int x = 0, y = 0, w = 0, h = 0;
        float xoffset = 0.f, yoffset = 0.f, xadvance = 0.f;
        int page = 0;
    };

    float size = 0.f;          // info size (absolute)
    float lineHeight = 0.f;    // common lineHeight
    float base = 0.f;          // common base: line top to baseline
    std::vector<std::string> pages;                // page files, by id
    std::unordered_map<uint32_t, Char> chars;      // by codepoint
    std::unordered_map<uint64_t, float> kerning;   // (first << 32 | second) -> amount
};

// Parses the text format. Returns false and fills error on failure.
bool parseBMFont(std::string_view text, BMFontDesc& out, std::string& error);

} // namespace drift
