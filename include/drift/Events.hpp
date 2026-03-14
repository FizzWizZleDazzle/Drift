#pragma once

#include <drift/Resource.hpp>

#include <vector>
#include <cassert>

namespace drift {

#ifndef SWIG

// Double-buffered event storage. Events written in frame N are readable in frame N+1.
template<typename T>
class Events : public Resource {
public:
    void send(const T& event) { current_.push_back(event); }

    // Swap buffers: previous_ gets current frame's events, current_ cleared for next frame.
    void update() {
        previous_.swap(current_);
        current_.clear();
    }

    const std::vector<T>& events() const { return previous_; }
    bool empty() const { return previous_.empty(); }
    size_t count() const { return previous_.size(); }

    auto begin() const { return previous_.begin(); }
    auto end() const { return previous_.end(); }

private:
    std::vector<T> current_;
    std::vector<T> previous_;
};

// Write-only handle for sending events from systems.
template<typename T>
struct EventWriter {
    Events<T>* ptr;

    explicit EventWriter(Events<T>* p) : ptr(p) {}

    void send(const T& event) {
        assert(ptr && "EventWriter<T>: null events resource");
        ptr->send(event);
    }
};

// Read-only handle for consuming events in systems.
template<typename T>
struct EventReader {
    const Events<T>* ptr;

    explicit EventReader(const Events<T>* p) : ptr(p) {}

    const std::vector<T>& events() const {
        assert(ptr && "EventReader<T>: null events resource");
        return ptr->events();
    }

    bool empty() const {
        assert(ptr && "EventReader<T>: null events resource");
        return ptr->empty();
    }

    auto begin() const {
        assert(ptr && "EventReader<T>: null events resource");
        return ptr->begin();
    }

    auto end() const {
        assert(ptr && "EventReader<T>: null events resource");
        return ptr->end();
    }
};

#endif // SWIG

} // namespace drift
