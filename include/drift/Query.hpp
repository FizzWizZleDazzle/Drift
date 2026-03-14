#pragma once

#include <drift/Types.hpp>
#include <drift/World.hpp>
#include <drift/ComponentRegistry.hpp>
#include <drift/QueryTraits.hpp>

#include <cassert>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace drift {

namespace detail {

// Build a flecs query expression from component types and filters
template<typename... Args>
struct QueryExprBuilder {
    static std::string build(World& world, const ComponentRegistry& registry) {
        std::string expr;
        buildImpl<Args...>(world, registry, expr);
        return expr;
    }

private:
    template<typename T>
    static void ensureRegistered(World& world, const ComponentRegistry& registry) {
        if (!registry.has<T>()) {
            world.registerComponent<T>(typeid(T).name());
        }
    }

    template<typename T, typename... Rest>
    static void buildImpl(World& world, const ComponentRegistry& registry, std::string& expr) {
        if (!expr.empty()) expr += ", ";

        if constexpr (IsWithFilter<T>::value) {
            using Inner = typename FilterInner<T>::type;
            ensureRegistered<Inner>(world, registry);
            ComponentId id = registry.get<Inner>();
            expr += "[none] #";
            expr += std::to_string(id);
        } else if constexpr (IsWithoutFilter<T>::value) {
            using Inner = typename FilterInner<T>::type;
            ensureRegistered<Inner>(world, registry);
            ComponentId id = registry.get<Inner>();
            expr += "!#";
            expr += std::to_string(id);
        } else if constexpr (IsChangedFilter<T>::value || IsAddedFilter<T>::value) {
            // Changed/Added act as With<T> for query expression, post-filtered during iteration
            using Inner = typename FilterInner<T>::type;
            ensureRegistered<Inner>(world, registry);
            ComponentId id = registry.get<Inner>();
            expr += "[none] #";
            expr += std::to_string(id);
        } else if constexpr (IsOptional<T>::value) {
            using Inner = typename OptionalInner<T>::type;
            ensureRegistered<Inner>(world, registry);
            ComponentId id = registry.get<Inner>();
            expr += "?#";
            expr += std::to_string(id);
        } else {
            ensureRegistered<T>(world, registry);
            ComponentId id = registry.get<T>();
            expr += "#";
            expr += std::to_string(id);
        }

        if constexpr (sizeof...(Rest) > 0) {
            buildImpl<Rest...>(world, registry, expr);
        }
    }
};

// Check if an entity matches all component requirements in Args...
template<typename... Args>
struct EntityMatcher;

template<>
struct EntityMatcher<> {
    static bool matches(World&, const ComponentRegistry&, EntityId) { return true; }
};

template<typename T, typename... Rest>
struct EntityMatcher<T, Rest...> {
    static bool matches(World& world, const ComponentRegistry& registry, EntityId entity) {
        if constexpr (IsChangedFilter<T>::value) {
            using Inner = typename FilterInner<T>::type;
            if (!registry.has<Inner>()) return false;
            ComponentId cid = registry.get<Inner>();
            if (!world.hasComponent(entity, cid)) return false;
            if (world.getComponentChangeTick(entity, cid) != world.currentTick()) return false;
        } else if constexpr (IsAddedFilter<T>::value) {
            using Inner = typename FilterInner<T>::type;
            if (!registry.has<Inner>()) return false;
            ComponentId cid = registry.get<Inner>();
            if (!world.hasComponent(entity, cid)) return false;
            if (world.getComponentAddTick(entity, cid) != world.currentTick()) return false;
        } else if constexpr (IsWithFilter<T>::value) {
            using Inner = typename FilterInner<T>::type;
            if (!registry.has<Inner>()) return false;
            if (!world.hasComponent(entity, registry.get<Inner>())) return false;
        } else if constexpr (IsWithoutFilter<T>::value) {
            using Inner = typename FilterInner<T>::type;
            if (!registry.has<Inner>()) return true; // unregistered = entity doesn't have it
            if (world.hasComponent(entity, registry.get<Inner>())) return false;
        } else if constexpr (IsOptional<T>::value) {
            // Optional components never exclude entities
        } else {
            if (!registry.has<T>()) return false;
            if (!world.hasComponent(entity, registry.get<T>())) return false;
        }
        return EntityMatcher<Rest...>::matches(world, registry, entity);
    }
};

// Field accessor: handles regular types and pointer types (from Optional<T>)
template<typename T>
struct FieldAccessor {
    static decltype(auto) getMut(void* field, int32_t i) {
        return static_cast<T*>(field)[i];
    }
    static decltype(auto) getConst(void* field, int32_t i) {
        return static_cast<const T*>(field)[i];
    }
};

template<typename T>
struct FieldAccessor<T*> {
    static T* getMut(void* field, int32_t i) {
        return field ? &static_cast<T*>(field)[i] : nullptr;
    }
    static const T* getConst(void* field, int32_t i) {
        return field ? &static_cast<const T*>(field)[i] : nullptr;
    }
};

// Helper to invoke lambda with entity ID + component pointers
template<typename Tuple, typename IndexSeq>
struct QueryInvokerWithEntity;

template<typename... Ts, size_t... Is>
struct QueryInvokerWithEntity<std::tuple<Ts...>, std::index_sequence<Is...>> {
    template<bool Mutable, typename Fn>
    static void invoke(Fn& fn, EntityId* entities, void** fields, int32_t count) {
        for (int32_t i = 0; i < count; ++i) {
            if constexpr (Mutable) {
                fn(entities[i], FieldAccessor<Ts>::getMut(fields[Is], i)...);
            } else {
                fn(entities[i], FieldAccessor<Ts>::getConst(fields[Is], i)...);
            }
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
                fn(FieldAccessor<Ts>::getMut(fields[Is], i)...);
            } else {
                fn(FieldAccessor<Ts>::getConst(fields[Is], i)...);
            }
        }
    }
};

// Helper to extract a single element from query fields
template<typename Tuple, typename IndexSeq>
struct QueryElementExtractor;

template<typename... Ts, size_t... Is>
struct QueryElementExtractor<std::tuple<Ts...>, std::index_sequence<Is...>> {
    template<bool Mutable>
    static auto extract(void** fields, int32_t index) {
        if constexpr (sizeof...(Ts) == 1) {
            using T = std::tuple_element_t<0, std::tuple<Ts...>>;
            if constexpr (Mutable) {
                return FieldAccessor<T>::getMut(fields[0], index);
            } else {
                return FieldAccessor<T>::getConst(fields[0], index);
            }
        } else {
            if constexpr (Mutable) {
                return std::tuple<Ts...>{FieldAccessor<Ts>::getMut(fields[Is], index)...};
            } else {
                return std::tuple<Ts...>{FieldAccessor<Ts>::getConst(fields[Is], index)...};
            }
        }
    }
};

// Check if any Args have Changed<T> or Added<T> filters
template<typename... Args>
struct HasChangeFilters : std::false_type {};

template<typename T, typename... Rest>
struct HasChangeFilters<T, Rest...> {
    static constexpr bool value = IsChangedFilter<T>::value || IsAddedFilter<T>::value || HasChangeFilters<Rest...>::value;
};

// Per-entity change filter check
template<typename... Args>
struct ChangeFilter;

template<>
struct ChangeFilter<> {
    static bool passes(World&, const ComponentRegistry&, EntityId) { return true; }
};

template<typename T, typename... Rest>
struct ChangeFilter<T, Rest...> {
    static bool passes(World& world, const ComponentRegistry& registry, EntityId entity) {
        if constexpr (IsChangedFilter<T>::value) {
            using Inner = typename FilterInner<T>::type;
            ComponentId cid = registry.get<Inner>();
            if (world.getComponentChangeTick(entity, cid) != world.currentTick()) return false;
        } else if constexpr (IsAddedFilter<T>::value) {
            using Inner = typename FilterInner<T>::type;
            ComponentId cid = registry.get<Inner>();
            if (world.getComponentAddTick(entity, cid) != world.currentTick()) return false;
        }
        return ChangeFilter<Rest...>::passes(world, registry, entity);
    }
};

} // namespace detail

// Unified query implementation parameterized by access mode
template<QueryAccess Access, typename... Args>
class QueryBase {
    using DataTuple = typename detail::DataComponents<Args...>::type;
    static constexpr int DataCount = detail::DataComponentCount<Args...>::value;
    static constexpr bool Mutable = (Access == QueryAccess::ReadWrite);
    static constexpr bool HasChangeFilters = detail::HasChangeFilters<Args...>::value;

    World& world_;
    const ComponentRegistry& registry_;
public:
    QueryBase(World& w, const ComponentRegistry& r) : world_(w), registry_(r) {}

    // Check if an entity matches this query's component requirements
    bool contains(EntityId entity) const {
        if (!world_.isAlive(entity)) return false;
        return detail::EntityMatcher<Args...>::matches(world_, registry_, entity);
    }

    template<typename Fn>
    void iter(Fn&& fn) {
        std::lock_guard<std::recursive_mutex> qlock(world_.queryMutex());
        std::string expr = detail::QueryExprBuilder<Args...>::build(world_, registry_);
        QueryIter qi = world_.queryIter(expr.c_str());

        while (world_.queryNext(&qi)) {
            void* fields[DataCount > 0 ? DataCount : 1];
            fillFields<Args...>(qi, fields, 0);

            if constexpr (HasChangeFilters) {
                // Must check per-entity with entity IDs
                EntityId* entities = world_.queryEntities(&qi);
                for (int32_t i = 0; i < qi.count; ++i) {
                    if (detail::ChangeFilter<Args...>::passes(world_, registry_, entities[i])) {
                        invokeAtIndex<DataTuple>(fn, fields, i);
                    }
                }
            } else {
                detail::QueryInvoker<DataTuple, std::make_index_sequence<DataCount>>::template invoke<Mutable>(
                    fn, fields, qi.count);
            }
        }
        world_.queryFini(&qi);
    }

    // Iterate with entity ID as first callback param
    template<typename Fn>
    void iterWithEntity(Fn&& fn) {
        std::lock_guard<std::recursive_mutex> qlock(world_.queryMutex());
        std::string expr = detail::QueryExprBuilder<Args...>::build(world_, registry_);
        QueryIter qi = world_.queryIter(expr.c_str());

        while (world_.queryNext(&qi)) {
            void* fields[DataCount > 0 ? DataCount : 1];
            fillFields<Args...>(qi, fields, 0);
            EntityId* entities = world_.queryEntities(&qi);

            if constexpr (HasChangeFilters) {
                for (int32_t i = 0; i < qi.count; ++i) {
                    if (detail::ChangeFilter<Args...>::passes(world_, registry_, entities[i])) {
                        invokeWithEntityAtIndex<DataTuple>(fn, entities, fields, i);
                    }
                }
            } else {
                detail::QueryInvokerWithEntity<DataTuple, std::make_index_sequence<DataCount>>::template invoke<Mutable>(
                    fn, entities, fields, qi.count);
            }
        }
        world_.queryFini(&qi);
    }

    // Returns true if the query matches no entities
    bool isEmpty() {
        std::lock_guard<std::recursive_mutex> qlock(world_.queryMutex());
        std::string expr = detail::QueryExprBuilder<Args...>::build(world_, registry_);
        QueryIter qi = world_.queryIter(expr.c_str());

        bool found = false;
        while (world_.queryNext(&qi)) {
            if (qi.count > 0) { found = true; break; }
        }
        world_.queryFini(&qi);
        return !found;
    }

    // Returns the single matching entity's data components.
    // For one data component returns T, for multiple returns std::tuple<Ts...>.
    // Asserts exactly one match exists.
    auto single() {
        static_assert(DataCount > 0, "single() requires at least one data component");

        std::lock_guard<std::recursive_mutex> qlock(world_.queryMutex());
        std::string expr = detail::QueryExprBuilder<Args...>::build(world_, registry_);
        QueryIter qi = world_.queryIter(expr.c_str());

        using ResultType = decltype(
            detail::QueryElementExtractor<DataTuple, std::make_index_sequence<DataCount>>::template extract<Mutable>(nullptr, 0));
        std::optional<ResultType> result;
        int totalCount = 0;

        while (world_.queryNext(&qi)) {
            if (qi.count > 0) {
                void* fields[DataCount > 0 ? DataCount : 1];
                fillFields<Args...>(qi, fields, 0);
                if (!result.has_value()) {
                    result = detail::QueryElementExtractor<DataTuple, std::make_index_sequence<DataCount>>::template extract<Mutable>(fields, 0);
                }
                totalCount += qi.count;
            }
        }
        world_.queryFini(&qi);

        assert(totalCount == 1 && "single() expects exactly one matching entity");
        return result.value();
    }

private:
    // Per-index invocation helpers for change-filtered iteration
    template<typename Tuple, typename Fn, size_t... Is>
    void invokeAtIndexImpl(Fn& fn, void** fields, int32_t i, std::index_sequence<Is...>) {
        using TupleTypes = Tuple;
        if constexpr (Mutable) {
            fn(detail::FieldAccessor<std::tuple_element_t<Is, TupleTypes>>::getMut(fields[Is], i)...);
        } else {
            fn(detail::FieldAccessor<std::tuple_element_t<Is, TupleTypes>>::getConst(fields[Is], i)...);
        }
    }

    template<typename Tuple, typename Fn>
    void invokeAtIndex(Fn& fn, void** fields, int32_t i) {
        invokeAtIndexImpl<Tuple>(fn, fields, i, std::make_index_sequence<DataCount>{});
    }

    template<typename Tuple, typename Fn, size_t... Is>
    void invokeWithEntityAtIndexImpl(Fn& fn, EntityId* entities, void** fields, int32_t i, std::index_sequence<Is...>) {
        using TupleTypes = Tuple;
        if constexpr (Mutable) {
            fn(entities[i], detail::FieldAccessor<std::tuple_element_t<Is, TupleTypes>>::getMut(fields[Is], i)...);
        } else {
            fn(entities[i], detail::FieldAccessor<std::tuple_element_t<Is, TupleTypes>>::getConst(fields[Is], i)...);
        }
    }

    template<typename Tuple, typename Fn>
    void invokeWithEntityAtIndex(Fn& fn, EntityId* entities, void** fields, int32_t i) {
        invokeWithEntityAtIndexImpl<Tuple>(fn, entities, fields, i, std::make_index_sequence<DataCount>{});
    }

    template<typename T, typename... Rest>
    void fillFields(QueryIter& qi, void** fields, int dataIdx) {
        if constexpr (detail::IsOptional<T>::value) {
            using Inner = typename detail::OptionalInner<T>::type;
            fields[dataIdx] = world_.queryFieldOptional(&qi, dataIdx, sizeof(Inner));
            if constexpr (sizeof...(Rest) > 0) {
                fillFields<Rest...>(qi, fields, dataIdx + 1);
            }
        } else if constexpr (!detail::IsFilter<T>::value) {
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

// Type aliases preserving the existing API
template<typename... Args>
using Query = QueryBase<QueryAccess::ReadOnly, Args...>;

template<typename... Args>
using QueryMut = QueryBase<QueryAccess::ReadWrite, Args...>;

} // namespace drift
