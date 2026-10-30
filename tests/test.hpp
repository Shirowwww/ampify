#pragma once

// A deliberately small test harness: no dependency, builds the same way for Linux and Windows.

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace amk::test {

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

}  // namespace amk::test

#define TEST_CASE(name)                                                    \
    static void name();                                                    \
    static const amk::test::Registration name##_registration(#name, name); \
    static void name()

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) amk::test::ReportFailure(__FILE__, __LINE__, "CHECK(" #condition ")"); \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                                    \
    do {                                                                                           \
        double actualValue = (actual);                                                             \
        double expectedValue = (expected);                                                         \
        if (!(std::fabs(actualValue - expectedValue) <= (tolerance))) {                            \
            std::ostringstream message;                                                            \
            message << "CHECK_NEAR(" #actual ", " #expected ") got " << actualValue                \
                    << ", expected " << expectedValue << " +/- " << (tolerance);                   \
            amk::test::ReportFailure(__FILE__, __LINE__, message.str());                           \
        }                                                                                          \
    } while (0)
