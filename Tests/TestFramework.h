#pragma once

// A tiny hand-rolled test harness instead of vendoring Catch2/GoogleTest -- matches this
// project's own established preference for small, dependency-free infrastructure over a
// new external library (see MeshLoader's hand-rolled OBJ parser for the same reasoning).
// Each TEST_CASE registers itself at static-init time (mirroring GameScripts'
// REGISTER_BEHAVIOUR/ScriptRegistrar pattern exactly), so Tests/Main.cpp just needs to
// run every registered case and doesn't need to know their names ahead of time.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace Duality::Test {

    struct TestCase {
        std::string Name;
        std::function<void()> Run;
    };

    inline std::vector<TestCase>& AllTests() {
        static std::vector<TestCase> tests;
        return tests;
    }

    struct Registrar {
        Registrar(const std::string& name, std::function<void()> run) {
            AllTests().push_back({ name, std::move(run) });
        }
    };

    // Thrown by CHECK on failure so one bad assertion doesn't cascade into unrelated
    // failures later in the same test case -- Main.cpp catches it per-test-case and
    // keeps running the rest of the suite.
    struct CheckFailure {
        std::string Message;
    };

    // Tracks pass/fail counts within the currently-running test case (reset by Main.cpp
    // before each one) so a test can report "N/M checks passed" instead of stopping dead
    // at the first CHECK -- see CHECK_SOFT below for checks that shouldn't abort the case.
    inline int& SoftPassCount() { static int n = 0; return n; }
    inline int& SoftFailCount() { static int n = 0; return n; }

}

// Aborts the current TEST_CASE immediately (via CheckFailure) -- use for a precondition
// the rest of the test case genuinely can't proceed without.
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            throw ::Duality::Test::CheckFailure{ std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": CHECK failed: " #cond }; \
        } \
    } while (0)

// Records a pass/fail but keeps running the rest of the test case -- use for independent
// assertions where seeing ALL of them fail (or pass) in one run is more useful than
// stopping at the first one, same reasoning as this session's own scratchpad test scripts.
#define CHECK_SOFT(cond, what) \
    do { \
        if (cond) { \
            ::Duality::Test::SoftPassCount()++; \
        } else { \
            ::Duality::Test::SoftFailCount()++; \
            std::printf("    FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__); \
        } \
    } while (0)

// Two-level indirection so ##__LINE__ actually pastes the expanded line NUMBER, not the
// literal text "__LINE__" -- without DE_CONCAT/DE_CONCAT_, every TEST_CASE in the same
// file would generate an identically-named function (a redefinition error the moment a
// file has more than one test case).
#define DE_CONCAT_(a, b) a##b
#define DE_CONCAT(a, b) DE_CONCAT_(a, b)

#define TEST_CASE(name) \
    static void DE_CONCAT(DE_TEST_FN_, __LINE__)(); \
    static ::Duality::Test::Registrar DE_CONCAT(DE_TEST_REG_, __LINE__)(name, &DE_CONCAT(DE_TEST_FN_, __LINE__)); \
    static void DE_CONCAT(DE_TEST_FN_, __LINE__)()
