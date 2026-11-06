#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "release_facts.hpp"

namespace ampify::cli {

// A key of settings.txt that Ampify reads, and what applies while it is not set. `fallback` is what
// `ampify config <key>` prints then; `note` says it in words when the value alone would be empty.
struct KnownSetting {
    std::string_view key;
    std::string_view fallback;
    std::string_view note;
};

// The fixed key, or for a key under a prefix (extensions.<id>.enabled, themes.<id>.<setting>...) its family.
std::optional<KnownSetting> FindSetting(std::string_view key);

// The fixed keys, comma separated, and the prefixes, for the message that refuses an unknown key.
std::string KnownSettingNames();

}  // namespace ampify::cli
