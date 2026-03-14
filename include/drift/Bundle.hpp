#pragma once

#include <tuple>
#include <utility>

namespace drift {

#ifndef SWIG

template<typename... Ts>
struct Bundle : std::tuple<Ts...> {
    using std::tuple<Ts...>::tuple;
};

// Deduction guide
template<typename... Ts>
Bundle(Ts...) -> Bundle<Ts...>;

#endif

} // namespace drift
