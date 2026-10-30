#pragma once

#include <string>

#include "../settings.hpp"
#include "theme.hpp"

namespace amk::theme {

// The theme keys of settings.txt. The values are read and written as they are kept on disk.
//   theme.active       folder name of the theme in use; empty or `none` for no theme
//   theme.scheme       colour scheme inside colors.ini; empty takes the first one
//   theme.live_reload  1 to apply a theme again when its files change (default 1)
//   theme.inspect      1 to write an inspector dump on the next pass; the kernel clears it afterwards
//   theme.parts.colors     1 to apply the colours of colors.ini (default 1)
//   theme.parts.resources  1 to apply resources.xaml and user.css (default 1)
//   theme.parts.assets     1 to load the images of the theme (default 1)
struct ThemeSettings {
    std::string active;
    std::string scheme;
    bool liveReload = true;
    bool inspect = false;
    ThemeParts parts;
};

ThemeSettings ReadThemeSettings(const Settings& settings);
void WriteThemeSettings(Settings& settings, const ThemeSettings& value);

}  // namespace amk::theme
