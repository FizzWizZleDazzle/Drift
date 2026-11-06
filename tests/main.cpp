// Runs every registered test. Pass a substring to run only matching tests.

#include "Test.hpp"

#include <cstring>
#include <exception>

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedCases = 0;
    for (const auto& c : drift_test::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        ++run;
        int before = drift_test::failures();
        std::printf("[ RUN  ] %s\n", c.name);
        try {
            c.fn();
        } catch (const drift_test::Abort&) {
        } catch (const std::exception& e) {
            ++drift_test::failures();
            std::printf("  FAILED: exception: %s\n", e.what());
        }
        bool ok = drift_test::failures() == before;
        if (!ok) ++failedCases;
        std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", c.name);
    }
    std::printf("%d tests, %d failed, %d failed checks\n", run, failedCases, drift_test::failures());
    return failedCases == 0 && run > 0 ? 0 : 1;
}
