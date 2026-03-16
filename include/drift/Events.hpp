#pragma once

#include <drift/Resource.hpp>

#include <vector>
#include <cassert>

namespace drift {

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
    explicit EventWriter(Events<T>* p) : ptr_(p) {}

    void send(const T& event) {
        assert(ptr_ && "EventWriter<T>: null events resource");
        ptr_->send(event);
    }

    Events<T>* get() { return ptr_; }
    const Events<T>* get() const { return ptr_; }

private:
    Events<T>* ptr_;
};

// Read-only handle for consuming events in systems.
template<typename T>
struct EventReader {
    explicit EventReader(const Events<T>* p) : ptr_(p) {}

    const std::vector<T>& events() const {
        assert(ptr_ && "EventReader<T>: null events resource");
        return ptr_->events();
    }

    bool empty() const {
        assert(ptr_ && "EventReader<T>: null events resource");
        return ptr_->empty();
    }

    auto begin() const {
        assert(ptr_ && "EventReader<T>: null events resource");
        return ptr_->begin();
    }

    auto end() const {
        assert(ptr_ && "EventReader<T>: null events resource");
        return ptr_->end();
    }

    const Events<T>* get() const { return ptr_; }

private:
    const Events<T>* ptr_;
};

} // namespace drift
