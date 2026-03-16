#pragma once

#include <drift/Types.hpp>
#include <drift/Resource.hpp>
#include <drift/Events.hpp>
#include <drift/QueryTraits.hpp>

#include <vector>
#include <functional>
#include <typeindex>

namespace drift {

class App;
class Commands;

// Forward declarations for Query types
template<QueryAccess, typename...> class QueryBase;
template<typename... Args> using Query = QueryBase<QueryAccess::ReadOnly, Args...>;
template<typename... Args> using QueryMut = QueryBase<QueryAccess::ReadWrite, Args...>;

// Access mode for dependency tracking.
enum class AccessMode { Read, Write };

// Distinguishes resource access from component access.
enum class AccessKind { Resource, Component };

// Describes one dependency of a system (resource or component).
struct AccessDescriptor {
    std::type_index typeId;
    AccessMode mode;
    AccessKind kind;

    AccessDescriptor(std::type_index t, AccessMode m, AccessKind k = AccessKind::Resource)
        : typeId(t), mode(m), kind(k) {}
};

// ---- Template machinery for extracting resource deps from function signatures ----

// Type trait: is this a Res<T>?
template<typename T> struct IsRes : std::false_type {};
template<typename T> struct IsRes<Res<T>> : std::true_type {};

// Type trait: is this a ResMut<T>?
template<typename T> struct IsResMut : std::false_type {};
template<typename T> struct IsResMut<ResMut<T>> : std::true_type {};

// Type trait: is this a Query<...>?
template<typename T> struct IsQuery : std::false_type {};
template<typename... Args> struct IsQuery<QueryBase<QueryAccess::ReadOnly, Args...>> : std::true_type {};

// Type trait: is this a QueryMut<...>?
template<typename T> struct IsQueryMut : std::false_type {};
template<typename... Args> struct IsQueryMut<QueryBase<QueryAccess::ReadWrite, Args...>> : std::true_type {};

// Type trait: is this an EventWriter<T>?
template<typename T> struct IsEventWriter : std::false_type {};
template<typename T> struct IsEventWriter<EventWriter<T>> : std::true_type {};

// Type trait: is this an EventReader<T>?
template<typename T> struct IsEventReader : std::false_type {};
template<typename T> struct IsEventReader<EventReader<T>> : std::true_type {};

// Extract the inner type from Res<T> or ResMut<T>
template<typename T> struct InnerType { using type = T; };
template<typename T> struct InnerType<Res<T>> { using type = T; };
template<typename T> struct InnerType<ResMut<T>> { using type = T; };

// Extract the event type from EventWriter<T> or EventReader<T>
template<typename T> struct EventInnerType;
template<typename T> struct EventInnerType<EventWriter<T>> { using type = T; };
template<typename T> struct EventInnerType<EventReader<T>> { using type = T; };

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

// ---- Collect component deps from Query/QueryMut template args ----

template<typename... Args>
struct QueryDepsCollector;

template<>
struct QueryDepsCollector<> {
    static void collect(std::vector<AccessDescriptor>&, AccessMode) {}
};

template<typename T, typename... Rest>
struct QueryDepsCollector<T, Rest...> {
    static void collect(std::vector<AccessDescriptor>& deps, AccessMode defaultMode) {
        if constexpr (detail::IsWithFilter<T>::value) {
            // With<F> is a structural read
            using Inner = typename detail::FilterInner<T>::type;
            deps.emplace_back(std::type_index(typeid(Inner)), AccessMode::Read, AccessKind::Component);
        } else if constexpr (detail::IsWithoutFilter<T>::value) {
            // Without<F> is a structural read
            using Inner = typename detail::FilterInner<T>::type;
            deps.emplace_back(std::type_index(typeid(Inner)), AccessMode::Read, AccessKind::Component);
        } else if constexpr (detail::IsChangedFilter<T>::value || detail::IsAddedFilter<T>::value) {
            // Changed<F>/Added<F> is a structural read
            using Inner = typename detail::FilterInner<T>::type;
            deps.emplace_back(std::type_index(typeid(Inner)), AccessMode::Read, AccessKind::Component);
        } else if constexpr (detail::IsOptional<T>::value) {
            // Optional<T> uses the default mode
            using Inner = typename detail::OptionalInner<T>::type;
            deps.emplace_back(std::type_index(typeid(Inner)), defaultMode, AccessKind::Component);
        } else {
            // Data component: uses the default mode (Read for Query, Write for QueryMut)
            deps.emplace_back(std::type_index(typeid(T)), defaultMode, AccessKind::Component);
        }
        QueryDepsCollector<Rest...>::collect(deps, defaultMode);
    }
};

// Query<...> reads components
template<typename... QArgs, typename... Rest>
struct DepsCollector<QueryBase<QueryAccess::ReadOnly, QArgs...>, Rest...> {
    static void collect(std::vector<AccessDescriptor>& deps) {
        QueryDepsCollector<QArgs...>::collect(deps, AccessMode::Read);
        DepsCollector<Rest...>::collect(deps);
    }
};

// QueryMut<...> writes components
template<typename... QArgs, typename... Rest>
struct DepsCollector<QueryBase<QueryAccess::ReadWrite, QArgs...>, Rest...> {
    static void collect(std::vector<AccessDescriptor>& deps) {
        QueryDepsCollector<QArgs...>::collect(deps, AccessMode::Write);
        DepsCollector<Rest...>::collect(deps);
    }
};

// EventWriter<T> writes to Events<T>
template<typename T, typename... Rest>
struct DepsCollector<EventWriter<T>, Rest...> {
    static void collect(std::vector<AccessDescriptor>& deps) {
        deps.emplace_back(std::type_index(typeid(Events<T>)), AccessMode::Write);
        DepsCollector<Rest...>::collect(deps);
    }
};

// EventReader<T> reads from Events<T>
template<typename T, typename... Rest>
struct DepsCollector<EventReader<T>, Rest...> {
    static void collect(std::vector<AccessDescriptor>& deps) {
        deps.emplace_back(std::type_index(typeid(Events<T>)), AccessMode::Read);
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

} // namespace drift
