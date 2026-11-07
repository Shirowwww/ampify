#pragma once

// The settings that switch the kit services on: discord.enabled, tray.enabled, remote.enabled, midi.enabled
// (settings_catalog.hpp). They are all off by default, so a service starts only when the user turned it on.

#include <optional>
#include <string>

#include "../host.hpp"
#include "../settings_catalog.hpp"

namespace ampify {

inline bool ServiceEnabled(Host& host, const std::string& key) {
    std::optional<std::string> value = host.ReadSetting(key);
    return value && ParseToggle(*value).value_or(false);
}

}  // namespace ampify
