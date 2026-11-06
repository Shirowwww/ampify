// Natives of the module clipboard (src/core/prelude/clipboard.cpp). Permissions: clipboard.write (copy) and
// clipboard.read (paste), apart, because reading gives whatever the user copied last, from any program.

#include "clipboard.hpp"

#include "../api/bridge.hpp"
#include "../api/module.hpp"

namespace ampify::natives {

namespace {

struct ClipboardNatives : ScriptBridge {
    // native.clipboardCopy(text): true once the text is on the clipboard.
    AMPIFY_NATIVE(Copy) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "clipboard.write", &failure)) return failure;
        std::string text = String(ctx, argc, argv, 0).value_or("");
        if (text.size() > kMaxClipboardText) return Throw(ctx, "RangeError", "clipboard.copy: the text is over 1 MB");
        ClipboardHost* host = HostOf(ext).Capability<ClipboardHost>();
        return JS_NewBool(ctx, host && host->CopyText(text));
    }

    // native.clipboardPaste(): the clipboard's text, or null. A longer text than the limit is null too, never cut.
    AMPIFY_NATIVE(Paste) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "clipboard.read", &failure)) return failure;
        ClipboardHost* host = HostOf(ext).Capability<ClipboardHost>();
        std::optional<std::string> text = host ? host->PasteText() : std::nullopt;
        if (!text || text->size() > kMaxClipboardText) return JS_NULL;
        return JS_NewStringLen(ctx, text->data(), text->size());
    }
};

}  // namespace

void DefineClipboard(api::NativeScope& scope) {
    scope.Function("clipboardCopy", ClipboardNatives::Copy, 1);
    scope.Function("clipboardPaste", ClipboardNatives::Paste, 0);
}

}  // namespace ampify::natives
