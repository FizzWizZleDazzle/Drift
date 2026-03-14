#pragma once

#include <drift/Types.hpp>
#include <drift/World.hpp>
#include <drift/ComponentRegistry.hpp>

#include <tuple>
#include <type_traits>
#include <utility>

namespace drift {

// Filter types
template<typename T> struct With {};
template<typename T> struct Without {};

namespace detail {

// Trait to check if a type is a filter
template<typename T> struct IsFilter : std::false_type {};
template<typename T> struct IsFilter<With<T>> : std::true_type {};
template<typename T> struct IsFilter<Without<T>> : std::true_type {};

// Extract the inner type from filter
template<typename T> struct FilterInner { using type = T; };
template<typename T> struct FilterInner<With<T>> { using type = T; };
template<typename T> struct FilterInner<Without<T>> { using type = T; };

// Check if type is With<T>
template<typename T> struct IsWithFilter : std::false_type {};
template<typename T> struct IsWithFilter<With<T>> : std::true_type {};

// Check if type is Without<T>
template<typename T> struct IsWithoutFilter : std::false_type {};
template<typename T> struct IsWithoutFilter<Without<T>> : std::true_type {};

// Filter out filter types from a type list, keeping only data components
template<typename... Args> struct DataComponents;

template<>
struct DataComponents<> {
    using type = std::tuple<>;
};

template<typename T, typename... Rest>
struct DataComponents<T, Rest...> {
    using rest_type = typename DataComponents<Rest...>::type;
    using type = std::conditional_t<
        IsFilter<T>::value,
        rest_type,
        decltype(std::tuple_cat(std::declval<std::tuple<T>>(), std::declval<rest_type>()))
    >;
};

// Count data (non-filter) components
template<typename... Args> struct DataComponentCount;
template<> struct DataComponentCount<> { static constexpr int value = 0; };
template<typename T, typename... Rest>
struct DataComponentCount<T, Rest...> {
    static constexpr int value = (IsFilter<T>::value ? 0 : 1) + DataComponentCount<Rest...>::value;
};

// Build a flecs query expression from component types and filters
template<typename... Args>
struct QueryExprBuilder {
    static std::string build(const ComponentRegistry& registry) {
        std::string expr;
        buildImpl<Args...>(registry, expr);
        return expr;
    }

private:
    template<typename T, typename... Rest>
    static void buildImpl(const ComponentRegistry& registry, std::string& expr) {
        if (!expr.empty()) expr += ", ";

        if constexpr (IsWithFilter<T>::value) {
            using Inner = typename FilterInner<T>::type;
            ComponentId id = registry.get<Inner>();
            expr += "[none] ";
            expr += std::to_string(id);
        } else if constexpr (IsWithoutFilter<T>::value) {
            using Inner = typename FilterInner<T>::type;
            ComponentId id = registry.get<Inner>();
            expr += "!";
            expr += std::to_string(id);
        } else {
            ComponentId id = registry.get<T>();
            expr += std::to_string(id);
        }

        if constexpr (sizeof...(Rest) > 0) {
            buildImpl<Rest...>(registry, expr);
        }
    }
};

// Helper to invoke lambda with the right component pointers
template<typename Tuple, typename IndexSeq>
struct QueryInvoker;

template<typename... Ts, size_t... Is>
struct QueryInvoker<std::tuple<Ts...>, std::index_sequence<Is...>> {
    template<bool Mutable, typename Fn>
    static void invoke(Fn& fn, void** fields, int32_t count) {
        for (int32_t i = 0; i < count; ++i) {
            if constexpr (Mutable) {
                fn(static_cast<Ts*>(fields[Is])[i]...);
            } else {
                fn(static_cast<const Ts*>(fields[Is])[i]...);
            }
        }
    }
};

} // namespace detail

// Read-only query
template<typename... Args>
class Query {
    using DataTuple = typename detail::DataComponents<Args...>::type;
    static constexpr int DataCount = detail::DataComponentCount<Args...>::value;

    World& world_;
    const ComponentRegistry& registry_;
public:
    Query(World& w, const ComponentRegistry& r) : world_(w), registry_(r) {}

    template<typename Fn>
    void iter(Fn&& fn) {
        std::string expr = detail::QueryExprBuilder<Args...>::build(registry_);
        QueryIter qi = world_.queryIter(expr.c_str());

        while (world_.queryNext(&qi)) {
            void* fields[DataCount > 0 ? DataCount : 1];
            fillFields<Args...>(qi, fields, 0);

            detail::QueryInvoker<DataTuple, std::make_index_sequence<DataCount>>::template invoke<false>(
                fn, fields, qi.count);
        }
        world_.queryFini(&qi);
    }

private:
    template<typename T, typename... Rest>
    void fillFields(QueryIter& qi, void** fields, int dataIdx) {
        if constexpr (!detail::IsFilter<T>::value) {
            fields[dataIdx] = world_.queryField(&qi, dataIdx, sizeof(T));
            if constexpr (sizeof...(Rest) > 0) {
                fillFields<Rest...>(qi, fields, dataIdx + 1);
            }
        } else {
            if constexpr (sizeof...(Rest) > 0) {
                fillFields<Rest...>(qi, fields, dataIdx);
            }
        }
    }
};

// Read-write query
template<typename... Args>
class QueryMut {
    using DataTuple = typename detail::DataComponents<Args...>::type;
    static constexpr int DataCount = detail::DataComponentCount<Args...>::value;

    World& world_;
    const ComponentRegistry& registry_;
public:
    QueryMut(World& w, const ComponentRegistry& r) : world_(w), registry_(r) {}

    template<typename Fn>
    void iter(Fn&& fn) {
        std::string expr = detail::QueryExprBuilder<Args...>::build(registry_);
        QueryIter qi = world_.queryIter(expr.c_str());

        while (world_.queryNext(&qi)) {
            void* fields[DataCount > 0 ? DataCount : 1];
            fillFields<Args...>(qi, fields, 0);

            detail::QueryInvoker<DataTuple, std::make_index_sequence<DataCount>>::template invoke<true>(
                fn, fields, qi.count);
        }
        world_.queryFini(&qi);
    }

private:
    template<typename T, typename... Rest>
    void fillFields(QueryIter& qi, void** fields, int dataIdx) {
        if constexpr (!detail::IsFilter<T>::value) {
            fields[dataIdx] = world_.queryField(&qi, dataIdx, sizeof(T));
            if constexpr (sizeof...(Rest) > 0) {
                fillFields<Rest...>(qi, fields, dataIdx + 1);
            }
        } else {
            if constexpr (sizeof...(Rest) > 0) {
                fillFields<Rest...>(qi, fields, dataIdx);
            }
        }
    }
};

} // namespace drift
