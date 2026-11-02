#include "theme_settings.hpp"

#include <cctype>

#include "../text_util.hpp"

namespace ampify::theme {

ThemeSettings ReadThemeSettings(const Settings& settings) {
    ThemeSettings value;
    value.active = settings.GetString("theme.active", "");
    std::string lowered = ToLowerAscii(value.active);
    if (lowered == "none") value.active.clear();
    value.scheme = settings.GetString("theme.scheme", "");
    value.schedule = settings.GetString("theme.schedule", "");
    value.liveReload = settings.GetBool("theme.live_reload", true);
    value.inspect = settings.GetBool("theme.inspect", false);
    value.parts.colors = settings.GetBool("theme.parts.colors", true);
    value.parts.resources = settings.GetBool("theme.parts.resources", true);
    value.parts.assets = settings.GetBool("theme.parts.assets", true);
    value.parts.script = settings.GetBool("theme.parts.script", true);
    return value;
}

void WriteThemeSettings(Settings& settings, const ThemeSettings& value) {
    settings.Set("theme.active", value.active.empty() ? "none" : value.active);
    settings.Set("theme.scheme", value.scheme);
    // Written only when set, so the settings of someone who never uses it do not grow a key.
    if (value.schedule.empty()) {
        settings.Remove("theme.schedule");
    } else {
        settings.Set("theme.schedule", value.schedule);
    }
    settings.SetBool("theme.live_reload", value.liveReload);
    settings.SetBool("theme.inspect", value.inspect);
    settings.SetBool("theme.parts.colors", value.parts.colors);
    settings.SetBool("theme.parts.resources", value.parts.resources);
    settings.SetBool("theme.parts.assets", value.parts.assets);
    settings.SetBool("theme.parts.script", value.parts.script);
}

}  // namespace ampify::theme
