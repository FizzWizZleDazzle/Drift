#pragma once

#include <drift/Types.hpp>

#include <typeindex>
#include <unordered_map>
#include <string>
#include <cassert>

namespace drift {

class ComponentRegistry {
public:
#ifndef SWIG
    template<typename T>
    void add(ComponentId id) {
        ids_[std::type_index(typeid(T))] = id;
    }

    template<typename T>
    ComponentId get() const {
        auto it = ids_.find(std::type_index(typeid(T)));
        assert(it != ids_.end() && "Component type not registered in ComponentRegistry");
        return it->second;
    }

    template<typename T>
    bool has() const {
        return ids_.find(std::type_index(typeid(T))) != ids_.end();
    }
#endif

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
