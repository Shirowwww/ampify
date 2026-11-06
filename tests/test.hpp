#pragma once

// A deliberately small test harness: no dependency, builds the same way for Linux and Windows.

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace ampify::test {

struct Case {
    std::string name;
    std::function<void()> run;
};

inline std::vector<Case>& Cases() {
    static std::vector<Case> cases;
    return cases;
}

inline int& Failures() {
    static int failures = 0;
    return failures;
}

struct Registration {
    Registration(const char* name, std::function<void()> run) { Cases().push_back({name, std::move(run)}); }
};

inline void ReportFailure(const char* file, int line, const std::string& message) {
    std::cerr << file << ':' << line << ": " << message << '\n';
    Failures()++;
}

// Thrown by REQUIRE (the failure is already reported) and by SKIP; the runner ends the case quietly.
struct StopCase {};
struct SkipCase {
    std::string reason;
};

}  // namespace ampify::test

#define TEST_CASE(name)                                                    \
    static void name();                                                    \
    static const ampify::test::Registration name##_registration(#name, name); \
    static void name()

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) ampify::test::ReportFailure(__FILE__, __LINE__, "CHECK(" #condition ")"); \
    } while (0)

// Like CHECK, but ends the case: for a precondition the rest of the case cannot run without.
#define REQUIRE(condition)                                                                \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            ampify::test::ReportFailure(__FILE__, __LINE__, "REQUIRE(" #condition ")");   \
            throw ampify::test::StopCase{};                                               \
        }                                                                                 \
    } while (0)

// Ends the case as not run, with the reason; the summary counts it apart from the passes.
#define SKIP(reason) throw ampify::test::SkipCase{reason}

#define CHECK_NEAR(actual, expected, tolerance)                                                    \
    do {                                                                                           \
        double actualValue = (actual);                                                             \
        double expectedValue = (expected);                                                         \
        if (!(std::fabs(actualValue - expectedValue) <= (tolerance))) {                            \
            std::ostringstream message;                                                            \
            message << "CHECK_NEAR(" #actual ", " #expected ") got " << actualValue                \
                    << ", expected " << expectedValue << " +/- " << (tolerance);                   \
            ampify::test::ReportFailure(__FILE__, __LINE__, message.str());                           \
        }                                                                                          \
    } while (0)
