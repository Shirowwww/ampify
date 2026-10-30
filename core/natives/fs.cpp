// Natives of the module fs (core/prelude/fs.cpp). Permission: fs.output. The only write an element can make:
// a text file in <kit>\output\<id>\ (rules in core/output_files.hpp).

#include "fs.hpp"

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../output_files.hpp"

namespace amk::natives {

namespace {

struct FsNatives : ScriptBridge {
    // native.fsWrite(name, text): true once written. A refused name or size throws; a disk failure returns false
    // with a journal line, so that a file held open by another program does not stop the element.
    AMK_NATIVE(Write) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "fs.output", &failure)) return failure;
        std::string name = String(ctx, argc, argv, 0).value_or("");
        std::string text = String(ctx, argc, argv, 1).value_or("");
        if (!IsValidOutputName(name)) {
            return Throw(ctx, "TypeError", "fs.writeText : nom refusé « " + name +
                                               " » (lettres, chiffres, . _ -, et .txt, .json, .csv, .html ou .xml)");
        }
        if (text.size() > kMaxOutputFileBytes) return Throw(ctx, "RangeError", "fs.writeText : 256 Ko au plus");
        OutputFilesHost* files = HostOf(ext).Capability<OutputFilesHost>();
        if (!files) return JS_FALSE;
        std::string error;
        if (!files->WriteOutputText(ext->id(), name, text, &error)) {
            JournalOf(ext).Write("[" + ext->id() + "] fichier de sortie " + name + " non écrit : " + error);
            return JS_FALSE;
        }
        return JS_TRUE;
    }
};

}  // namespace

void DefineFs(api::NativeScope& scope) {
    scope.Function("fsWrite", FsNatives::Write, 2);
}

}  // namespace amk::natives
