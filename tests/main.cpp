#include <exception>

#include "test.hpp"

int main() {
    int passed = 0;
    for (const ampify::test::Case& test : ampify::test::Cases()) {
        int failuresBefore = ampify::test::Failures();
        try {
            test.run();
        } catch (const std::exception& error) {
            ampify::test::ReportFailure(test.name.c_str(), 0, std::string("exception: ") + error.what());
        } catch (...) {
            ampify::test::ReportFailure(test.name.c_str(), 0, "unknown exception");
        }

        if (ampify::test::Failures() == failuresBefore) {
            passed++;
        } else {
            std::cerr << "FAILED: " << test.name << '\n';
        }
    }

    std::cout << passed << " / " << ampify::test::Cases().size() << " tests passed\n";
    return ampify::test::Failures() == 0 ? 0 : 1;
}
