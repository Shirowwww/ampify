#include "retired.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <optional>

#include "release_facts.hpp"
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
// The second pass of 1.3.0. Each heir does more by default than the snippet it took in: those rows (whenNew) turn
// that off for someone who did not use the heir, and leave someone who did as they were.
constexpr CarriedSetting kCoverAmbienceSettings[] = {
    {.from = "enabled", .to = "reach", .toValue = "Off", .whenNew = true},
    {.from = "enabled", .to = "glow", .toValue = "Display"},
    {.from = "reach", .to = "glow"},
};
constexpr CarriedSetting kAnimatedArtworkSettings[] = {
    {.from = "enabled", .to = "reach", .toValue = "Off", .whenNew = true},
    {.from = "enabled", .to = "moving-background", .toValue = "true"},
    {.from = "strength", .to = "background-strength"},
};
constexpr CarriedSetting kHoverSearchSettings[] = {
    {.from = "enabled", .to = "compact", .toValue = "false", .whenNew = true},
    {.from = "enabled", .to = "search-on-hover", .toValue = "true"},
};
constexpr CarriedSetting kPlaybarTimesSettings[] = {
    {.from = "enabled", .to = "custom-thickness", .toValue = "false", .whenNew = true},
    {.from = "enabled", .to = "times", .toValue = "Under the pointer"},
    {.from = "show", .to = "times"},
};
constexpr CarriedSetting kFocusedLyricsSettings[] = {
    {.from = "enabled", .to = "align", .toValue = "Left", .whenNew = true},
    {.from = "enabled", .to = "focus", .toValue = "Medium"},
    {.from = "strength", .to = "focus"},
};

constexpr RetiredElement kRetired[] = {
    {.kind = ElementKind::Snippet, .id = "bottom-playbar"},  // 1.3.0: apple.playbar
    {.kind = ElementKind::Theme, .id = "ember", .into = "artwork", .settings = kEmberSettings},  // 1.3.0
    {.kind = ElementKind::Theme, .id = "high-contrast"},  // 1.3.0
    {.kind = ElementKind::Snippet, .id = "hover-row-actions"},  // 1.3.0: Apple shows the buttons on hover itself
    // Before the other merges into the same heirs: their choices (Centred lyrics, a set thickness) win over the
    // defaults these rows turn off.
    {.kind = ElementKind::Snippet, .id = "cover-ambience", .into = "album-colors", .settings = kCoverAmbienceSettings},
    {.kind = ElementKind::Snippet, .id = "animated-artwork", .into = "album-colors", .settings = kAnimatedArtworkSettings},
    {.kind = ElementKind::Snippet, .id = "hover-search", .into = "sidebar-style", .settings = kHoverSearchSettings},
    {.kind = ElementKind::Snippet, .id = "playbar-times", .into = "progress-line", .settings = kPlaybarTimesSettings},
    {.kind = ElementKind::Snippet, .id = "focused-lyrics", .into = "lyrics-style", .settings = kFocusedLyricsSettings},
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
    // A switch on by default: unset, it counts as on while this theme is the active one and `doneWhenSet` is unset
    // too. Once moved, that key is written (on or off), so a later start does not move it again.
    std::string_view onByDefaultFor = {};
    std::string_view doneWhenSet = {};
    // Written first when the snippet these values switch on was off: what it does by default and the old switch did
    // not stays off, and someone who already used it keeps their choices.
    std::string_view heirSwitch = {};
    std::span<const SettingValue> whenNew = {};
};

bool OnByDefault(const MovedSwitch& moved, const Settings& settings, const std::string& active) {
    return !moved.onByDefaultFor.empty() && active == moved.onByDefaultFor &&
           !settings.Get(std::string(moved.doneWhenSet));
}

// 1.3.0: Greenroom's and Palettes' "Hide the Explicit and Lossless badges".
constexpr SettingValue kBadgesHidden[] = {
    {"snippets.declutter.enabled", "1"},
    {"snippets.declutter.badges", "true"},
    {"snippets.declutter.explicit", "true"},
};

// 1.3.3: Glass's "Moving artwork in the background", which Artwork colours does under every theme.
constexpr SettingValue kMovingBackground[] = {
    {"snippets.album-colors.enabled", "1"},
    {"snippets.album-colors.moving-background", "true"},
};
constexpr SettingValue kAlbumColoursOff[] = {
    {"snippets.album-colors.reach", "Off"},
    {"snippets.album-colors.glow", "Off"},
};

constexpr MovedSwitch kMovedSwitches[] = {
    {"themes.greenroom.hide-badges", kBadgesHidden},
    {"themes.palettes.hide-badges", kBadgesHidden},
    {"themes.glass.moving-artwork", kMovingBackground, "glass", "snippets.album-colors.moving-background",
     "snippets.album-colors.enabled", kAlbumColoursOff},
};

// 1.3.0: catalog.json stays as Ampify 1.2.1 reads it, and 1.3.0 reads its own file. The old default, saved as it was,
// follows the new one; any other address was chosen and stays.
constexpr std::string_view kCatalogUrlKey = "catalog.url";
constexpr std::string_view kOldCatalogUrl =
    "https://raw.githubusercontent.com/Shirowwww/ampify-catalog/main/catalog.json";

void MoveCatalogUrl(Settings& settings) {
    const std::string key(kCatalogUrlKey);
    if (settings.GetString(key, "") == kOldCatalogUrl) settings.Set(key, std::string(kDefaultCatalogUrl));
}

void MoveSwitches(Settings& settings) {
    const std::string active = ActiveThemeId(settings);
    for (const MovedSwitch& moved : kMovedSwitches) {
        const std::string key(moved.key);
        if (settings.GetBool(key, OnByDefault(moved, settings, active))) {
            if (!moved.heirSwitch.empty() && !settings.GetBool(std::string(moved.heirSwitch), false)) {
                for (const SettingValue& target : moved.whenNew) settings.Set(std::string(target.key), std::string(target.value));
            }
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

// The active theme, or a snippet switched on.
bool InUse(ElementKind kind, std::string_view id, const std::string& active, const Settings& settings) {
    if (kind == ElementKind::Theme) return active == id;
    return settings.GetBool(Prefix(kind, id) + std::string(kEnabledKey), false);
}

bool Waits(const RetiredElement& element, std::span<const RetiredElement* const> waiting) {
    return std::find(waiting.begin(), waiting.end(), &element) != waiting.end();
}

// The element in use goes to what took its place, with its settings. `heirWasInUse`: the one that took its place was
// already in use before the move, so the rows marked whenNew leave it as it was.
void MoveInUse(const RetiredElement& element, const std::string& active, bool heirWasInUse, Settings& settings) {
    if (!InUse(element.kind, element.id, active, settings)) return;
    if (element.into.empty()) {
        if (element.kind == ElementKind::Theme) settings.Set(kThemeActiveKey, "none");
        return;
    }
    for (const CarriedSetting& carried : element.settings) {
        if (!(carried.whenNew && heirWasInUse)) Carry(element, carried, settings);
    }
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

std::vector<const RetiredElement*> AwaitingReplacement(const Settings& settings, const InstalledCheck& installed) {
    const std::string active = ActiveThemeId(settings);
    std::vector<const RetiredElement*> waiting;
    for (const RetiredElement& element : kRetired) {
        if (element.into.empty() || !InUse(element.kind, element.id, active, settings)) continue;
        if (!installed(ElementRef{element.kind, std::string(element.into)})) waiting.push_back(&element);
    }
    return waiting;
}

std::vector<ElementRef> ReplacementsToInstall(const Settings& settings, const InstalledCheck& installed) {
    const std::string active = ActiveThemeId(settings);
    std::vector<ElementRef> wanted;
    for (const RetiredElement& element : kRetired) {
        if (element.into.empty()) continue;
        ElementRef replacement{element.kind, std::string(element.into)};
        const bool needed = InUse(element.kind, element.id, active, settings) ||
                            InUse(element.kind, element.into, active, settings);
        if (!needed || installed(replacement)) continue;
        if (std::find(wanted.begin(), wanted.end(), replacement) != wanted.end()) continue;
        wanted.push_back(std::move(replacement));
    }
    return wanted;
}

bool MoveRetiredSettings(Settings& settings, std::span<const RetiredElement* const> waiting) {
    const std::map<std::string, std::string> before = settings.Values();
    const std::string active = ActiveThemeId(settings);

    if (!settings.Get(kPlaybarKey)) {
        if (std::optional<std::string> place = ChosenPlace(settings, active)) settings.Set(kPlaybarKey, *place);
    }
    const Settings original = settings;
    for (const RetiredElement& element : kRetired) {
        if (Waits(element, waiting)) continue;
        const bool heirWasInUse = !element.into.empty() && InUse(element.kind, element.into, active, original);
        MoveInUse(element, active, heirWasInUse, settings);
    }
    MoveSwitches(settings);
    MoveCatalogUrl(settings);

    RemoveWhere(settings, [waiting](const std::string& key) {
        for (const RetiredElement& element : kRetired) {
            if (!Waits(element, waiting) && key.starts_with(Prefix(element.kind, element.id))) return true;
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

std::vector<std::string> RetiredFolders(std::span<const RetiredElement* const> waiting) {
    std::vector<std::string> folders;
    for (const RetiredElement& element : kRetired) {
        if (Waits(element, waiting)) continue;
        folders.push_back(std::string(TraitsOf(element.kind).folder) + "/" + std::string(element.id));
    }
    return folders;
}

}  // namespace ampify::theme
