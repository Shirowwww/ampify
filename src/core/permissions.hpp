#pragma once

// The permissions a manifest may ask for (docs/extensions/api.md, section 4). schemas/manifest.schema.json lists the
// same names, and schemas/catalog.schema.json all but the firstPartyOnly ones: tests/manifest_tests.cpp reads both
// and compares them to this table.

#include <string_view>

namespace ampify {

struct PermissionInfo {
    std::string_view name;
    std::string_view serviceSetting;  // the kit service it needs on, such as discord.enabled; empty for none
    bool reserved;                    // accepted, but no function uses it yet
    bool firstPartyOnly;              // only an element of kKitElements may hold it, never a download
};

// In the order of their names.
inline constexpr PermissionInfo kPermissions[] = {
    {"audio.analyze", "", false, false},  {"audio.eq", "", false, false},
    {"audio.gain", "", false, false},     {"clipboard.read", "", false, false},
    {"clipboard.write", "", false, false}, {"fs.output", "", false, false},
    {"ipc.discord", "discord.enabled", false, false},
    {"kit.manage", "", false, true},      {"library.read", "", false, false},
    {"library.write", "", true, false},   {"midi.input", "midi.enabled", false, false},
    {"notifications", "", false, false},  {"player.control", "", false, false},
    {"player.read", "", false, false},    {"services.read", "", true, false},
    {"storage", "", false, false},        {"ui.fullscreen", "", false, false},
    {"ui.keyboard", "", false, false},    {"ui.menu", "", false, false},
    {"ui.page", "", false, false},        {"ui.panel", "", false, false},
    {"ui.playbar", "", false, false},     {"ui.popup", "", false, false},
    {"ui.sidebar", "", false, false},     {"ui.topbar", "", false, false},
    {"ui.tray", "tray.enabled", false, false},
    {"ui.window", "", false, false},
};

// network:<host> grants requests to one host, written in lowercase.
inline constexpr std::string_view kNetworkPermissionPrefix = "network:";

// The row of a fixed permission, or null (a network:<host> permission has none).
const PermissionInfo* FindPermission(std::string_view name);

// A name of kPermissions, or network:<host> with a lowercase host name.
bool IsKnownPermission(std::string_view permission);

// The service setting a permission needs, or null. Remote control has no permission: it is the user's alone.
const char* ServiceSettingOf(std::string_view permission);

}  // namespace ampify
