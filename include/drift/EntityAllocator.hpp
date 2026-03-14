#pragma once

#include <drift/Types.hpp>

namespace drift {

class EntityAllocator {
public:
    virtual ~EntityAllocator() = default;
    virtual EntityId allocate() = 0;
};

} // namespace drift
