#ifndef DRIFT_ASSET_H
#define DRIFT_ASSET_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

DRIFT_API DriftResult      drift_asset_init(void);
DRIFT_API void             drift_asset_shutdown(void);

DRIFT_API DriftAssetHandle drift_asset_load(const char* path, DriftAssetType type);
DRIFT_API void*            drift_asset_get_data(DriftAssetHandle handle);
DRIFT_API DriftAssetType   drift_asset_get_type(DriftAssetHandle handle);
DRIFT_API void             drift_asset_release(DriftAssetHandle handle);
DRIFT_API bool             drift_asset_is_valid(DriftAssetHandle handle);
DRIFT_API const char*      drift_asset_get_path(DriftAssetHandle handle);

/* Hot reload support */
DRIFT_API void             drift_asset_poll_changes(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIFT_ASSET_H */
