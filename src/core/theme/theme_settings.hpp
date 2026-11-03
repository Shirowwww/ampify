#pragma once

#include <map>
#include <string>

#include "../settings.hpp"
#include "theme.hpp"

namespace ampify::theme {

// The theme keys of settings.txt. The values are read and written as they are kept on disk.
//   theme.active       folder name of the theme in use; empty or `none` for no theme
//   theme.scheme       colour scheme inside colors.ini; empty takes the theme's default; `auto` follows the Windows mode
//   theme.schedule     schemes by local time, such as `07:00=Light,19:00=Dark`; wins over theme.scheme (scheme_choice.hpp)
//   theme.live_reload  1 to apply a theme again when its files change (default 1)
//   theme.inspect      1 to write an inspector dump on the next pass; the kernel clears it afterwards
//   theme.parts.colors     1 to apply the colours of colors.ini (default 1)
//   theme.parts.resources  1 to apply resources.xaml and user.css (default 1)
//   theme.parts.assets     1 to load the images of the theme (default 1)
//   theme.parts.script     1 to run main.js of the active theme in AppleMusic.exe (default 1)
//   themes.<id>.<key>      the value of a setting the theme <id> declares in its manifest (ReadThemeValues)
//   snippets.<id>.<key>    the same for the snippet <id> (ReadSnippetValues)
struct ThemeSettings {
    std::string active;
    std::string scheme;
    std::string schedule;
    bool liveReload = true;
    bool inspect = false;
    ThemeParts parts;
};

ThemeSettings ReadThemeSettings(const Settings& settings);
void WriteThemeSettings(Settings& settings, const ThemeSettings& value);

// The values of the settings a theme declares, stored as themes.<id>.<key>, by key. Empty for an empty id.
std::map<std::string, std::string> ReadThemeValues(const Settings& settings, const std::string& id);

// The same for a snippet: snippets.<id>.<key>, without snippets.<id>.enabled.
std::map<std::string, std::string> ReadSnippetValues(const Settings& settings, const std::string& id);

}  // namespace ampify::theme
