// Natives of the module kit (src/core/prelude/kit.cpp). Permission: kit.manage, which only the Marketplace that ships
// with the kit holds (src/cli/catalog.cpp refuses it in the catalogue).

#include "kit.hpp"

#include <array>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../json.hpp"
#include "../kit_layout.hpp"

namespace ampify {

bool IsKitCommandAllowed(const std::vector<std::string>& arguments) {
    static constexpr std::array<std::string_view, 10> kCommands = {
        "market", "install", "remove", "update", "theme", "extension", "app", "snippet", "list", "settings"};
    if (arguments.empty() || arguments.size() > 8) return false;
    bool known = false;
    for (std::string_view command : kCommands) known = known || arguments.front() == command;
    if (!known) return false;
    // `update` alone updates the kit itself; with an id it updates one element, which is all the page needs.
    if (arguments.front() == "update" && (arguments.size() < 2 || arguments[1].starts_with("-"))) return false;
    for (const std::string& argument : arguments) {
        if (argument.empty() || argument.size() > 200) return false;
        if (argument.starts_with("--") && argument != "--yes" && argument != "--json" && argument != "--scheme") {
            return false;  // --kit-dir, --catalog, --install-dir... stay out of a page's reach
        }
    }
    return true;
}

namespace natives {

namespace {

struct KitNatives : ScriptBridge {
    static void Report(ScriptExtension* ext, double id, int code, const std::string& out, const std::string& err) {
        ext->DeliverEvent("kitDone", Json(Json::Object{{"id", id}, {"code", static_cast<double>(code)},
                                                       {"out", out}, {"err", err}})
                                         .Dump());
    }

    AMPIFY_NATIVE(Run) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "kit.manage", &failure)) return failure;
        if (!IsFirstParty(ext->id())) {
            return Throw(ctx, "PermissionDenied", "kit.manage is reserved for the Marketplace that ships with Ampify");
        }
        double id = 0;
        if (argc < 2 || JS_ToFloat64(ctx, &id, argv[0]) != 0) return Throw(ctx, "TypeError", "kit.run: invalid call");
        std::optional<Json> parsed = Json::Parse(String(ctx, argc, argv, 1).value_or(""));
        std::vector<std::string> arguments;
        if (parsed && parsed->IsArray()) {
            for (const Json& item : *parsed->AsArray()) {
                if (const std::string* text = item.AsString()) arguments.push_back(*text);
            }
        }
        if (!IsKitCommandAllowed(arguments)) {
            return Throw(ctx, "PermissionDenied", "kit.run: command refused for a page");
        }
        KitCommandHost* host = HostOf(ext).Capability<KitCommandHost>();
        if (!host) {
            Report(ext, id, -1, "", "Ampify commands are not available in this process");
            return JS_UNDEFINED;
        }
        JournalOf(ext).Write("[" + ext->id() + "] ampify " + arguments.front() + (arguments.size() > 1 ? " " + arguments[1] : ""));
        host->RunKitCommand(ext->id(), arguments, [ext, id](int code, const std::string& out, const std::string& err) {
            Report(ext, id, code, out, err);
        });
        return JS_UNDEFINED;
    }

    static void Stop(ScriptExtension& extension) {
        if (KitCommandHost* host = HostOf(&extension).Capability<KitCommandHost>()) host->Forget(extension.id());
    }
};

}  // namespace

void DefineKit(api::NativeScope& scope) {
    scope.Function("kitRun", KitNatives::Run, 2);
}

void StopKit(ScriptExtension& extension) {
    KitNatives::Stop(extension);
}

}  // namespace natives

}  // namespace ampify
