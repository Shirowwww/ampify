#pragma once

// The « Mods (Apple Music Mod Kit) » card that the kit adds to the Playback page of Apple Music's settings: the list
// of installed extensions, and what a toggle of that card does. Portable, so the file handling is tested under Linux;
// the card itself is built by win/ui_service.cpp.

#include <filesystem>
#include <string>
#include <vector>

namespace amk::uirender {

struct ModEntry {
    std::string id;
    std::string name;         // manifest name, or the id when the manifest has none
    std::string description;  // may be empty
    bool enabled = false;     // extensions.<id>.enabled in settings.txt
};

// The extensions of <kit>\extensions whose manifest is valid and whose kind is extension, sorted by name. Folders that
// do not hold a valid manifest are left out: the kernel would not load them either.
std::vector<ModEntry> ListMods(const std::filesystem::path& kit);

// What a toggle of the card does, as `amk extension enable|disable` would: writes extensions.<id>.enabled in
// settings.txt (keeping the other lines), then appends `start <id>` or `stop <id>` to reload.txt, which the kernel's
// JavaScript thread reads every 500 ms. False with a French line in `error` when a file cannot be written.
bool SwitchMod(const std::filesystem::path& kit, const std::string& id, bool enabled, std::string& error);

}  // namespace amk::uirender
