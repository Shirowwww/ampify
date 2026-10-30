// The list of the modules that make the AMK object, and the assembly of the prelude from their fragments.
//
// To add a module `foo`: write core/prelude/foo.cpp (kFoo, the JavaScript) and, when it needs the kernel,
// core/natives/foo.cpp (DefineFoo, plus StartFoo or StopFoo when needed); declare them below; add one line to
// kModules. Both files go in cmake/sources/<package>.cmake. Guide: docs/extensions/API.md, "Organisation du code".

#include "module.hpp"

#include "../script_extension.hpp"

namespace amk {

namespace prelude {
extern const char kFrameHead[];
extern const char kFrameTail[];
extern const char kCore[];
extern const char kConfig[];
extern const char kStorage[];
extern const char kPlayer[];
extern const char kAudio[];
extern const char kKeyboard[];
extern const char kNotify[];
extern const char kHttp[];
extern const char kIcons[];
extern const char kUri[];
extern const char kTimers[];
extern const char kConsole[];
extern const char kUi[];
extern const char kAudioAnalysis[];
extern const char kColor[];
extern const char kReact[];
extern const char kCrypto[];
extern const char kI18n[];
extern const char kDiscord[];
extern const char kTray[];
extern const char kMidi[];
extern const char kRemote[];
extern const char kFs[];
}  // namespace prelude

namespace natives {
void DefineCore(api::NativeScope& scope);
void DefineConfig(api::NativeScope& scope);
void DefineStorage(api::NativeScope& scope);
void StartStorage(ScriptExtension& extension);
void DefinePlayer(api::NativeScope& scope);
void DefineAudio(api::NativeScope& scope);
void StopAudio(ScriptExtension& extension);
void DefineKeyboard(api::NativeScope& scope);
void StopKeyboard(ScriptExtension& extension);
void DefineNotify(api::NativeScope& scope);
void DefineHttp(api::NativeScope& scope);
void DefineTimers(api::NativeScope& scope);
void DefineUi(api::NativeScope& scope);
void StopUi(ScriptExtension& extension);
void DefineAudioAnalysis(api::NativeScope& scope);
void StopAudioAnalysis(ScriptExtension& extension);
void DefineColor(api::NativeScope& scope);
void DefineReact(api::NativeScope& scope);
void DefineCrypto(api::NativeScope& scope);
void DefineI18n(api::NativeScope& scope);
void DefineDiscord(api::NativeScope& scope);
void StopDiscord(ScriptExtension& extension);
void DefineTray(api::NativeScope& scope);
void StopTray(ScriptExtension& extension);
void DefineMidi(api::NativeScope& scope);
void StopMidi(ScriptExtension& extension);
void DefineRemote(api::NativeScope& scope);
void StopRemote(ScriptExtension& extension);
void DefineFs(api::NativeScope& scope);
}  // namespace natives

namespace prelude { extern const char kLink[]; }
namespace natives { void DefineLink(api::NativeScope& scope); void StartLink(ScriptExtension& extension); }
namespace prelude { extern const char kKit[]; }
namespace natives { void DefineKit(api::NativeScope& scope); void StopKit(ScriptExtension& extension); }

namespace api {

namespace {

// Prelude order: a module sees what the modules above it added, and AMK lists its members in this order.
// Columns: name, prelude fragment, natives, start hook, stop hook.
const ApiModule kModules[] = {
    {"core", prelude::kCore, natives::DefineCore, nullptr, nullptr},
    {"config", prelude::kConfig, natives::DefineConfig, nullptr, nullptr},
    {"storage", prelude::kStorage, natives::DefineStorage, natives::StartStorage, nullptr},
    {"player", prelude::kPlayer, natives::DefinePlayer, nullptr, nullptr},
    {"audio", prelude::kAudio, natives::DefineAudio, nullptr, natives::StopAudio},
    {"audioAnalysis", prelude::kAudioAnalysis, natives::DefineAudioAnalysis, nullptr, natives::StopAudioAnalysis},
    {"keyboard", prelude::kKeyboard, natives::DefineKeyboard, nullptr, natives::StopKeyboard},
    {"notify", prelude::kNotify, natives::DefineNotify, nullptr, nullptr},
    {"http", prelude::kHttp, natives::DefineHttp, nullptr, nullptr},
    {"icons", prelude::kIcons, nullptr, nullptr, nullptr},
    {"uri", prelude::kUri, nullptr, nullptr, nullptr},
    {"color", prelude::kColor, natives::DefineColor, nullptr, nullptr},
    {"timers", prelude::kTimers, natives::DefineTimers, nullptr, nullptr},
    {"console", prelude::kConsole, nullptr, nullptr, nullptr},
    {"ui", prelude::kUi, natives::DefineUi, nullptr, natives::StopUi},
    {"link", prelude::kLink, natives::DefineLink, natives::StartLink, nullptr},
    {"react", prelude::kReact, natives::DefineReact, nullptr, nullptr},
    {"crypto", prelude::kCrypto, natives::DefineCrypto, nullptr, nullptr},
    {"i18n", prelude::kI18n, natives::DefineI18n, nullptr, nullptr},
    {"discord", prelude::kDiscord, natives::DefineDiscord, nullptr, natives::StopDiscord},
    {"tray", prelude::kTray, natives::DefineTray, nullptr, natives::StopTray},
    {"midi", prelude::kMidi, natives::DefineMidi, nullptr, natives::StopMidi},
    {"remote", prelude::kRemote, natives::DefineRemote, nullptr, natives::StopRemote},
    {"fs", prelude::kFs, natives::DefineFs, nullptr, nullptr},
    {"kit", prelude::kKit, natives::DefineKit, nullptr, natives::StopKit},
};

}  // namespace

std::span<const ApiModule> Modules() { return kModules; }

const std::string& Prelude() {
    static const std::string text = [] {
        std::string out = prelude::kFrameHead;
        for (const ApiModule& module : kModules) {
            if (!module.prelude) continue;
            out += "\n  // ---- Module ";
            out += module.name;
            out += "\n  {";
            out += module.prelude;
            out += "  }\n";
        }
        out += prelude::kFrameTail;
        return out;
    }();
    return text;
}

JSValue MakeNatives(JSContext* ctx, ScriptExtension& extension) {
    JSValue natives = JS_NewObject(ctx);
    NativeScope scope(ctx, natives, extension);
    for (const ApiModule& module : kModules) {
        if (module.defineNatives) module.defineNatives(scope);
    }
    return natives;
}

}  // namespace api
}  // namespace amk
