#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

#include <quickjs.h>

namespace amk::js {

struct Limits {
    size_t memoryBytes = size_t{32} << 20;  // heap of one extension
    size_t stackBytes = size_t{512} << 10;  // recursion depth of one extension
    std::chrono::milliseconds budget{2000};  // longest single entry (Eval, RunJobs, a callback) before it is cut
};

// One QuickJS runtime and context, owned by one extension. QuickJS is not thread-safe: every call must come
// from the thread that owns the runtime. The sandbox is the default QuickJS global object: no `std` or `os`
// module is loaded, so there is no file, network or process access except what the kit adds.
class Runtime {
public:
    explicit Runtime(Limits limits = {});
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    JSContext* context() const { return m_ctx; }

    // Runs a classic script. On an exception or a cut budget, `error` gets one line and false is returned.
    // When `result` is given, it receives the completion value, which the caller must Free.
    bool Eval(std::string_view source, const std::string& name, std::string* error, JSValue* result = nullptr);

    // Runs the queued promise jobs until none is left. Returns false if a job threw.
    bool RunJobs(std::string* error);

    // Calls `fn` with `argc` arguments. Returns false on exception or cut budget. `result` is owned by the caller.
    bool Call(JSValueConst fn, int argc, JSValueConst* argv, JSValue* result, std::string* error);

    // Frees a value owned by this runtime. Use it instead of JS_FreeValue, which needs the context.
    void Free(JSValue value);

    // The owner's pointer, reachable from native callbacks through FromContext.
    void SetUserData(void* data) { m_userData = data; }
    void* UserData() const { return m_userData; }
    static Runtime* FromContext(JSContext* ctx);

private:
    void StartBudget();
    static int InterruptHandler(JSRuntime* rt, void* opaque);

    JSRuntime* m_rt = nullptr;
    JSContext* m_ctx = nullptr;
    Limits m_limits;
    std::chrono::steady_clock::time_point m_deadline;
    void* m_userData = nullptr;
};

// Describes the pending exception of `ctx` in one line, such as "TypeError: x is not a function", and clears it.
std::string TakeException(JSContext* ctx);

// Converts a JS string value to UTF-8. Returns `fallback` if the value is not a string.
std::string ToUtf8(JSContext* ctx, JSValueConst value, const std::string& fallback = {});

// Creates an Error object with a custom `name`, so that the script can tell PermissionDenied from other failures.
JSValue NewNamedError(JSContext* ctx, const char* name, const std::string& message);

}  // namespace amk::js
