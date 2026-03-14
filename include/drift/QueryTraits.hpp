#pragma once

#include <type_traits>
#include <tuple>

namespace drift {

// Access mode for queries
enum class QueryAccess { ReadOnly, ReadWrite };

// Filter types
template<typename T> struct With {};
template<typename T> struct Without {};

// Optional component in queries: entity not filtered out if missing, callback gets pointer (nullptr if absent)
template<typename T> struct Optional {};

// Change detection filters
template<typename T> struct Changed {};
template<typename T> struct Added {};

namespace detail {

// Trait to check if a type is a filter
template<typename T> struct IsFilter : std::false_type {};
template<typename T> struct IsFilter<With<T>> : std::true_type {};
template<typename T> struct IsFilter<Without<T>> : std::true_type {};
template<typename T> struct IsFilter<Changed<T>> : std::true_type {};
template<typename T> struct IsFilter<Added<T>> : std::true_type {};

// Trait to check if a type is Optional<T>
template<typename T> struct IsOptional : std::false_type {};
template<typename T> struct IsOptional<Optional<T>> : std::true_type {};

// Extract inner type from Optional
template<typename T> struct OptionalInner { using type = T; };
template<typename T> struct OptionalInner<Optional<T>> { using type = T; };

// Extract the inner type from filter
template<typename T> struct FilterInner { using type = T; };
template<typename T> struct FilterInner<With<T>> { using type = T; };
template<typename T> struct FilterInner<Without<T>> { using type = T; };
template<typename T> struct FilterInner<Changed<T>> { using type = T; };
template<typename T> struct FilterInner<Added<T>> { using type = T; };

// Check if type is With<T>
template<typename T> struct IsWithFilter : std::false_type {};
template<typename T> struct IsWithFilter<With<T>> : std::true_type {};

// Check if type is Without<T>
template<typename T> struct IsWithoutFilter : std::false_type {};
template<typename T> struct IsWithoutFilter<Without<T>> : std::true_type {};

// Check if type is Changed<T>
template<typename T> struct IsChangedFilter : std::false_type {};
template<typename T> struct IsChangedFilter<Changed<T>> : std::true_type {};

// Check if type is Added<T>
template<typename T> struct IsAddedFilter : std::false_type {};
template<typename T> struct IsAddedFilter<Added<T>> : std::true_type {};

// Filter out filter types from a type list, keeping only data components
// Optional<T> contributes T* (pointer) to the data tuple
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
        std::conditional_t<
            IsOptional<T>::value,
            decltype(std::tuple_cat(
                std::declval<std::tuple<typename OptionalInner<T>::type*>>(),
                std::declval<rest_type>())),
            decltype(std::tuple_cat(std::declval<std::tuple<T>>(), std::declval<rest_type>()))
        >
    >;
};

// Count data (non-filter) components
template<typename... Args> struct DataComponentCount;
template<> struct DataComponentCount<> { static constexpr int value = 0; };
template<typename T, typename... Rest>
struct DataComponentCount<T, Rest...> {
    static constexpr int value = (IsFilter<T>::value ? 0 : 1) + DataComponentCount<Rest...>::value;
};

} // namespace detail
} // namespace drift
