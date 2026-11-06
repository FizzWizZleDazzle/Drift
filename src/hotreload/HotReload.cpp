#include <drift/plugins/HotReloadPlugin.hpp>

#ifndef __EMSCRIPTEN__
#include <filesystem>
#include <system_error>
#endif

namespace drift {

int HotReloadResource::poll(AssetServer& assets) {
#ifdef __EMSCRIPTEN__
    (void)assets;
    return 0;
#else
    // Collect first: reloading may add or remove records.
    std::vector<std::string> changed;
    for (const AssetRecord& r : assets.records()) {
        if (!assets.canReload(r.kind)) continue;
        std::error_code ec;
        auto t = std::filesystem::last_write_time(r.path, ec);
        if (ec) continue;   // not a file on disk (named in code, or deleted)
        int64_t stamp = static_cast<int64_t>(t.time_since_epoch().count());
        auto [it, inserted] = stamps_.try_emplace(r.path, stamp);
        if (inserted || it->second == stamp) continue;
        it->second = stamp;
        changed.push_back(r.path);
    }
    int reloaded = 0;
    for (const auto& path : changed) {
        if (assets.reload(path.c_str())) ++reloaded;
    }
    return reloaded;
#endif
}

} // namespace drift
