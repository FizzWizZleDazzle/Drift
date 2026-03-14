#pragma once

#include <drift/Types.hpp>

namespace drift {

static constexpr int MaxChildren = 16;

struct Parent {
    EntityId entity = InvalidEntityId;
};

struct Children {
    EntityId ids[MaxChildren] = {};
    int count = 0;

    void add(EntityId id) {
        if (count < MaxChildren) {
            ids[count++] = id;
        }
    }

    void remove(EntityId id) {
        for (int i = 0; i < count; ++i) {
            if (ids[i] == id) {
                ids[i] = ids[--count];
                ids[count] = InvalidEntityId;
                return;
            }
        }
    }
};

} // namespace drift
