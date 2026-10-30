#include "js_runtime.hpp"

namespace amk::js {

Runtime::Runtime(Limits limits) : m_limits(limits) {
    m_rt = JS_NewRuntime();
    JS_SetMemoryLimit(m_rt, m_limits.memoryBytes);
    JS_SetMaxStackSize(m_rt, m_limits.stackBytes);
    JS_SetRuntimeOpaque(m_rt, this);
    JS_SetInterruptHandler(m_rt, &Runtime::InterruptHandler, this);
    m_ctx = JS_NewContext(m_rt);
}

Runtime::~Runtime() {
    if (m_ctx) JS_FreeContext(m_ctx);
    if (m_rt) JS_FreeRuntime(m_rt);
}

void Runtime::StartBudget() { m_deadline = std::chrono::steady_clock::now() + m_limits.budget; }

int Runtime::InterruptHandler(JSRuntime* rt, void* opaque) {
    (void)rt;
    auto* self = static_cast<Runtime*>(opaque);
    return std::chrono::steady_clock::now() > self->m_deadline ? 1 : 0;
}

bool Runtime::Eval(std::string_view source, const std::string& name, std::string* error, JSValue* result) {
    StartBudget();
    JSValue value = JS_Eval(m_ctx, source.data(), source.size(), name.c_str(), JS_EVAL_TYPE_GLOBAL);
    bool ok = !JS_IsException(value);
    if (!ok) {
        if (error) *error = TakeException(m_ctx);
        JS_ResetUncatchableError(m_ctx);
    }
    if (ok && result) {
        *result = value;
    } else {
        JS_FreeValue(m_ctx, value);
    }
    return ok;
}

bool Runtime::RunJobs(std::string* error) {
    StartBudget();
    while (true) {
        JSContext* pending = nullptr;
        int status = JS_ExecutePendingJob(m_rt, &pending);
        if (status == 0) return true;
        if (status < 0) {
            if (error) *error = TakeException(pending ? pending : m_ctx);
            return false;
        }
    }
}

bool Runtime::Call(JSValueConst fn, int argc, JSValueConst* argv, JSValue* result, std::string* error) {
    StartBudget();
    JSValue value = JS_Call(m_ctx, fn, JS_UNDEFINED, argc, argv);
    if (JS_IsException(value)) {
        if (error) *error = TakeException(m_ctx);
        JS_ResetUncatchableError(m_ctx);
        return false;
    }
    if (result) {
        *result = value;
    } else {
        JS_FreeValue(m_ctx, value);
    }
    return true;
}

void Runtime::Free(JSValue value) { JS_FreeValue(m_ctx, value); }

Runtime* Runtime::FromContext(JSContext* ctx) {
    return static_cast<Runtime*>(JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
}

std::string ToUtf8(JSContext* ctx, JSValueConst value, const std::string& fallback) {
    if (!JS_IsString(value)) return fallback;
    const char* text = JS_ToCString(ctx, value);
    if (!text) return fallback;
    std::string out(text);
    JS_FreeCString(ctx, text);
    return out;
}

std::string TakeException(JSContext* ctx) {
    JSValue exception = JS_GetException(ctx);
    std::string line;

    if (JS_IsError(exception)) {
        JSValue nameValue = JS_GetPropertyStr(ctx, exception, "name");
        JSValue messageValue = JS_GetPropertyStr(ctx, exception, "message");
        std::string name = ToUtf8(ctx, nameValue, "Error");
        std::string message = ToUtf8(ctx, messageValue);
        line = name + (message.empty() ? "" : ": " + message);
        JS_FreeValue(ctx, nameValue);
        JS_FreeValue(ctx, messageValue);
    } else {
        // A thrown string or number, or an uncatchable error that the budget handler raised.
        const char* text = JS_ToCString(ctx, exception);
        line = text ? text : "exception inconnue";
        if (text) JS_FreeCString(ctx, text);
    }

    if (line.find("interrupted") != std::string::npos) line = "budget de temps dépassé, appel interrompu";
    JS_FreeValue(ctx, exception);
    return line;
}

JSValue NewNamedError(JSContext* ctx, const char* name, const std::string& message) {
    JSValue error = JS_NewError(ctx);
    JS_DefinePropertyValueStr(ctx, error, "name", JS_NewString(ctx, name), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, error, "message", JS_NewStringLen(ctx, message.data(), message.size()),
                              JS_PROP_C_W_E);
    return error;
}

}  // namespace amk::js
