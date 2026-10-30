#include "theme_settings.hpp"

#include <cctype>

namespace amk::theme {

ThemeSettings ReadThemeSettings(const Settings& settings) {
    ThemeSettings value;
    value.active = settings.GetString("theme.active", "");
    std::string lowered;
    for (char c : value.active) lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lowered == "none") value.active.clear();
    value.scheme = settings.GetString("theme.scheme", "");
    value.liveReload = settings.GetBool("theme.live_reload", true);
    value.inspect = settings.GetBool("theme.inspect", false);
    value.parts.colors = settings.GetBool("theme.parts.colors", true);
    value.parts.resources = settings.GetBool("theme.parts.resources", true);
    value.parts.assets = settings.GetBool("theme.parts.assets", true);
    return value;
}

void WriteThemeSettings(Settings& settings, const ThemeSettings& value) {
    settings.Set("theme.active", value.active.empty() ? "none" : value.active);
    settings.Set("theme.scheme", value.scheme);
    settings.SetBool("theme.live_reload", value.liveReload);
    settings.SetBool("theme.inspect", value.inspect);
    settings.SetBool("theme.parts.colors", value.parts.colors);
    settings.SetBool("theme.parts.resources", value.parts.resources);
    settings.SetBool("theme.parts.assets", value.parts.assets);
}

}  // namespace amk::theme
