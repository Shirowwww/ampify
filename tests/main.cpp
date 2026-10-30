#include <exception>

#include "test.hpp"

int main() {
    int passed = 0;
    for (const amk::test::Case& test : amk::test::Cases()) {
        int failuresBefore = amk::test::Failures();
        try {
            test.run();
        } catch (const std::exception& error) {
            amk::test::ReportFailure(test.name.c_str(), 0, std::string("exception: ") + error.what());
        } catch (...) {
            amk::test::ReportFailure(test.name.c_str(), 0, "unknown exception");
        }

        if (amk::test::Failures() == failuresBefore) {
            passed++;
        } else {
            std::cerr << "FAILED: " << test.name << '\n';
        }
    }

    std::cout << passed << " / " << amk::test::Cases().size() << " tests passed\n";
    return amk::test::Failures() == 0 ? 0 : 1;
}
