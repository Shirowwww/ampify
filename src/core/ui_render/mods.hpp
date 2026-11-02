#pragma once

// The "Ampify" card that Ampify adds to the Playback page of Apple Music's settings: the list
// of installed extensions, and what a toggle of that card does. Portable, so the file handling is tested under Linux;
// the card itself is built by src/win/ui_service.cpp.

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../manifest.hpp"

namespace ampify::uirender {

struct ModEntry {
    std::string id;
    std::string name;         // manifest name, or the id when the manifest has none
    std::string description;  // may be empty
    std::map<std::string, ManifestText> translations;  // name and description by language, from the manifest
    bool enabled = false;     // extensions.<id>.enabled in settings.txt
    std::vector<SettingSpec> settings;  // what the manifest declares, in its order
    std::vector<std::string> values;    // the value of each setting now: settings.txt, else the default
    bool theme = false;  // the active theme's settings (SetThemeSetting) rather than an extension's (SetModSetting)

    // The name and description to show in a language: its translation when the manifest has one, else the English text.
    const std::string& NameIn(const std::string& language) const;
    const std::string& DescriptionIn(const std::string& language) const;
};

// The extensions of <ampify>\extensions whose manifest is valid and whose kind is extension, sorted by name. Folders that
// do not hold a valid manifest are left out: the kernel would not load them either.
std::vector<ModEntry> ListMods(const std::filesystem::path& kit);

// What a toggle of the card does, as `ampify extension enable|disable` would: writes extensions.<id>.enabled in
// settings.txt (keeping the other lines), then appends `start <id>` or `stop <id>` to reload.txt, which the kernel's
// JavaScript thread reads every 500 ms. False with an English line in `error` when a file cannot be written.
bool SwitchMod(const std::filesystem::path& kit, const std::string& id, bool enabled, std::string& error);

// What a change of a declared setting in the card does: writes extensions.<id>.<key> = value in settings.txt, then
// appends `restart <id>` to reload.txt, so the extension starts again with it. The value is put on one line.
bool SetModSetting(const std::filesystem::path& kit, const std::string& id, const std::string& key,
                   const std::string& value, std::string& error);

// The themes of <ampify>\themes whose manifest is valid, sorted by name, for the card's theme list. Only id, name,
// translations and theme are filled.
std::vector<ModEntry> ListThemes(const std::filesystem::path& kit);

// Makes <id> the active theme, or none when it is empty, as `ampify theme set` does: a scheme belongs to the theme it
// was chosen for, so it goes unless the theme stays the same or the scheme is "auto".
bool ChooseTheme(const std::filesystem::path& kit, const std::string& id, std::string& error);

// The theme in use (theme.active), empty for none.
std::string ActiveThemeId(const std::filesystem::path& kit);

// The active theme (theme.active) and the settings its manifest declares, with their values now. Nothing when no
// theme is active, or when it declares no setting.
std::optional<ModEntry> ActiveThemeSettings(const std::filesystem::path& kit);

// What a change of a theme's setting in the card does: writes themes.<id>.<key> = value in settings.txt. The theme
// service watches that file and applies the theme again, so no reload request is needed.
bool SetThemeSetting(const std::filesystem::path& kit, const std::string& id, const std::string& key,
                     const std::string& value, std::string& error);

}  // namespace ampify::uirender
