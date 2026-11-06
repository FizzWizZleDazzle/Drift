#include "font/BMFont.hpp"

#include <cmath>
#include <cstdlib>

namespace drift {

namespace {

// One "key=value" pair; values may be quoted.
struct Pair { std::string_view key, value; };

std::vector<Pair> splitPairs(std::string_view line) {
    std::vector<Pair> out;
    size_t i = 0, n = line.size();
    while (i < n) {
        while (i < n && (line[i] == ' ' || line[i] == '\t')) ++i;
        size_t ks = i;
        while (i < n && line[i] != '=' && line[i] != ' ' && line[i] != '\t') ++i;
        std::string_view key = line.substr(ks, i - ks);
        if (i >= n || line[i] != '=') {
            if (!key.empty()) out.push_back({key, {}});
            continue;
        }
        ++i;  // '='
        std::string_view value;
        if (i < n && line[i] == '"') {
            size_t vs = ++i;
            while (i < n && line[i] != '"') ++i;
            value = line.substr(vs, i - vs);
            if (i < n) ++i;
        } else {
            size_t vs = i;
            while (i < n && line[i] != ' ' && line[i] != '\t') ++i;
            value = line.substr(vs, i - vs);
        }
        out.push_back({key, value});
    }
    return out;
}

float num(std::string_view v) {
    std::string s(v);
    return static_cast<float>(std::strtod(s.c_str(), nullptr));
}

} // namespace

bool parseBMFont(std::string_view text, BMFontDesc& out, std::string& error) {
    if (text.size() >= 3 && text.substr(0, 3) == "BMF") {
        error = "binary BMFont files are not supported; export the text format";
        return false;
    }
    if (text.find("<?xml") != std::string_view::npos || text.find("<font>") != std::string_view::npos) {
        error = "XML BMFont files are not supported; export the text format";
        return false;
    }

    bool sawCommon = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        auto pairs = splitPairs(line);
        if (pairs.empty()) continue;
        std::string_view tag = pairs[0].key;

        if (tag == "info") {
            for (auto& p : pairs) if (p.key == "size") out.size = std::fabs(num(p.value));
        } else if (tag == "common") {
            sawCommon = true;
            for (auto& p : pairs) {
                if (p.key == "lineHeight") out.lineHeight = num(p.value);
                else if (p.key == "base") out.base = num(p.value);
            }
        } else if (tag == "page") {
            int id = -1;
            std::string_view file;
            for (auto& p : pairs) {
                if (p.key == "id") id = static_cast<int>(num(p.value));
                else if (p.key == "file") file = p.value;
            }
            if (id < 0 || id > 255 || file.empty()) {
                error = "bad page line";
                return false;
            }
            if (static_cast<int>(out.pages.size()) <= id) out.pages.resize(id + 1);
            out.pages[id] = std::string(file);
        } else if (tag == "char") {
            BMFontDesc::Char c;
            long id = -1;
            for (auto& p : pairs) {
                if (p.key == "id") id = static_cast<long>(num(p.value));
                else if (p.key == "x") c.x = static_cast<int>(num(p.value));
                else if (p.key == "y") c.y = static_cast<int>(num(p.value));
                else if (p.key == "width") c.w = static_cast<int>(num(p.value));
                else if (p.key == "height") c.h = static_cast<int>(num(p.value));
                else if (p.key == "xoffset") c.xoffset = num(p.value);
                else if (p.key == "yoffset") c.yoffset = num(p.value);
                else if (p.key == "xadvance") c.xadvance = num(p.value);
                else if (p.key == "page") c.page = static_cast<int>(num(p.value));
            }
            if (id >= 0) out.chars[static_cast<uint32_t>(id)] = c;
        } else if (tag == "kerning") {
            long first = -1, second = -1;
            float amount = 0.f;
            for (auto& p : pairs) {
                if (p.key == "first") first = static_cast<long>(num(p.value));
                else if (p.key == "second") second = static_cast<long>(num(p.value));
                else if (p.key == "amount") amount = num(p.value);
            }
            if (first >= 0 && second >= 0) {
                out.kerning[(static_cast<uint64_t>(first) << 32) | static_cast<uint32_t>(second)] = amount;
            }
        }
    }

    if (!sawCommon || out.lineHeight <= 0.f) {
        error = "missing 'common lineHeight'";
        return false;
    }
    if (out.pages.empty()) {
        error = "no pages";
        return false;
    }
    if (out.size <= 0.f) out.size = out.lineHeight;
    return true;
}

} // namespace drift
