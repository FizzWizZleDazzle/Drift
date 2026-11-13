#pragma once

// Weighted random choice: loot drops, enemy spawns, room types.
//
//   WeightedTable<std::string> loot;
//   loot.add("coin", 70).add("potion", 25).add("sword", 5);
//   const std::string* drop = loot.roll(rng);
//
// The same Rng state gives the same rolls on every platform.

#include <drift/math/Random.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace drift {

template<typename T>
class WeightedTable {
public:
    struct Entry {
        T item;
        float weight;
    };

    // Weights are relative: 70, 25, 5 and 0.7, 0.25, 0.05 roll the same.
    // A weight <= 0 never rolls.
    WeightedTable& add(T item, float weight) {
        entries_.push_back({std::move(item), weight});
        return *this;
    }
    void clear() { entries_.clear(); }
    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    const std::vector<Entry>& entries() const { return entries_; }

    float totalWeight() const {
        float total = 0.f;
        for (const Entry& e : entries_) total += e.weight > 0.f ? e.weight : 0.f;
        return total;
    }

    // Index of one rolled entry, or -1 when no entry has a positive weight.
    int32_t rollIndex(Rng& rng) const {
        std::vector<float> weights;
        for (const Entry& e : entries_) weights.push_back(e.weight);
        return rng.weightedIndex(weights);
    }

    // One rolled item, or nullptr when no entry has a positive weight.
    const T* roll(Rng& rng) const {
        int32_t i = rollIndex(rng);
        return i < 0 ? nullptr : &entries_[static_cast<size_t>(i)].item;
    }

    // `count` rolls; an item can come up more than once.
    std::vector<T> roll(Rng& rng, size_t count) const {
        std::vector<T> out;
        for (size_t n = 0; n < count; ++n) {
            const T* item = roll(rng);
            if (!item) break;
            out.push_back(*item);
        }
        return out;
    }

    // Up to `count` different entries, without replacement: a rolled entry
    // leaves the table for the rest of the draw.
    std::vector<T> rollUnique(Rng& rng, size_t count) const {
        std::vector<T> out;
        std::vector<float> weights;
        for (const Entry& e : entries_) weights.push_back(e.weight);
        while (out.size() < count) {
            int32_t i = rng.weightedIndex(weights);
            if (i < 0) break;
            out.push_back(entries_[static_cast<size_t>(i)].item);
            weights[static_cast<size_t>(i)] = 0.f;
        }
        return out;
    }

private:
    std::vector<Entry> entries_;
};

} // namespace drift
