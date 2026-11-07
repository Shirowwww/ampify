#pragma once

// The settings that switch the kit services on: discord.enabled, tray.enabled, remote.enabled, midi.enabled. They
// are all off by default, so a service starts only when the user turned it on (ampify config <key> 1).

#include <optional>
#include <string>
#include <string_view>

#include "../host.hpp"
#include "../permissions.hpp"

namespace ampify {

// 1, true, on, yes (any case) are on. Anything else, and an absent key, is off.
inline bool IsOnText(std::string_view value) {
    std::string lowered;
    for (char c : value) {
        if (c != ' ' && c != '\t') lowered.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    }
    return lowered == "1" || lowered == "true" || lowered == "on" || lowered == "yes";
}

inline bool ServiceEnabled(Host& host, const std::string& key) {
    std::optional<std::string> value = host.ReadSetting(key);
    return value && IsOnText(*value);
}

}  // namespace ampify
