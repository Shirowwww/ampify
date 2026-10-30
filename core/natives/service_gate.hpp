#pragma once

// For the natives of the kit services (discord, tray, midi, remote): the setting check, with one journal line per
// element and service when the service is off, so that an author sees why nothing happens.

#include <set>
#include <string>

#include "../api/bridge.hpp"
#include "kit_language.hpp"
#include "service_flags.hpp"

namespace amk {

struct ServiceGate : ScriptBridge {
    struct Warned {
        std::set<std::string> keys;
    };

    // True when `key` (such as "discord.enabled") is on in the settings.
    static bool Open(ScriptExtension* ext, const std::string& key, const std::string& service) {
        if (ServiceEnabled(HostOf(ext), key)) return true;
        if (State<Warned>(ext).keys.insert(key).second) {
            JournalOf(ext).Write("[" + ext->id() + "] " +
                                 i18n::Text(KitLanguage(HostOf(ext)), "service.off",
                                            {{"service", service}, {"setting", key}}));
        }
        return false;
    }
};

}  // namespace amk
