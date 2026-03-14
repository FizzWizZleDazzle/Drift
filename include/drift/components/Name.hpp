#pragma once

#include <cstring>

namespace drift {

struct Name {
    char value[64] = {};

    Name() = default;

    explicit Name(const char* v) {
        if (v) {
            std::strncpy(value, v, sizeof(value) - 1);
            value[sizeof(value) - 1] = '\0';
        }
    }
};

} // namespace drift
