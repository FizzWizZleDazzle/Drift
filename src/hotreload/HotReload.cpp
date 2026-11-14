#include <drift/plugins/HotReloadPlugin.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <filesystem>
#include <system_error>
#endif

#include <algorithm>
#include <cstdlib>

namespace drift {

namespace {

// Files named with filesChanged, waiting for the next poll.
std::vector<std::string>& namedFiles() {
    static std::vector<std::string> files;
    return files;
}

// "./assets/a.png" and "/assets/a.png" -> "assets/a.png": a page names
// files by their path in the game folder, a game loads them by whatever
// path it wrote.
std::string gamePath(std::string p) {
    while (p.rfind("./", 0) == 0) p.erase(0, 2);
    while (!p.empty() && p[0] == '/') p.erase(0, 1);
    return p;
}

void addOnce(std::vector<std::string>& list, const std::string& s) {
    if (std::find(list.begin(), list.end(), s) == list.end()) list.push_back(s);
}

} // namespace

void HotReloadResource::filesChanged(const std::vector<std::string>& paths) {
    auto& files = namedFiles();
    files.insert(files.end(), paths.begin(), paths.end());
}

int HotReloadResource::poll(AssetServer& assets) {
    // Collect first: reloading may add or remove records.
    std::vector<std::string> changed;
    if (!namedFiles().empty()) {
        std::vector<std::string> named;
        named.swap(namedFiles());
        for (std::string& n : named) n = gamePath(n);
        for (const AssetRecord& r : assets.records()) {
            if (!assets.canReload(r.kind)) continue;
            if (std::find(named.begin(), named.end(), gamePath(r.path)) != named.end()) addOnce(changed, r.path);
        }
    }
#ifndef __EMSCRIPTEN__
    for (const AssetRecord& r : assets.records()) {
        if (!assets.canReload(r.kind)) continue;
        std::error_code ec;
        auto t = std::filesystem::last_write_time(r.path, ec);
        if (ec) continue;   // not a file on disk (named in code, or deleted)
        int64_t stamp = static_cast<int64_t>(t.time_since_epoch().count());
        auto [it, inserted] = stamps_.try_emplace(r.path, stamp);
        if (inserted || it->second == stamp) continue;
        it->second = stamp;
        addOnce(changed, r.path);
    }
#endif
    int reloaded = 0;
    for (const auto& path : changed) {
        if (assets.reload(path.c_str())) ++reloaded;
    }
    return reloaded;
}

} // namespace drift

#ifdef __EMSCRIPTEN__
// A page that wrote new files into the file system lists their paths in
// Module.driftChangedFiles and calls drift_files_changed(); the assets
// loaded from them reload at the next frame. Pages built with
// `drift build --web --dev` do this when the game's files change.
EM_JS(char*, drift_take_changed_files, (), {
    var list = Module['driftChangedFiles'] || [];
    Module['driftChangedFiles'] = [];
    return stringToNewUTF8(list.join('\n'));
});

extern "C" EMSCRIPTEN_KEEPALIVE void drift_files_changed() {
    char* text = drift_take_changed_files();
    std::string all = text ? text : "";
    std::free(text);
    std::vector<std::string> paths;
    size_t at = 0;
    while (at < all.size()) {
        size_t end = all.find('\n', at);
        if (end == std::string::npos) end = all.size();
        if (end > at) paths.push_back(all.substr(at, end - at));
        at = end + 1;
    }
    drift::HotReloadResource::filesChanged(paths);
}
#endif
