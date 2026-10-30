#pragma once

// The language of the kit's own texts, from the setting `language` (fr, en, auto) and the host locale.

#include <optional>
#include <string>

#include "../host.hpp"
#include "../i18n.hpp"

namespace amk {

inline i18n::Language KitLanguage(Host& host) {
    std::optional<std::string> setting = host.ReadSetting(std::string(i18n::kLanguageSetting));
    return i18n::ResolveLanguage(setting.value_or("auto"), host.Locale());
}

}  // namespace amk
