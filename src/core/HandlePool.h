#pragma once

#include <drift/Handle.h>
#include <vector>
#include <cassert>

namespace drift {

// Generational slot pool. Used internally by all Resource classes.
// T = the tag type for Handle<T>, V = the value stored per slot.
template<typename T, typename V>
class HandlePool {
public:
    HandlePool() {
        // Slot 0 is reserved (null handle).
        slots_.push_back(Slot{});
        slots_[0].generation = 0;
        slots_[0].alive = false;
    }

    Handle<T> create(const V& value) {
        uint32_t index;
        if (!freeList_.empty()) {
            index = freeList_.back();
            freeList_.pop_back();
        } else {
            index = static_cast<uint32_t>(slots_.size());
            slots_.push_back(Slot{});
        }

        auto& slot = slots_[index];
        slot.value = value;
        slot.alive = true;
        return Handle<T>::make(index, slot.generation);
    }

    void destroy(Handle<T> handle) {
        if (!valid(handle)) return;
        uint32_t idx = handle.index();
        auto& slot = slots_[idx];
        slot.alive = false;
        slot.generation = (slot.generation + 1) & 0xFFF;
        slot.value = V{};
        freeList_.push_back(idx);
    }

    bool valid(Handle<T> handle) const {
        if (handle.id == 0) return false;
        uint32_t idx = handle.index();
        if (idx >= slots_.size()) return false;
        const auto& slot = slots_[idx];
        return slot.alive && slot.generation == handle.generation();
    }

    V* get(Handle<T> handle) {
        if (!valid(handle)) return nullptr;
        return &slots_[handle.index()].value;
    }

    const V* get(Handle<T> handle) const {
        if (!valid(handle)) return nullptr;
        return &slots_[handle.index()].value;
    }

    // Iterate all alive slots
    template<typename Fn>
    void forEach(Fn&& fn) {
        for (uint32_t i = 1; i < slots_.size(); ++i) {
            if (slots_[i].alive) {
                fn(Handle<T>::make(i, slots_[i].generation), slots_[i].value);
            }
        }
    }

    size_t aliveCount() const {
        size_t count = 0;
        for (size_t i = 1; i < slots_.size(); ++i) {
            if (slots_[i].alive) ++count;
        }
        return count;
    }

    void clear() {
        slots_.clear();
        freeList_.clear();
        slots_.push_back(Slot{});
        slots_[0].generation = 0;
        slots_[0].alive = false;
    }

private:
    struct Slot {
        V value{};
        uint32_t generation = 1;
        bool alive = false;
    };

    std::vector<Slot> slots_;
    std::vector<uint32_t> freeList_;
};

} // namespace drift
