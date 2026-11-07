#pragma once

// The "Ampify" card that Ampify adds to the Playback page of Apple Music's settings: the list
// of installed extensions, and what a toggle of that card does. Portable, so the file handling is tested under Linux;
// the card itself is built by src/win/ui_service_settings.cpp and ui_service_mods.cpp.

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
    ElementKind kind = ElementKind::Extension;
    bool enabled = false;     // its enabled key in settings.txt
    std::vector<SettingSpec> settings;  // what the manifest declares, in its order
    std::vector<std::string> values;    // the value of each setting now: settings.txt, else the default

    ElementRef Ref() const { return {kind, id}; }

    // The name and description to show in a language: its translation when the manifest has one, else the English text.
    const std::string& NameIn(const std::string& language) const;
    const std::string& DescriptionIn(const std::string& language) const;
};

// The extensions of <ampify>\extensions whose manifest is valid and whose kind is extension, sorted by name. Folders that
// do not hold a valid manifest are left out: the kernel would not load them either.
std::vector<ModEntry> ListMods(const std::filesystem::path& kit);

// Whether the extension <id> is on in settings.txt, its own default otherwise. Also for one built into the kernel, such
// as atmos-leveler, which has no manifest for ListMods to find.
bool IsModOn(const std::filesystem::path& kit, const std::string& id);

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

// theme.accent value that makes the accent follow the song playing (the same word as theme::kArtworkAccent).
inline constexpr char kArtworkAccent[] = "artwork";

// The accent colour the user chose over the theme's (theme.accent), as six upper-case hex digits, or kArtworkAccent;
// empty for the theme's own. The engine applies it with or without a theme.
std::string ChosenAccent(const std::filesystem::path& kit);

// Writes theme.accent: "RRGGBB" or "#RRGGBB", kArtworkAccent, or empty to go back to the theme's accent. Anything else
// is refused and leaves the setting as it was.
bool ChooseAccent(const std::filesystem::path& kit, const std::string& accent, std::string& error);

// The accents the settings page offers before "Custom…", Apple Music's pink first.
struct AccentPreset {
    const char* value;
    const char* textKey;
};
const std::vector<AccentPreset>& AccentPresets();

// The theme in use (theme.active), empty for none.
std::string ActiveThemeId(const std::filesystem::path& kit);

// The active theme (theme.active) and the settings its manifest declares, with their values now. Nothing when no
// theme is active, or when it declares no setting.
std::optional<ModEntry> ActiveThemeSettings(const std::filesystem::path& kit);

// What a change of a theme's setting in the card does: writes themes.<id>.<key> = value in settings.txt. The theme
// service watches that file and applies the theme again, so no reload request is needed.
bool SetThemeSetting(const std::filesystem::path& kit, const std::string& id, const std::string& key,
                     const std::string& value, std::string& error);

// One declared setting of any kind: SetModSetting for an extension, SetThemeSetting for a theme, SetSnippetSetting for
// a snippet.
bool SetElementSetting(const std::filesystem::path& kit, const ElementRef& element, const std::string& key,
                       const std::string& value, std::string& error);

// The snippets switched on whose manifest declares settings, with their values now, sorted by name.
std::vector<ModEntry> EnabledSnippetSettings(const std::filesystem::path& kit);

// Writes snippets.<id>.<key> = value in settings.txt; the theme service applies the snippets again, as for a theme.
bool SetSnippetSetting(const std::filesystem::path& kit, const std::string& id, const std::string& key,
                       const std::string& value, std::string& error);

// The settings an installed extension, theme or snippet declares, with their values now, whether it is on or not.
// Nothing when it is not installed under that kind, its manifest is not valid, or it declares no setting.
std::optional<ModEntry> ElementSettings(const std::filesystem::path& kit, std::string_view kind, const std::string& id);

// What `ampify settings` and the Marketplace do: checks `value` against what the element declares for `key` (one of
// a choice's options, a number within its bounds, true or false), then writes it as the Ampify card does. False with
// an English line in `error` otherwise, and settings.txt is left as it was.
bool ChangeElementSetting(const std::filesystem::path& kit, std::string_view kind, const std::string& id,
                          const std::string& key, const std::string& value, std::string& error);

// The same for several settings of one element at once (Reset all, Import): every value is checked first, and when one
// does not fit nothing is written. One write, so an extension restarts once.
bool ChangeElementValues(const std::filesystem::path& kit, std::string_view kind, const std::string& id,
                         const std::map<std::string, std::string>& values, std::string& error);

// Reset and Reset all: the settings `keys` (every declared one when empty) leave settings.txt, so they take their
// manifest default and follow it if a new version changes it. One write; an extension restarts once.
bool ResetElementValues(const std::filesystem::path& kit, std::string_view kind, const std::string& id,
                        const std::vector<std::string>& keys, std::string& error);

}  // namespace ampify::uirender
