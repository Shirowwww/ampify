#pragma once

// The "Ampify" card that Ampify adds to the Playback page of Apple Music's settings: the list
// of installed extensions, and what a toggle of that card does. Portable, so the file handling is tested under Linux;
// the card itself is built by src/win/ui_service.cpp.

#include <filesystem>
#include <string>
#include <vector>

#include "../manifest.hpp"

namespace ampify::uirender {

struct ModEntry {
    std::string id;
    std::string name;         // manifest name, or the id when the manifest has none
    std::string description;  // may be empty
    bool enabled = false;     // extensions.<id>.enabled in settings.txt
    std::vector<SettingSpec> settings;  // what the manifest declares, in its order
    std::vector<std::string> values;    // the value of each setting now: settings.txt, else the default
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

}  // namespace ampify::uirender
