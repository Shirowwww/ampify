#include "js_runtime.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace ampify::js {

namespace {

constexpr size_t kMaxModuleDepth = 16;

bool IsModuleSegment(std::string_view segment) {
    if (segment.empty() || segment.size() > 64 || segment.front() == '.') return false;
    return std::all_of(segment.begin(), segment.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
               c == '-';
    });
}

bool EndsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::vector<std::string_view> Split(std::string_view text) {
    std::vector<std::string_view> parts;
    while (true) {
        size_t slash = text.find('/');
        parts.push_back(text.substr(0, slash));
        if (slash == std::string_view::npos) break;
        text.remove_prefix(slash + 1);
    }
    return parts;
}

}  // namespace

std::optional<std::string> ResolveModuleName(std::string_view base, std::string_view specifier, std::string* reason) {
    auto refuse = [&](const std::string& why) -> std::optional<std::string> {
        if (reason) *reason = why;
        return std::nullopt;
    };
    if (specifier.substr(0, 2) != "./" && specifier.substr(0, 3) != "../") {
        return refuse("only a path relative to the file (./ or ../) is accepted: " + std::string(specifier));
    }

    // The folder of the importing module, as segments. Its own name is dropped.
    std::vector<std::string> path;
    if (!base.empty()) {
        for (std::string_view segment : Split(base)) path.emplace_back(segment);
        path.pop_back();
    }

    for (std::string_view segment : Split(specifier)) {
        if (segment == ".") continue;
        if (segment == "..") {
            if (path.empty()) return refuse("the path leaves the element folder: " + std::string(specifier));
            path.pop_back();
            continue;
        }
        if (!IsModuleSegment(segment)) {
            return refuse("file name refused (letters, digits, . _ - only): " + std::string(specifier));
        }
        path.emplace_back(segment);
    }
    if (path.empty() || path.size() > kMaxModuleDepth) return refuse("path refused: " + std::string(specifier));
    if (!EndsWith(path.back(), ".js") && !EndsWith(path.back(), ".mjs")) {
        return refuse("only .js and .mjs files can be imported: " + std::string(specifier));
    }

    std::string name;
    for (const std::string& segment : path) name += (name.empty() ? "" : "/") + segment;
    return name;
}

Runtime::Runtime(Limits limits) : m_limits(limits) {
    m_rt = JS_NewRuntime();
    JS_SetMemoryLimit(m_rt, m_limits.memoryBytes);
    JS_SetMaxStackSize(m_rt, m_limits.stackBytes);
    JS_SetRuntimeOpaque(m_rt, this);
    JS_SetInterruptHandler(m_rt, &Runtime::InterruptHandler, this);
    JS_SetModuleLoaderFunc(m_rt, &Runtime::NormalizeModule, &Runtime::LoadModule, this);
    m_ctx = JS_NewContext(m_rt);
}

Runtime::~Runtime() {
    if (m_ctx) JS_FreeContext(m_ctx);
    if (m_rt) JS_FreeRuntime(m_rt);
}

void Runtime::StartBudget() { m_deadline = std::chrono::steady_clock::now() + m_limits.budget; }

int Runtime::InterruptHandler(JSContext* ctx, void* opaque) {
    (void)ctx;
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

bool Runtime::EvalModule(std::string_view source, const std::string& name, std::string* error) {
    StartBudget();
    JSValue promise = JS_Eval(m_ctx, source.data(), source.size(), name.c_str(), JS_EVAL_TYPE_MODULE);
    if (JS_IsException(promise)) {
        if (error) *error = TakeException(m_ctx);
        JS_ResetUncatchableError(m_ctx);
        return false;
    }
    bool ok = RunJobs(error);
    // An exception in the module body, or in a top-level await, rejects the promise of its evaluation.
    if (ok && JS_PromiseState(m_ctx, promise) == JS_PROMISE_REJECTED) {
        JS_Throw(m_ctx, JS_PromiseResult(m_ctx, promise));
        if (error) *error = TakeException(m_ctx);
        JS_ResetUncatchableError(m_ctx);
        ok = false;
    }
    JS_FreeValue(m_ctx, promise);
    return ok;
}

char* Runtime::NormalizeModule(JSContext* ctx, const char* base, const char* name, void* opaque) {
    (void)opaque;
    std::string reason;
    std::optional<std::string> resolved = ResolveModuleName(base ? base : "", name ? name : "", &reason);
    if (!resolved) {
        JS_ThrowReferenceError(ctx, "import refused: %s", reason.c_str());
        return nullptr;
    }
    return js_strdup(ctx, resolved->c_str());
}

JSModuleDef* Runtime::LoadModule(JSContext* ctx, const char* name, void* opaque) {
    auto* self = static_cast<Runtime*>(opaque);
    if (self->m_moduleRoot.empty()) {
        JS_ThrowReferenceError(ctx, "import refused: no element folder here");
        return nullptr;
    }
    std::string reason;
    std::optional<std::string> source = ReadModuleFile(self->m_moduleRoot, name, &reason);
    if (!source) {
        JS_ThrowReferenceError(ctx, "import refused: %s", reason.c_str());
        return nullptr;
    }
    JSValue compiled =
        JS_Eval(ctx, source->data(), source->size(), name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled)) return nullptr;
    auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
    JS_FreeValue(ctx, compiled);
    return module;
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

std::optional<std::string> ReadModuleFile(const std::filesystem::path& root, const std::string& name,
                                          std::string* reason) {
    namespace fs = std::filesystem;
    auto refuse = [&](const std::string& why) -> std::optional<std::string> {
        if (reason) *reason = why;
        return std::nullopt;
    };

    // Each step below the root must be a plain folder or file: a symbolic link could point anywhere on the disk.
    fs::path path = root;
    std::error_code ec;
    for (std::string_view segment : Split(name)) {
        path /= std::string(segment);
        if (fs::is_symlink(fs::symlink_status(path, ec))) return refuse("symbolic link refused: " + name);
    }
    if (!fs::is_regular_file(fs::status(path, ec)) || ec) {
        return refuse("file not found in the element folder: " + name);
    }

    // A link that the check above cannot see (a junction on Windows) still resolves out of the folder.
    std::error_code rootError;
    std::error_code pathError;
    fs::path realRoot = fs::weakly_canonical(root, rootError);
    fs::path realPath = fs::weakly_canonical(path, pathError);
    if (!rootError && !pathError) {
        fs::path relative = realPath.lexically_relative(realRoot);
        if (relative.empty() || *relative.begin() == "..") return refuse("the file leaves the element folder: " + name);
    }

    uintmax_t size = fs::file_size(path, ec);
    if (ec || size > kMaxModuleBytes) return refuse("file too big for a module (1 MB at most): " + name);
    std::ifstream file(path, std::ios::binary);
    if (!file) return refuse("file unreadable: " + name);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
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
        line = text ? text : "unknown exception";
        if (text) JS_FreeCString(ctx, text);
    }

    if (line.find("interrupted") != std::string::npos) line = "time budget exceeded, call interrupted";
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

}  // namespace ampify::js
