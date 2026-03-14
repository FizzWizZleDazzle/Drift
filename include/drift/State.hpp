#pragma once

#include <drift/Resource.hpp>

#include <optional>
#include <functional>

namespace drift {

class App;

#ifndef SWIG

// RunCondition: predicate checked before running a system
using RunCondition = std::function<bool(const App&)>;

// State<T> — holds the current value of a state enum
template<typename T>
struct State : public Resource {
    T current;
    explicit State(T initial) : current(initial) {}
};

// NextState<T> — write-only handle for requesting state transitions
template<typename T>
struct NextState : public Resource {
    void set(T value) { pending_ = value; }
    bool hasPending() const { return pending_.has_value(); }
    T take() { T v = pending_.value(); pending_.reset(); return v; }

private:
    std::optional<T> pending_;
};

#endif // SWIG

} // namespace drift
