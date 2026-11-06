#include "gfx/Device.hpp"

namespace drift::gfx {

static std::vector<BackendInfo>& registry() {
    static std::vector<BackendInfo> list;
    return list;
}

void registerBackend(const BackendInfo& info) {
    for (const auto& b : registry()) {
        if (std::string(b.name) == info.name) return;
    }
    registry().push_back(info);
}

const std::vector<BackendInfo>& backends() {
    return registry();
}

} // namespace drift::gfx
