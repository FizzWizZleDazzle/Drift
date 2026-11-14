#include "web_pack.hpp"

#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace drift::web {

namespace {

struct Entry {
    std::string path;   // in the pack: relative to the game folder, '/' separated
    fs::path source;
    std::string data;   // the bytes, for a file made here rather than read
    bool inMemory = false;
};

struct Project {
    std::string entry = "main.cdl";
    std::string assets = "assets";
    std::string name;
    std::string description;
    std::string icon;
};

// [web]: the page around the game. drift_web.js reads the touch and
// orientation part as window.DRIFT_PAGE.
struct WebPage {
    std::string orientation;           // "", "landscape" or "portrait"
    std::string background = "#000000";
    bool fullscreenButton = false;
    bool offline = false;              // installable, plays without a connection
    std::string touchControls = "auto";
    std::vector<std::string> stick;    // KeyboardEvent codes: up, left, down, right
    nlohmann::json buttons = nlohmann::json::array();   // [{"code", "label"}]
};

// Drift's key names (the drift module's Key enum) as KeyboardEvent codes.
std::string keyCode(const std::string& name) {
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') return "Key" + name;
    if (name.size() == 4 && name.rfind("Num", 0) == 0 && name[3] >= '0' && name[3] <= '9') return "Digit" + name.substr(3);
    if (name.size() >= 2 && name[0] == 'F' && std::all_of(name.begin() + 1, name.end(), ::isdigit)) {
        int n = std::stoi(name.substr(1));
        return n >= 1 && n <= 12 ? name : "";
    }
    static const std::pair<const char*, const char*> named[] = {
        {"Return", "Enter"}, {"Escape", "Escape"}, {"Backspace", "Backspace"}, {"Tab", "Tab"},
        {"Space", "Space"}, {"Right", "ArrowRight"}, {"Left", "ArrowLeft"}, {"Down", "ArrowDown"},
        {"Up", "ArrowUp"}, {"LCtrl", "ControlLeft"}, {"LShift", "ShiftLeft"}, {"LAlt", "AltLeft"},
        {"RCtrl", "ControlRight"}, {"RShift", "ShiftRight"}, {"RAlt", "AltRight"}};
    for (const auto& [from, to] : named) {
        if (name == from) return to;
    }
    return "";
}

bool readFile(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

bool writeFile(const fs::path& path, const std::string& data) {
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) return false;
    o.write(data.data(), std::streamsize(data.size()));
    return bool(o);
}

bool hidden(const fs::path& name) { return name.string().rfind('.', 0) == 0; }

// Build output folders: build and build-<anything>.
bool buildOutput(const fs::path& name) {
    std::string n = name.string();
    return n == "build" || n.rfind("build-", 0) == 0;
}

bool readProject(const fs::path& file, Project& p, WebPage& web, std::string& error) {
    if (!fs::is_regular_file(file)) return true;
    toml::table t;
    try {
        t = toml::parse_file(file.string());
    } catch (const toml::parse_error& e) {
        error = file.string() + ": " + std::string(e.description());
        return false;
    }
    if (auto v = t["project"]["entry"].value<std::string>()) p.entry = *v;
    if (auto v = t["project"]["assets"].value<std::string>()) p.assets = *v;
    if (auto v = t["project"]["name"].value<std::string>()) p.name = *v;
    if (auto v = t["project"]["description"].value<std::string>()) p.description = *v;
    if (auto v = t["project"]["icon"].value<std::string>()) p.icon = *v;

    auto w = t["web"];
    auto bad = [&](const std::string& key, const std::string& why) {
        error = file.string() + ": [web] " + key + ": " + why;
        return false;
    };
    if (auto v = w["orientation"].value<std::string>()) {
        if (*v != "any" && *v != "landscape" && *v != "portrait") return bad("orientation", "takes \"any\", \"landscape\" or \"portrait\"");
        web.orientation = *v == "any" ? "" : *v;
    }
    if (auto v = w["background"].value<std::string>()) {
        bool hex = v->size() == 7 && (*v)[0] == '#' && std::all_of(v->begin() + 1, v->end(), ::isxdigit);
        if (!hex) return bad("background", "takes a color as \"#rrggbb\"");
        web.background = *v;
    }
    if (auto v = w["fullscreen_button"].value<bool>()) web.fullscreenButton = *v;
    if (auto v = w["offline"].value<bool>()) web.offline = *v;
    if (auto v = w["touch_controls"].value<std::string>()) {
        if (*v != "auto" && *v != "always" && *v != "never") return bad("touch_controls", "takes \"auto\", \"always\" or \"never\"");
        web.touchControls = *v;
    }
    if (auto v = w["touch_stick"].value<std::string>()) {
        if (*v == "arrows") web.stick = {"ArrowUp", "ArrowLeft", "ArrowDown", "ArrowRight"};
        else if (*v == "wasd") web.stick = {"KeyW", "KeyA", "KeyS", "KeyD"};
        else if (!v->empty()) return bad("touch_stick", "takes \"arrows\", \"wasd\" or \"\"");
    }
    if (auto* list = w["touch_buttons"].as_array()) {
        for (const auto& item : *list) {
            std::string key, label;
            if (auto s = item.value<std::string>()) {
                key = *s;
            } else if (auto* tbl = item.as_table()) {
                key = (*tbl)["key"].value_or(std::string());
                label = (*tbl)["label"].value_or(std::string());
            }
            std::string code = keyCode(key);
            if (code.empty()) return bad("touch_buttons", "\"" + key + "\" is not a key name (Space, Z, Return, Up, ...)");
            web.buttons.push_back({{"code", code}, {"label", label.empty() ? key : label}});
        }
    }
    return true;
}

std::string escapeHtml(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// JSON safe inside a <script> element, in ASCII.
std::string scriptJson(const nlohmann::json& j) {
    std::string s = j.dump(-1, ' ', true);
    for (size_t at = s.find("</"); at != std::string::npos; at = s.find("</", at + 2)) s.replace(at, 2, "<\\/");
    return s;
}

// "<width>x<height>" of a PNG image, from its header; "any" for other images.
std::string imageSizes(const std::string& data) {
    if (data.size() < 24 || data.compare(1, 3, "PNG") != 0) return "any";
    auto u32 = [&](size_t i) {
        return uint32_t(uint8_t(data[i])) << 24 | uint32_t(uint8_t(data[i + 1])) << 16 |
               uint32_t(uint8_t(data[i + 2])) << 8 | uint32_t(uint8_t(data[i + 3]));
    };
    return std::to_string(u32(16)) + "x" + std::to_string(u32(20));
}

uint64_t fnv1a(uint64_t h, const std::string& data) {
    for (unsigned char c : data) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

// The service worker of an offline page: on install it stores every file of
// the page in a cache named after `version`, then answers every request it
// can from that cache, so the game starts without a connection. A new build
// has a new version; its worker replaces the old one and its cache.
std::string serviceWorker(const std::string& version, const std::vector<std::string>& files) {
    std::string list = scriptJson(files);
    return "// Written by `drift build --web` for [web] offline = true.\n"
           "// Caches are per page, so games on one site keep their own.\n"
           "const PAGE = 'drift ' + self.registration.scope + ' ';\n"
           "const CACHE = PAGE + '" + version + "';\n"
           "const FILES = " + list + ";\n"
           "self.addEventListener('install', e => {\n"
           "  e.waitUntil(caches.open(CACHE).then(c => c.addAll(FILES)).then(() => self.skipWaiting()));\n"
           "});\n"
           "self.addEventListener('activate', e => {\n"
           "  e.waitUntil(caches.keys()\n"
           "    .then(keys => Promise.all(keys.filter(k => k.startsWith(PAGE) && k !== CACHE).map(k => caches.delete(k))))\n"
           "    .then(() => self.clients.claim()));\n"
           "});\n"
           "self.addEventListener('fetch', e => {\n"
           "  if (e.request.method !== 'GET') return;\n"
           "  e.respondWith(caches.open(CACHE)\n"
           "    .then(c => c.match(e.request, {ignoreSearch: true}))\n"
           "    .then(hit => hit || fetch(e.request)));\n"
           "});\n";
}

// Takes the place of an offline page's worker when the page is built
// without `offline`.
std::string retiredServiceWorker() {
    return "// Written by `drift build --web`: this page is no longer offline.\n"
           "const PAGE = 'drift ' + self.registration.scope + ' ';\n"
           "self.addEventListener('install', () => self.skipWaiting());\n"
           "self.addEventListener('activate', e => {\n"
           "  e.waitUntil(caches.keys()\n"
           "    .then(keys => Promise.all(keys.filter(k => k.startsWith(PAGE)).map(k => caches.delete(k))))\n"
           "    .then(() => self.registration.unregister()));\n"
           "});\n";
}

void putU32(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out += char((v >> (8 * i)) & 0xff);
}

bool sameFolder(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return fs::equivalent(a, b, ec);
}

} // namespace

bool buildWebPage(const std::string& folderArg, const std::string& outArg, const std::string& runtimeArg,
                  bool dev, std::string& error, std::string& summary, const CompileGame& compile) {
    std::error_code ec;
    fs::path folder = fs::absolute(folderArg, ec);
    if (!fs::is_directory(folder)) {
        error = folderArg + ": not a folder";
        return false;
    }
    fs::path out = fs::absolute(outArg, ec);
    fs::path runtime = fs::absolute(runtimeArg, ec);
    for (const char* name : {"drift.html", "drift.js", "drift.wasm"}) {
        if (!fs::is_regular_file(runtime / name)) {
            error = "no web runtime at " + runtime.string() + " (it needs drift.html, drift.js and drift.wasm)";
            return false;
        }
    }
    if (dev && !fs::is_regular_file(runtime / "drift-dev.js")) {
        error = "the web runtime at " + runtime.string() + " has no drift-dev.js, which dev pages need; it is older than hot reload";
        return false;
    }

    Project project;
    WebPage web;
    if (!readProject(folder / "drift.toml", project, web, error)) return false;
    fs::path assets = fs::path(project.assets).lexically_normal();
    if (!assets.has_filename()) assets = assets.parent_path();   // "assets/"

    std::vector<Entry> files;
    if (fs::is_regular_file(folder / "drift.toml")) files.push_back({"drift.toml", folder / "drift.toml"});

    // Every script, at its path in the folder. Hidden folders, build output,
    // the page being written and the asset folder (packed whole below) are
    // skipped.
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::follow_directory_symlink, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path& p = it->path();
        fs::path rel = p.lexically_relative(folder);
        if (it->is_directory()) {
            if (hidden(p.filename()) || buildOutput(p.filename()) || rel == assets || sameFolder(p, out)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (it->is_regular_file() && p.extension() == ".cdl" && !hidden(p.filename())) {
            files.push_back({rel.generic_string(), p});
        }
    }
    if (ec) {
        error = folder.string() + ": " + ec.message();
        return false;
    }
    if (std::none_of(files.begin(), files.end(), [&](const Entry& e) {
            return e.path == fs::path(project.entry).lexically_normal().generic_string();
        })) {
        error = (folder / project.entry).string() + ": no such script; drift.toml [project] entry names the game's first script";
        return false;
    }

    // The asset folder, everything in it but hidden files.
    size_t assetCount = 0;
    if (fs::is_directory(folder / assets)) {
        for (auto it = fs::recursive_directory_iterator(folder / assets, fs::directory_options::follow_directory_symlink, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const fs::path& p = it->path();
            if (hidden(p.filename())) {
                if (it->is_directory()) it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file()) {
                files.push_back({p.lexically_relative(folder).generic_string(), p});
                ++assetCount;
            }
        }
        if (ec) {
            error = (folder / assets).string() + ": " + ec.message();
            return false;
        }
    }
    // The game compiled to bytecode, which the page starts from.
    std::string compiled, notCompiled;
    if (!dev && compile) {
        std::string bytes, why;
        if (compile(folder.string(), project.entry, bytes, why)) {
            Entry built;
            built.path = fs::path(project.entry).lexically_normal().generic_string() + "b";
            built.data = std::move(bytes);
            built.inMemory = true;
            files.push_back(std::move(built));
            compiled = files.back().path;
        } else {
            notCompiled = why.substr(0, why.find('\n'));
        }
    }
    std::sort(files.begin(), files.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });

    // The pack: header, index, then every file's bytes.
    nlohmann::json index = nlohmann::json::array();
    std::string blob;
    for (const Entry& f : files) {
        std::string data = f.data;
        if (!f.inMemory && !readFile(f.source, data)) {
            error = f.source.string() + ": cannot read";
            return false;
        }
        index.push_back({{"path", f.path}, {"offset", blob.size()}, {"size", data.size()}});
        blob += data;
    }
    std::string indexText = nlohmann::json{{"files", index}}.dump();
    std::string pack = "DRIFTPAK";
    putU32(pack, 1);
    putU32(pack, uint32_t(indexText.size()));
    pack += indexText;
    pack += blob;

    fs::create_directories(out, ec);
    if (!fs::is_directory(out)) {
        error = out.string() + ": cannot make the folder";
        return false;
    }
    if (!writeFile(out / "game.pack", pack)) {
        error = (out / "game.pack").string() + ": cannot write";
        return false;
    }
    for (const char* name : {"drift.js", "drift.wasm"}) {
        fs::copy_file(runtime / name, out / name, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = (out / name).string() + ": " + ec.message();
            return false;
        }
    }
    std::string page;
    if (!readFile(runtime / "drift.html", page)) {
        error = (runtime / "drift.html").string() + ": cannot read";
        return false;
    }
    const std::string title = "<title>Drift</title>";
    if (!project.name.empty()) {
        size_t at = page.find(title);
        if (at != std::string::npos) page.replace(at, title.size(), "<title>" + escapeHtml(project.name) + "</title>");
    }

    // What the page's <head> says about the game, from [project] and [web].
    // The runtime and the game start downloading while the browser is still
    // reading drift.js, which asks for them only once it runs. A dev page
    // asks for the pack in other ways too, which a preload would only warn
    // about.
    std::string head = "<link rel=\"preload\" href=\"drift.wasm\" as=\"fetch\" type=\"application/wasm\" crossorigin>\n";
    if (!dev) head += "<link rel=\"preload\" href=\"game.pack\" as=\"fetch\" crossorigin>\n";
    std::vector<std::string> extras;   // files written beside index.html
    if (!project.description.empty()) {
        head += "<meta name=\"description\" content=\"" + escapeHtml(project.description) + "\">\n";
        head += "<meta property=\"og:description\" content=\"" + escapeHtml(project.description) + "\">\n";
    }
    if (!project.name.empty()) head += "<meta property=\"og:title\" content=\"" + escapeHtml(project.name) + "\">\n";
    head += "<meta property=\"og:type\" content=\"website\">\n";
    head += "<meta name=\"theme-color\" content=\"" + web.background + "\">\n";
    head += "<style>:root { --drift-background: " + web.background + "; }</style>\n";

    std::string iconName, iconType, iconSizes;
    if (!project.icon.empty()) {
        fs::path icon = folder / project.icon;
        std::string data;
        if (!readFile(icon, data)) {
            error = icon.string() + ": cannot read; drift.toml [project] icon names the page's icon";
            return false;
        }
        std::string ext = icon.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        iconType = ext == ".png" ? "image/png" : ext == ".svg" ? "image/svg+xml" : ext == ".ico" ? "image/x-icon"
                 : ext == ".webp" ? "image/webp" : (ext == ".jpg" || ext == ".jpeg") ? "image/jpeg" : "";
        if (iconType.empty()) {
            error = icon.string() + ": the page icon must be .png, .svg, .ico, .webp or .jpg";
            return false;
        }
        iconSizes = imageSizes(data);
        iconName = "icon" + ext;
        if (!writeFile(out / iconName, data)) {
            error = (out / iconName).string() + ": cannot write";
            return false;
        }
        extras.push_back(iconName);
        head += "<link rel=\"icon\" href=\"" + iconName + "\" type=\"" + iconType + "\">\n";
        head += "<link rel=\"apple-touch-icon\" href=\"" + iconName + "\">\n";
    }

    nlohmann::json config = {{"orientation", web.orientation},
                             {"fullscreen_button", web.fullscreenButton},
                             {"touch", {{"show", web.touchControls}, {"stick", web.stick}, {"buttons", web.buttons}}},
                             {"sizes", {{"drift.wasm", fs::file_size(out / "drift.wasm", ec)},
                                        {"game.pack", pack.size()}}}};
    head += "<script>window.DRIFT_PAGE = " + scriptJson(config) + ";</script>\n";

    // A dev page hot reloads; it must not be served from an offline copy.
    if (dev) {
        fs::copy_file(runtime / "drift-dev.js", out / "drift-dev.js", fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = (out / "drift-dev.js").string() + ": " + ec.message();
            return false;
        }
        extras.push_back("drift-dev.js");
        head += "<script src=\"drift-dev.js\" defer></script>\n";
        web.offline = false;
    } else {
        fs::remove(out / "drift-dev.js", ec);
    }

    if (web.offline) {
        std::string name = project.name.empty() ? "Drift" : project.name;
        nlohmann::json icons = nlohmann::json::array();
        if (!iconName.empty()) {
            icons.push_back({{"src", iconName}, {"type", iconType},
                             {"sizes", iconSizes}});
        }
        nlohmann::json manifest = {{"name", name}, {"short_name", name}, {"start_url", "./"}, {"scope", "./"},
                                   {"display", "fullscreen"}, {"background_color", web.background},
                                   {"theme_color", web.background}, {"icons", icons}};
        if (!project.description.empty()) manifest["description"] = project.description;
        if (!web.orientation.empty()) manifest["orientation"] = web.orientation;
        if (!writeFile(out / "manifest.webmanifest", manifest.dump(2) + "\n")) {
            error = (out / "manifest.webmanifest").string() + ": cannot write";
            return false;
        }
        // The worker keeps a copy of every file of the page under a name
        // that changes with them, so a new build replaces the old copy.
        std::vector<std::string> cached = {"./", "index.html", "drift.js", "drift.wasm", "game.pack",
                                           "manifest.webmanifest"};
        cached.insert(cached.end(), extras.begin(), extras.end());
        uint64_t version = 1469598103934665603ull;
        for (const std::string& name : {std::string("game.pack"), std::string("drift.js"), std::string("drift.wasm")}) {
            std::string data;
            readFile(out / name, data);
            version = fnv1a(version, data);
        }
        version = fnv1a(version, page + head);
        char tag[17];
        std::snprintf(tag, sizeof(tag), "%016llx", static_cast<unsigned long long>(version));
        if (!writeFile(out / "sw.js", serviceWorker(tag, cached))) {
            error = (out / "sw.js").string() + ": cannot write";
            return false;
        }
        head += "<link rel=\"manifest\" href=\"manifest.webmanifest\">\n";
        head += "<script>if ('serviceWorker' in navigator && location.protocol !== 'file:') "
                "navigator.serviceWorker.register('sw.js');</script>\n";
        extras.push_back("manifest.webmanifest");
        extras.push_back("sw.js");
    } else if (fs::exists(out / "sw.js")) {
        // This folder was an offline page before. Players who opened it
        // still run its worker, which would keep serving the old copy; this
        // one takes its place, drops the copy and unregisters itself.
        fs::remove(out / "manifest.webmanifest", ec);
        if (!writeFile(out / "sw.js", retiredServiceWorker())) {
            error = (out / "sw.js").string() + ": cannot write";
            return false;
        }
        extras.push_back("sw.js");
    }

    size_t at = page.find("</head>");
    if (at == std::string::npos) {
        error = (runtime / "drift.html").string() + ": no </head>";
        return false;
    }
    page.insert(at, head);
    if (!writeFile(out / "index.html", page)) {
        error = (out / "index.html").string() + ": cannot write";
        return false;
    }

    std::string written = "index.html, drift.js, drift.wasm, game.pack";
    for (const std::string& e : extras) written += ", " + e;
    size_t scripts = files.size() - assetCount - (compiled.empty() ? 0 : 1);
    summary = out.string() + ": " + written + " (" + std::to_string(scripts) + " scripts and settings, " +
              std::to_string(assetCount) + " assets" + (compiled.empty() ? "" : ", compiled") + ")";
    if (!notCompiled.empty()) summary += "\nThe page compiles the scripts when it loads: " + notCompiled;
    return true;
}

} // namespace drift::web
