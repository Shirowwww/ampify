#pragma once

// For the natives of the Ampify services (discord, tray, midi, remote): the setting check, with one log line per
// element and service when the service is off, so that an author sees why nothing happens.

#include <set>
#include <string>

#include "../api/bridge.hpp"
#include "../i18n.hpp"
#include "service_flags.hpp"

namespace ampify {

struct ServiceGate : ScriptBridge {
    struct Warned {
        std::set<std::string> keys;
    };

    // True when `key` (such as "discord.enabled") is on in the settings.
    static bool Open(ScriptExtension* ext, const std::string& key, const std::string& service) {
        if (ServiceEnabled(HostOf(ext), key)) return true;
        if (State<Warned>(ext).keys.insert(key).second) {
            JournalOf(ext).Write("[" + ext->id() + "] " +
                                 i18n::Text("service.off", {{"service", service}, {"setting", key}}));
        }
        return false;
    }
};

}  // namespace ampify
