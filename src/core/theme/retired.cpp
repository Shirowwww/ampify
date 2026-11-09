#include "retired.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <optional>

#include "../settings_catalog.hpp"
#include "../text_util.hpp"

namespace ampify::theme {

namespace {

// Ember merged into Artwork (1.3.0): its shapes and veil (Artwork's stops at 0.9), and its Artwork accent.
constexpr CarriedSetting kEmberSettings[] = {
    {.from = "panel-radius", .to = "panel-radius"},
    {.from = "gap", .to = "gap"},
    {.from = "veil", .to = "veil", .atMost = "0.9"},
    {.from = "accent", .to = "art-accent", .fromValue = "artwork", .toValue = "true"},
};

// The snippets merged in 1.3.0. A setting the old snippet only had by default is written from its switch ("enabled",
// with the value it stood for), then its own stored value, when there is one, takes over. Rows are read in order: the
// last one that writes a key wins.
constexpr CarriedSetting kHideExplicitSettings[] = {
    {.from = "enabled", .to = "explicit", .toValue = "true"},
};
constexpr CarriedSetting kSquareCornersSettings[] = {
    {.from = "enabled", .to = "shape", .toValue = "Square"},
};
constexpr CarriedSetting kHideButtonsSettings[] = {
    {.from = "enabled", .to = "download-button", .toValue = "true"},  // its default; AirPlay Declutter always hides
    {.from = "download", .to = "download-button"},
    {.from = "lyrics", .to = "lyrics-button"},
    {.from = "queue", .to = "queue-button"},
    {.from = "volume", .to = "volume-button"},
};
// Lyrics style starts centred: the spotlight and the dark pane alone kept Apple's lines at the left.
constexpr CarriedSetting kLyricsSpotlightSettings[] = {
    {.from = "enabled", .to = "align", .toValue = "Left"},
    {.from = "enabled", .to = "spotlight", .toValue = "true"},
};
constexpr CarriedSetting kDarkLyricsSettings[] = {
    {.from = "enabled", .to = "align", .toValue = "Left"},
    {.from = "enabled", .to = "background", .toValue = "Dark"},
    {.from = "style", .to = "background"},
};
constexpr CarriedSetting kCentredLyricsSettings[] = {
    {.from = "enabled", .to = "align", .toValue = "Centred"},
};
// Progress line sets the thickness unless told not to: a mascot alone kept Apple's line.
constexpr CarriedSetting kProgressMascotSettings[] = {
    {.from = "enabled", .to = "custom-thickness", .toValue = "false"},
    {.from = "enabled", .to = "mascot", .toValue = "Comet"},
    {.from = "mascot", .to = "mascot"},
    {.from = "rainbow", .to = "rainbow"},
};
constexpr CarriedSetting kProgressThicknessSettings[] = {
    {.from = "enabled", .to = "custom-thickness", .toValue = "true"},
    {.from = "enabled", .to = "thickness", .toValue = "5"},
    {.from = "thickness", .to = "thickness"},
    {.from = "custom-volume", .to = "custom-volume"},
    {.from = "volume", .to = "volume"},
};
// Sidebar style starts compact: the icons alone kept Apple's rows.
constexpr CarriedSetting kSidebarIconsSettings[] = {
    {.from = "enabled", .to = "compact", .toValue = "false"},
    {.from = "enabled", .to = "icons", .toValue = "Round"},
    {.from = "shape", .to = "icons"},
};
constexpr CarriedSetting kCompactSidebarSettings[] = {
    {.from = "enabled", .to = "compact", .toValue = "true"},
};

constexpr RetiredElement kRetired[] = {
    {.kind = ElementKind::Snippet, .id = "bottom-playbar"},  // 1.3.0: apple.playbar
    {.kind = ElementKind::Theme, .id = "ember", .into = "artwork", .settings = kEmberSettings},  // 1.3.0
    {.kind = ElementKind::Theme, .id = "high-contrast"},  // 1.3.0
    {.kind = ElementKind::Snippet, .id = "hover-row-actions"},  // 1.3.0: Apple shows the buttons on hover itself
    {.kind = ElementKind::Snippet, .id = "hide-explicit", .into = "declutter", .settings = kHideExplicitSettings},
    {.kind = ElementKind::Snippet, .id = "square-corners", .into = "round-corners", .settings = kSquareCornersSettings},
    {.kind = ElementKind::Snippet, .id = "hide-buttons", .into = "declutter", .settings = kHideButtonsSettings},
    {.kind = ElementKind::Snippet, .id = "lyrics-spotlight", .into = "lyrics-style", .settings = kLyricsSpotlightSettings},
    {.kind = ElementKind::Snippet, .id = "dark-lyrics", .into = "lyrics-style", .settings = kDarkLyricsSettings},
    {.kind = ElementKind::Snippet, .id = "centred-lyrics", .into = "lyrics-style", .settings = kCentredLyricsSettings},
    {.kind = ElementKind::Snippet, .id = "progress-mascot", .into = "progress-line", .settings = kProgressMascotSettings},
    {.kind = ElementKind::Snippet, .id = "progress-thickness", .into = "progress-line",
     .settings = kProgressThicknessSettings},
    {.kind = ElementKind::Snippet, .id = "sidebar-icons", .into = "sidebar-style", .settings = kSidebarIconsSettings},
    {.kind = ElementKind::Snippet, .id = "compact-sidebar", .into = "sidebar-style", .settings = kCompactSidebarSettings},
};

constexpr std::string_view kBarSuffix = ".bar-position";

// A theme's own switch that went because snippets do the same under every theme: on, it switches those on instead.
struct SettingValue {
    std::string_view key;
    std::string_view value;
};

struct MovedSwitch {
    std::string_view key;
    std::span<const SettingValue> becomes;
};

// 1.3.0: Greenroom's and Palettes' "Hide the Explicit and Lossless badges".
constexpr SettingValue kBadgesHidden[] = {
    {"snippets.declutter.enabled", "1"},
    {"snippets.declutter.badges", "true"},
    {"snippets.declutter.explicit", "true"},
};

constexpr MovedSwitch kMovedSwitches[] = {
    {"themes.greenroom.hide-badges", kBadgesHidden},
    {"themes.palettes.hide-badges", kBadgesHidden},
};

void MoveSwitches(Settings& settings) {
    for (const MovedSwitch& moved : kMovedSwitches) {
        const std::string key(moved.key);
        if (settings.GetBool(key, false)) {
            for (const SettingValue& target : moved.becomes) settings.Set(std::string(target.key), std::string(target.value));
        }
        settings.Remove(key);
    }
}

// Where each theme put the bar before 1.3.0 while nobody chose. A stored value equal to it was the settings page
// saving the default, not a choice: it does not become the place of every theme.
struct OldPlace {
    std::string_view theme;
    std::string_view place;
};
constexpr OldPlace kOldDefaults[] = {
    {"artwork", "top"},    {"cinema", "bottom"}, {"cosy", "bottom"},      {"ember", "bottom"},
    {"fluent", "top"},     {"glass", "bottom"},  {"greenroom", "bottom"}, {"palettes", "top"},
};

std::string_view OldDefault(std::string_view theme) {
    for (const OldPlace& old : kOldDefaults) {
        if (old.theme == theme) return old.place;
    }
    return "top";
}

bool IsPlace(std::string_view place) { return place == "top" || place == "bottom" || place == "centred"; }

std::string Prefix(ElementKind kind, std::string_view id) { return ElementRef{kind, std::string(id)}.SettingsPrefix(); }

// The place someone chose before 1.3.0: the Bottom playback bar snippet when it was on, else the active theme's own
// setting when it differed from that theme's default. Ember's users keep its bar, at the bottom unless they moved it:
// Artwork, which takes its place, puts it at the top.
std::optional<std::string> ChosenPlace(const Settings& settings, const std::string& active) {
    const std::string snippet = Prefix(ElementKind::Snippet, "bottom-playbar");
    if (settings.GetBool(snippet + std::string(kEnabledKey), false)) {
        return settings.GetBool(snippet + "centred", false) ? "centred" : "bottom";
    }
    if (active.empty()) return std::nullopt;
    std::string stored = ToLowerAscii(settings.GetString("themes." + active + std::string(kBarSuffix), ""));
    if (active == "ember") return IsPlace(stored) ? stored : std::string(OldDefault(active));
    if (IsPlace(stored) && stored != OldDefault(active)) return stored;
    return std::nullopt;
}

std::optional<double> NumberOf(std::string_view text) {
    double value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size()) return std::nullopt;
    return value;
}

void Carry(const RetiredElement& element, const CarriedSetting& carried, Settings& settings) {
    std::optional<std::string> value = settings.Get(Prefix(element.kind, element.id) + std::string(carried.from));
    if (!value) return;
    if (!carried.fromValue.empty() && ToLowerAscii(*value) != ToLowerAscii(carried.fromValue)) return;
    std::string written = carried.toValue.empty() ? *value : std::string(carried.toValue);
    if (!carried.atMost.empty()) {
        std::optional<double> number = NumberOf(written);
        std::optional<double> ceiling = NumberOf(carried.atMost);
        if (!number || !ceiling) return;
        if (*number > *ceiling) written = std::string(carried.atMost);
    }
    settings.Set(Prefix(element.kind, element.into) + std::string(carried.to), written);
}

// The element in use goes to what took its place, with its settings: the active theme, or a snippet switched on.
void MoveInUse(const RetiredElement& element, const std::string& active, Settings& settings) {
    const std::string enabled = Prefix(element.kind, element.id) + std::string(kEnabledKey);
    const bool inUse = element.kind == ElementKind::Theme ? active == element.id : settings.GetBool(enabled, false);
    if (!inUse) return;
    if (element.into.empty()) {
        if (element.kind == ElementKind::Theme) settings.Set(kThemeActiveKey, "none");
        return;
    }
    for (const CarriedSetting& carried : element.settings) Carry(element, carried, settings);
    if (element.kind == ElementKind::Theme) {
        settings.Set(kThemeActiveKey, std::string(element.into));
    } else {
        settings.Set(Prefix(element.kind, element.into) + std::string(kEnabledKey), "1");
    }
}

void RemoveWhere(Settings& settings, const auto& gone) {
    std::vector<std::string> keys;
    for (const auto& [key, value] : settings.Values()) {
        if (gone(key)) keys.push_back(key);
    }
    for (const std::string& key : keys) settings.Remove(key);
}

}  // namespace

std::span<const RetiredElement> RetiredElements() { return kRetired; }

bool MoveRetiredSettings(Settings& settings) {
    const std::map<std::string, std::string> before = settings.Values();
    const std::string active = ActiveThemeId(settings);

    if (!settings.Get(kPlaybarKey)) {
        if (std::optional<std::string> place = ChosenPlace(settings, active)) settings.Set(kPlaybarKey, *place);
    }
    for (const RetiredElement& element : kRetired) MoveInUse(element, active, settings);
    MoveSwitches(settings);

    RemoveWhere(settings, [](const std::string& key) {
        for (const RetiredElement& element : kRetired) {
            if (key.starts_with(Prefix(element.kind, element.id))) return true;
        }
        // Only Ampify's themes dropped bar-position: a theme of someone else's may still declare and read it.
        for (const OldPlace& old : kOldDefaults) {
            if (key == "themes." + std::string(old.theme) + std::string(kBarSuffix)) return true;
        }
        return false;
    });
    return settings.Values() != before;
}

bool IsRetired(ElementKind kind, std::string_view id) {
    return std::any_of(std::begin(kRetired), std::end(kRetired),
                       [&](const RetiredElement& element) { return element.kind == kind && element.id == id; });
}

std::vector<std::string> RetiredFolders() {
    std::vector<std::string> folders;
    for (const RetiredElement& element : kRetired) {
        folders.push_back(std::string(TraitsOf(element.kind).folder) + "/" + std::string(element.id));
    }
    return folders;
}

}  // namespace ampify::theme
