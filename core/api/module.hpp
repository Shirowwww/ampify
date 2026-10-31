#pragma once

// The Ampify object is made of modules. A module is a fragment of the prelude (core/prelude/<module>.cpp), the
// natives that fragment calls (core/natives/<module>.cpp) and, when it needs them, start and stop hooks. The list,
// in prelude order, is in core/api/modules.cpp. Contributor guide: CONTRIBUTING.md, "Add an API module".

#include <span>
#include <string>

#include "../js_runtime.hpp"

namespace ampify {

class ScriptExtension;

namespace api {

// Fills the `native` object that the prelude receives. Its fields are the only way down to the kernel, and each
// name must be unique across the modules. Used on the JavaScript thread, while the extension starts.
class NativeScope {
public:
    NativeScope(JSContext* ctx, JSValue natives, ScriptExtension& extension)
        : m_ctx(ctx), m_natives(natives), m_extension(extension) {}

    // native.<name>: a C function that expects `length` arguments.
    void Function(const char* name, JSCFunction* fn, int length) {
        JS_SetPropertyStr(m_ctx, m_natives, name, JS_NewCFunction(m_ctx, fn, name, length));
    }

    // native.<name>: a constant string.
    void String(const char* name, const std::string& value) {
        JS_SetPropertyStr(m_ctx, m_natives, name, JS_NewStringLen(m_ctx, value.data(), value.size()));
    }

    ScriptExtension& extension() const { return m_extension; }

private:
    JSContext* m_ctx;
    JSValue m_natives;
    ScriptExtension& m_extension;
};

struct ApiModule {
    const char* name;

    // JavaScript run inside the prelude, in its own block, after the modules listed before it. It sees `native`,
    // `Ampify`, `dispatch` and the shared helpers of core/prelude/frame.cpp. Null when the module has none.
    const char* prelude;

    // Adds the natives that the fragment calls. Null when the module has none.
    void (*defineNatives)(NativeScope& scope);

    // Called by ScriptExtension::Start before the runtime exists, in list order. Null when not needed.
    void (*start)(ScriptExtension& extension);

    // Called by ScriptExtension::Stop after the onStop handlers ran, in list order: releases what the element
    // holds in the host (shortcuts, gain, interface). Null when not needed.
    void (*stop)(ScriptExtension& extension);
};

// Every module, in prelude order.
std::span<const ApiModule> Modules();

// The whole prelude: a function expression that takes the natives and returns the dispatch object. Built once.
const std::string& Prelude();

// The `native` object passed to the prelude, with the natives of every module. The caller frees it.
JSValue MakeNatives(JSContext* ctx, ScriptExtension& extension);

}  // namespace api
}  // namespace ampify
