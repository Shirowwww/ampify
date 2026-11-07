#include "theme_settings.hpp"

#include "../element_ref.hpp"
#include "../settings_catalog.hpp"
#include "../text_util.hpp"

namespace ampify::theme {

ThemeSettings ReadThemeSettings(const Settings& settings) {
    ThemeSettings value;
    value.active = ActiveThemeId(settings);
    value.scheme = settings.GetString("theme.scheme", "");
    value.schedule = settings.GetString("theme.schedule", "");
    value.accent = settings.GetString("theme.accent", "");
    value.glass = settings.GetString(kGlassChoiceKey, "");
    value.liveReload = ToggleOf(settings, kLiveReloadKey);
    value.inspect = ToggleOf(settings, kInspectKey);
    value.parts.colors = ToggleOf(settings, kThemeColorsKey);
    value.parts.resources = ToggleOf(settings, kThemeResourcesKey);
    value.parts.assets = ToggleOf(settings, kThemeAssetsKey);
    value.parts.script = ToggleOf(settings, kThemeScriptKey);
    return value;
}

void WriteThemeSettings(Settings& settings, const ThemeSettings& value) {
    settings.Set(kThemeActiveKey, value.active.empty() ? "none" : value.active);
    settings.Set("theme.scheme", value.scheme);
    // Written only when set, so the settings of someone who never uses it do not grow a key.
    if (value.schedule.empty()) {
        settings.Remove("theme.schedule");
    } else {
        settings.Set("theme.schedule", value.schedule);
    }
    if (value.accent.empty()) {
        settings.Remove("theme.accent");
    } else {
        settings.Set("theme.accent", value.accent);
    }
    if (value.glass.empty()) {
        settings.Remove(kGlassChoiceKey);
    } else {
        settings.Set(kGlassChoiceKey, value.glass);
    }
    settings.SetBool(kLiveReloadKey, value.liveReload);
    settings.SetBool(kInspectKey, value.inspect);
    settings.SetBool(kThemeColorsKey, value.parts.colors);
    settings.SetBool(kThemeResourcesKey, value.parts.resources);
    settings.SetBool(kThemeAssetsKey, value.parts.assets);
    settings.SetBool(kThemeScriptKey, value.parts.script);
}

namespace {

std::map<std::string, std::string> ReadValues(const Settings& settings, const std::string& prefix) {
    std::map<std::string, std::string> values;
    for (auto it = settings.Values().lower_bound(prefix); it != settings.Values().end(); ++it) {
        if (it->first.compare(0, prefix.size(), prefix) != 0) break;
        values[it->first.substr(prefix.size())] = it->second;
    }
    return values;
}

}  // namespace

std::map<std::string, std::string> ReadThemeValues(const Settings& settings, const std::string& id) {
    if (id.empty()) return {};
    return ReadValues(settings, ElementRef{ElementKind::Theme, id}.SettingsPrefix());
}

std::map<std::string, std::string> ReadSnippetValues(const Settings& settings, const std::string& id) {
    if (id.empty()) return {};
    std::map<std::string, std::string> values =
        ReadValues(settings, ElementRef{ElementKind::Snippet, id}.SettingsPrefix());
    values.erase(std::string(kEnabledKey));  // the snippet's switch, not one of its settings
    return values;
}

}  // namespace ampify::theme
