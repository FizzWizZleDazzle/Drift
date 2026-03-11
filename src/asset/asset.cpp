#include "drift/asset.h"
#include "drift/drift.h"

#include <string>
#include <unordered_map>
#include <cstring>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// Internal state -- anonymous namespace for internal linkage
// ---------------------------------------------------------------------------
namespace {

struct AssetEntry {
    std::string    path;
    DriftAssetType type      = DRIFT_ASSET_TEXTURE;
    void          *data      = nullptr;
    int            refcount  = 0;
    time_t         file_mod_time = 0;
};

struct AssetRegistry {
    std::unordered_map<uint32_t, AssetEntry>     entries;    // handle id -> entry
    std::unordered_map<std::string, uint32_t>    path_map;   // path -> handle id
    uint32_t next_id    = 1;
    bool     initialised = false;
};

AssetRegistry g_assets;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static time_t get_file_mod_time(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_mtime;
    }
    return 0;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

DriftResult drift_asset_init(void)
{
    if (g_assets.initialised) {
        DRIFT_LOG_WARN("drift_asset_init: already initialised");
        return DRIFT_OK;
    }

    g_assets.entries.clear();
    g_assets.path_map.clear();
    g_assets.next_id     = 1;
    g_assets.initialised = true;

    DRIFT_LOG_INFO("Asset system initialised");
    return DRIFT_OK;
}

void drift_asset_shutdown(void)
{
    if (!g_assets.initialised) {
        return;
    }

    if (!g_assets.entries.empty()) {
        DRIFT_LOG_WARN("drift_asset_shutdown: %zu asset(s) still loaded, releasing",
                       g_assets.entries.size());
    }

    g_assets.entries.clear();
    g_assets.path_map.clear();
    g_assets.next_id     = 1;
    g_assets.initialised = false;

    DRIFT_LOG_INFO("Asset system shut down");
}

// ---------------------------------------------------------------------------
// Load / query / release
// ---------------------------------------------------------------------------

DriftAssetHandle drift_asset_load(const char *path, DriftAssetType type)
{
    DriftAssetHandle invalid = { DRIFT_INVALID_HANDLE_ID };

    if (!g_assets.initialised) {
        DRIFT_LOG_ERROR("drift_asset_load: asset system not initialised");
        return invalid;
    }

    if (!path || path[0] == '\0') {
        DRIFT_LOG_ERROR("drift_asset_load: path is null or empty");
        return invalid;
    }

    // Check the deduplication map -- if already loaded, bump refcount
    auto path_it = g_assets.path_map.find(path);
    if (path_it != g_assets.path_map.end()) {
        uint32_t existing_id = path_it->second;
        auto entry_it = g_assets.entries.find(existing_id);
        if (entry_it != g_assets.entries.end()) {
            entry_it->second.refcount++;
            DRIFT_LOG_DEBUG("drift_asset_load: reusing '%s' (handle %u, refcount %d)",
                            path, existing_id, entry_it->second.refcount);
            DriftAssetHandle h = { existing_id };
            return h;
        }
    }

    // Allocate a new entry
    uint32_t id = g_assets.next_id++;

    AssetEntry entry;
    entry.path          = path;
    entry.type          = type;
    entry.data          = nullptr; // actual loading done by type-specific subsystem
    entry.refcount      = 1;
    entry.file_mod_time = get_file_mod_time(path);

    g_assets.entries[id]       = std::move(entry);
    g_assets.path_map[path]    = id;

    DRIFT_LOG_DEBUG("drift_asset_load: loaded '%s' as handle %u", path, id);

    DriftAssetHandle h = { id };
    return h;
}

void *drift_asset_get_data(DriftAssetHandle handle)
{
    auto it = g_assets.entries.find(handle.id);
    if (it == g_assets.entries.end()) {
        return nullptr;
    }
    return it->second.data;
}

DriftAssetType drift_asset_get_type(DriftAssetHandle handle)
{
    auto it = g_assets.entries.find(handle.id);
    if (it == g_assets.entries.end()) {
        return DRIFT_ASSET_TEXTURE; // default / safe fallback
    }
    return it->second.type;
}

void drift_asset_release(DriftAssetHandle handle)
{
    if (handle.id == DRIFT_INVALID_HANDLE_ID) {
        return;
    }

    auto it = g_assets.entries.find(handle.id);
    if (it == g_assets.entries.end()) {
        DRIFT_LOG_WARN("drift_asset_release: invalid handle %u", handle.id);
        return;
    }

    it->second.refcount--;

    if (it->second.refcount <= 0) {
        DRIFT_LOG_DEBUG("drift_asset_release: freeing '%s' (handle %u)",
                        it->second.path.c_str(), handle.id);

        // Remove from path dedup map
        g_assets.path_map.erase(it->second.path);

        // Remove from entries
        g_assets.entries.erase(it);
    }
}

bool drift_asset_is_valid(DriftAssetHandle handle)
{
    if (handle.id == DRIFT_INVALID_HANDLE_ID) {
        return false;
    }
    return g_assets.entries.find(handle.id) != g_assets.entries.end();
}

const char *drift_asset_get_path(DriftAssetHandle handle)
{
    auto it = g_assets.entries.find(handle.id);
    if (it == g_assets.entries.end()) {
        return nullptr;
    }
    return it->second.path.c_str();
}

// ---------------------------------------------------------------------------
// Hot reload support (stub)
// ---------------------------------------------------------------------------

void drift_asset_poll_changes(void)
{
    // TODO: iterate entries, compare file_mod_time with current stat,
    //       and trigger reload callbacks for changed assets.
}
