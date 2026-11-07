// Natives of the module tray (src/core/prelude/tray.cpp). Permission: ui.tray. Setting: tray.enabled.

#include "tray.hpp"

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../json.hpp"
#include "service_gate.hpp"

namespace ampify::natives {

namespace {

// Cuts a label to kMaxTrayLabel bytes, on a character boundary, and drops control characters.
std::string CleanLabel(const std::string& label) {
    std::string out;
    for (char c : label) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) out.push_back(c);
    }
    if (out.size() > kMaxTrayLabel) {
        size_t end = kMaxTrayLabel;
        while (end > 0 && (static_cast<unsigned char>(out[end]) & 0xC0) == 0x80) end--;
        out.resize(end);
    }
    return out;
}

struct TrayNatives : ScriptBridge {
    // native.traySetMenu(itemsJson): [{id, label, separator, checked, enabled}], built by the prelude.
    AMPIFY_NATIVE(SetMenu) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.tray", &failure)) return failure;
        std::optional<Json> value = Json::Parse(String(ctx, argc, argv, 0).value_or(""));
        const Json::Array* list = value ? value->AsArray() : nullptr;
        if (!list) return Throw(ctx, "TypeError", "tray.setMenu expects a list");
        if (list->size() > kMaxTrayItems) return Throw(ctx, "RangeError", "tray.setMenu: 20 entries at most");

        std::vector<TrayItem> items;
        for (const Json& entry : *list) {
            TrayItem item;
            const Json* id = entry.Find("id");
            item.id = id && id->AsNumber() ? static_cast<int>(*id->AsNumber()) : 0;
            const Json* separator = entry.Find("separator");
            item.separator = separator && separator->AsBool() && *separator->AsBool();
            if (!item.separator) {
                const Json* label = entry.Find("label");
                item.label = CleanLabel(label && label->AsString() ? *label->AsString() : "");
                if (item.label.empty()) return Throw(ctx, "TypeError", "tray.setMenu: each entry expects a label");
                const Json* checked = entry.Find("checked");
                item.checked = checked && checked->AsBool() && *checked->AsBool();
                const Json* enabled = entry.Find("enabled");
                item.enabled = !(enabled && enabled->AsBool() && !*enabled->AsBool());
            }
            items.push_back(std::move(item));
        }

        if (!ServiceGate::Open(ext, kTrayKey, "Notification area icon")) return JS_FALSE;
        TrayHost* tray = HostOf(ext).Capability<TrayHost>();
        if (!tray) return JS_FALSE;
        return JS_NewBool(ctx, tray->SetTrayMenu(ext->id(), items));
    }

    // native.trayListen(): checks the permission before Ampify.tray.onClick keeps its function.
    AMPIFY_NATIVE(Listen) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.tray", &failure)) return failure;
        return JS_NewBool(ctx, ServiceGate::Open(ext, kTrayKey, "Notification area icon") &&
                                   HostOf(ext).Capability<TrayHost>() != nullptr);
    }
};

}  // namespace

void DefineTray(api::NativeScope& scope) {
    scope.Function("traySetMenu", TrayNatives::SetMenu, 1);
    scope.Function("trayListen", TrayNatives::Listen, 0);
    scope.String("trayOwner", scope.extension().id());
}

void StopTray(ScriptExtension& extension) {
    if (!extension.manifest().HasPermission("ui.tray")) return;
    if (TrayHost* tray = ScriptBridge::HostOf(&extension).Capability<TrayHost>()) tray->RemoveTrayMenu(extension.id());
}

}  // namespace ampify::natives
