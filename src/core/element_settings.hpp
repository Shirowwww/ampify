#pragma once

// The settings card of an extension, a theme or a snippet, as portable logic: the groups and the search, the values a
// reset writes, and the file that shares one element's settings. The card itself is in src/win/ui_service_mods.cpp.

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "manifest.hpp"

namespace ampify::settings_panel {

inline constexpr int kFileVersion = 1;
inline constexpr std::string_view kFileFormat = "ampify-settings";
// A bigger file is refused before it is read: a settings file is a few hundred bytes.
inline constexpr size_t kMaxFileBytes = 64 * 1024;

// A heading of the card and the settings under it, as indexes into the manifest's list.
struct SettingGroup {
    std::string name;               // empty for the settings with no group
    std::vector<size_t> settings;   // in manifest order
};

// The settings under their headings: those without a group first, then each group in the order its first setting is
// declared. Without any group, one unnamed group holds them all, so the card looks as it did.
std::vector<SettingGroup> GroupSettings(const std::vector<SettingSpec>& settings);

// The label and description of a setting, in the language the card shows.
struct SettingText {
    std::string label;
    std::string description;
};

// The indexes of the settings that match `query`: each word of it must appear in the label or the description, whatever
// the case. An empty query matches every setting.
std::vector<size_t> MatchingSettings(const std::vector<SettingText>& texts, std::string_view query);

// The value every setting takes when it is reset: its manifest default, by key.
std::map<std::string, std::string> DefaultValues(const std::vector<SettingSpec>& settings);

// The file that shares the settings of one element. `values` are the values as settings.txt holds them, by key; a key
// missing from it takes its default. Each value keeps its type: true or false, a number, or a text.
std::string ExportSettings(std::string_view kind, std::string_view id, const std::vector<SettingSpec>& settings,
                           const std::map<std::string, std::string>& values);

// Reads a file made by ExportSettings for the element `kind` and `id`, and returns the values it holds, as settings.txt
// keeps them. Nothing when the file is refused, with an English reason in `error`: too big, not this format or
// version, made for another element, a key the element does not declare, or a value of the wrong type or outside
// what the setting accepts. A key the file leaves out keeps its value.
std::optional<std::map<std::string, std::string>> ImportSettings(std::string_view text, std::string_view kind,
                                                                 std::string_view id,
                                                                 const std::vector<SettingSpec>& settings,
                                                                 std::string& error);

}  // namespace ampify::settings_panel
