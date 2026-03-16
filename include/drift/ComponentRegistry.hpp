#pragma once

#include <drift/Types.hpp>
#include <drift/Log.hpp>

#include <typeindex>
#include <unordered_map>
#include <string>

namespace drift {

class ComponentRegistry {
public:
    template<typename T>
    void add(ComponentId id) {
        ids_[std::type_index(typeid(T))] = id;
    }

    template<typename T>
    ComponentId get() const {
        auto it = ids_.find(std::type_index(typeid(T)));
        if (it == ids_.end()) {
            DRIFT_LOG_ERROR("Component type not registered in ComponentRegistry: %s",
                            typeid(T).name());
            return 0;
        }
        return it->second;
    }

    template<typename T>
    bool has() const {
        return ids_.find(std::type_index(typeid(T))) != ids_.end();
    }

    void addByName(const char* name, ComponentId id) {
        if (name) idsByName_[name] = id;
    }

    ComponentId getByName(const char* name) const {
        if (!name) return 0;
        auto it = idsByName_.find(name);
        return it != idsByName_.end() ? it->second : 0;
    }

private:
    std::unordered_map<std::type_index, ComponentId> ids_;
    std::unordered_map<std::string, ComponentId> idsByName_;
};

} // namespace drift
