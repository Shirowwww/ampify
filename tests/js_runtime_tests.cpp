#include <string>

#include "../core/js_runtime.hpp"
#include "test.hpp"

namespace {

// Evaluates `code` and returns its completion value as text, or "ERROR: ..." when it throws.
std::string EvalText(amk::js::Runtime& runtime, const std::string& code) {
    std::string error;
    JSValue result = JS_UNDEFINED;
    if (!runtime.Eval(code, "test.js", &error, &result)) return "ERROR: " + error;
    std::string text = amk::js::ToUtf8(runtime.context(), result, "not a string");
    if (JS_IsNumber(result)) {
        int32_t number = 0;
        JS_ToInt32(runtime.context(), &number, result);
        text = std::to_string(number);
    }
    runtime.Free(result);
    return text;
}

}  // namespace

TEST_CASE(Runtime_EvalReturnsTheCompletionValue) {
    amk::js::Runtime runtime;
    CHECK(EvalText(runtime, "2 + 3") == "5");
    CHECK(EvalText(runtime, "'a' + 'b'") == "ab");
}

TEST_CASE(Runtime_DescribesExceptionsInOneLine) {
    amk::js::Runtime runtime;
    std::string text = EvalText(runtime, "null.x");
    CHECK(text.find("ERROR: TypeError") == 0);
    CHECK(text.find('\n') == std::string::npos);
}

TEST_CASE(Runtime_CutsAnEntryThatRunsTooLong) {
    amk::js::Limits limits;
    limits.budget = std::chrono::milliseconds(50);
    amk::js::Runtime runtime(limits);
    std::string error;
    CHECK(!runtime.Eval("while (true) {}", "loop.js", &error));
    CHECK(error.find("budget") != std::string::npos);

    // The runtime is still usable afterwards: each entry gets a fresh budget.
    CHECK(EvalText(runtime, "1 + 1") == "2");
}

TEST_CASE(Runtime_StopsAHeapThatGrowsTooMuch) {
    amk::js::Limits limits;
    limits.memoryBytes = size_t{1} << 20;
    amk::js::Runtime runtime(limits);
    CHECK(EvalText(runtime, "var a = []; for (;;) a.push('x'.repeat(10000)); 0").find("ERROR:") == 0);
}

TEST_CASE(Runtime_TurnsDeepRecursionIntoAnError) {
    amk::js::Limits limits;
    limits.stackBytes = size_t{64} << 10;
    amk::js::Runtime runtime(limits);
    CHECK(EvalText(runtime, "function f() { return f() + 1; } f()").find("ERROR:") == 0);
}

TEST_CASE(Runtime_RunsPromiseJobsOnRequest) {
    amk::js::Runtime runtime;
    CHECK(EvalText(runtime, "globalThis.x = 0; Promise.resolve(41).then((v) => { globalThis.x = v + 1; }); 0") == "0");
    std::string error;
    CHECK(runtime.RunJobs(&error));
    CHECK(EvalText(runtime, "x") == "42");
}

TEST_CASE(Runtime_HasNoFileNetworkOrProcessGlobals) {
    amk::js::Runtime runtime;
    CHECK(EvalText(runtime, "[typeof std, typeof os, typeof require, typeof process, typeof XMLHttpRequest].join()") ==
          "undefined,undefined,undefined,undefined,undefined");
}
