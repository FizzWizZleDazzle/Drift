#pragma once

#include <drift/Types.hpp>
#include <drift/Resource.hpp>

#include <vector>
#include <functional>
#include <typeindex>

namespace drift {

class App;
class Commands;

// Forward declarations for Query types
template<typename... Args> class Query;
template<typename... Args> class QueryMut;

// Access mode for dependency tracking.
enum class AccessMode { Read, Write };

// Describes one resource dependency of a system.
struct AccessDescriptor {
    std::type_index typeId;
    AccessMode mode;

    AccessDescriptor(std::type_index t, AccessMode m) : typeId(t), mode(m) {}
};

// Base class for systems that can be registered from C# via SWIG.
class SystemBase {
public:
    virtual ~SystemBase() = default;
    virtual void execute(App& app, float dt) = 0;
    virtual std::vector<AccessDescriptor> getDependencies() const { return {}; }
};

#ifndef SWIG

// ---- Template machinery for extracting resource deps from function signatures ----

// Type trait: is this a Res<T>?
template<typename T> struct IsRes : std::false_type {};
template<typename T> struct IsRes<Res<T>> : std::true_type {};

// Type trait: is this a ResMut<T>?
template<typename T> struct IsResMut : std::false_type {};
template<typename T> struct IsResMut<ResMut<T>> : std::true_type {};

// Type trait: is this a Query<...>?
template<typename T> struct IsQuery : std::false_type {};
template<typename... Args> struct IsQuery<Query<Args...>> : std::true_type {};

// Type trait: is this a QueryMut<...>?
template<typename T> struct IsQueryMut : std::false_type {};
template<typename... Args> struct IsQueryMut<QueryMut<Args...>> : std::true_type {};

// Extract the inner type from Res<T> or ResMut<T>
template<typename T> struct InnerType { using type = T; };
template<typename T> struct InnerType<Res<T>> { using type = T; };
template<typename T> struct InnerType<ResMut<T>> { using type = T; };

// Collect AccessDescriptors from a parameter pack
template<typename... Args>
struct DepsCollector;

template<>
struct DepsCollector<> {
    static void collect(std::vector<AccessDescriptor>&) {}
};

// Skip Commands& (injected from App, not a resource dependency)
template<typename... Rest>
struct DepsCollector<Commands&, Rest...> {
    static void collect(std::vector<AccessDescriptor>& deps) {
        DepsCollector<Rest...>::collect(deps);
    }
};

template<typename T, typename... Rest>
struct DepsCollector<Res<T>, Rest...> {
    static void collect(std::vector<AccessDescriptor>& deps) {
        deps.emplace_back(std::type_index(typeid(T)), AccessMode::Read);
        DepsCollector<Rest...>::collect(deps);
    }
};

template<typename T, typename... Rest>
struct DepsCollector<ResMut<T>, Rest...> {
    static void collect(std::vector<AccessDescriptor>& deps) {
        deps.emplace_back(std::type_index(typeid(T)), AccessMode::Write);
        DepsCollector<Rest...>::collect(deps);
    }
};

// Skip Query<...> and QueryMut<...> (not resource deps, they use World)
template<typename... QArgs, typename... Rest>
struct DepsCollector<Query<QArgs...>, Rest...> {
    static void collect(std::vector<AccessDescriptor>& deps) {
        DepsCollector<Rest...>::collect(deps);
    }
};

template<typename... QArgs, typename... Rest>
struct DepsCollector<QueryMut<QArgs...>, Rest...> {
    static void collect(std::vector<AccessDescriptor>& deps) {
        DepsCollector<Rest...>::collect(deps);
    }
};

// SystemTraits: decompose a function pointer into its dependencies
template<typename Fn>
struct SystemTraits;

// For function pointers: void(*)(Res<A>, ResMut<B>, ...)
template<typename... Args>
struct SystemTraits<void(*)(Args...)> {
    static std::vector<AccessDescriptor> dependencies() {
        std::vector<AccessDescriptor> deps;
        DepsCollector<Args...>::collect(deps);
        return deps;
    }
};

// Helper to construct a Res<T> or ResMut<T> from App
template<typename T>
struct ParamBuilder;

template<typename T>
struct ParamBuilder<Res<T>> {
    static Res<T> build(App& app);
};

template<typename T>
struct ParamBuilder<ResMut<T>> {
    static ResMut<T> build(App& app);
};

#endif // SWIG

} // namespace drift
