#pragma once

// Minimal self-registering test harness: TEST(name) { CHECK(cond); }.
// A failed CHECK reports and lets the test continue; REQUIRE stops it.

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace drift_test {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int n = 0;
    return n;
}

struct Register {
    Register(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

struct Abort {};

inline bool check(bool ok, const char* expr, const char* file, int line) {
    if (!ok) {
        ++failures();
        std::printf("  FAILED %s:%d: %s\n", file, line, expr);
    }
    return ok;
}

} // namespace drift_test

#define DRIFT_TEST_CAT2(a, b) a##b
#define DRIFT_TEST_CAT(a, b) DRIFT_TEST_CAT2(a, b)

#define TEST(name)                                                                  \
    static void name();                                                             \
    static ::drift_test::Register DRIFT_TEST_CAT(reg_, name)(#name, &name);         \
    static void name()

#define CHECK(cond) ::drift_test::check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define REQUIRE(cond) do { if (!CHECK(cond)) throw ::drift_test::Abort{}; } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(std::fabs(double(a) - double(b)) <= double(eps))
