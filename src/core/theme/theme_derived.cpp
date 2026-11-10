// The rules Ampify derives from a theme's own: room for decors, accents carried over, the scrubber's corners.

#include "theme.hpp"
#include "theme_detail.hpp"

namespace ampify::theme {

using namespace detail;

namespace {

double LargestCorner(std::string_view corners) {
    double largest = 0;
    for (std::string_view corner : SplitArguments(corners)) {
        largest = std::max(largest, ParseNumber(corner).value_or(0));
    }
    return largest;
}

// The progress line runs along the bottom of the song's display, up to its right edge: a theme that rounds the display
// cut its end off with the corner. It stops where the corner's curve meets the line, 2 above the display's edge: a
// margin of the whole radius left a gap at the end that read as a line too short.
constexpr double kScrubberLift = 2;

std::string ScrubberClearOfCorners(const Theme& theme) {
    if (Sets(theme, ".scrubber", "Margin")) return {};
    double radius = 0;
    int order = -1;
    for (const Assignment& assignment : theme.assignments) {
        if (assignment.media || assignment.selector.trigger != Trigger::Always) continue;
        if (assignment.selector.text == ".lcd" && assignment.property == "CornerRadius" && assignment.order >= order) {
            order = assignment.order;
            radius = LargestCorner(assignment.value.text);
        }
    }
    if (radius <= kScrubberLift) return {};
    // Half of it: the line's template repeats its margin inside (see .scrubber in layouts/bottom-bar.css).
    double curve = radius - std::sqrt(radius * radius - (radius - kScrubberLift) * (radius - kScrubberLift));
    double inset = std::ceil(curve / 2);
    return ".scrubber { margin: 0 " + FormatNumber(inset) + " 0 0; }\n";
}

// An element whose height Apple fixes below what its template asks for: WinUI clips the template and all it holds to
// that height, so a ::before or ::after image inside showed as a sliver (a 6-high progress line, template 18). A
// bottom-aligned slot grows upwards, moved down by what it gained. The clip also cut a thicker track: the line's
// template centres it in a row 3 below its top, at least 4 high. A theme's track gets a slot that reaches its bottom.
struct FixedSlot {
    std::string_view name;      // the stable name (builtins.cpp)
    std::string_view selector;  // the element it names, for a theme that writes it in full
    double height;              // Apple's
    double asked;               // what its template asks for: the least room that clips nothing
    double trackTop;            // where its template's row for the track starts, from its top
    double trackRow;            // that row's least height; the track is centred in it
    double track;               // Apple's own thickness
    std::string_view tracks[2]; // the elements that draw it, whose height is its thickness
    std::string_view above[2];  // what Apple sets just above the track, which a thicker one rose into
};

constexpr FixedSlot kFixedSlots[] = {
    {".scrubber", "ScrubberControl#LCDScrubber", 6, 18, 3, 4, 2, {"HorizontalTrackRect", "HorizontalDecreaseRect"},
     {".current-time", ".duration"}},
};

bool InSlot(const std::string& selector, const FixedSlot& slot) {
    std::string_view element = slot.selector.substr(slot.selector.find('#') + 1);
    return selector.find(slot.name) != std::string::npos || selector.find(element) != std::string::npos;
}

double LengthOrZero(std::string_view text) {
    double value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && std::isfinite(value) ? value : 0;
}

// The height a decor takes in its panel's layout: its own, plus its top and bottom margins (negative ones give room
// back, as the progress mascot's do, so it takes none).
double DecorLayoutHeight(const Theme& theme, const Assignment& content) {
    double height = 0;
    double margins = 0;
    for (const Assignment& assignment : theme.assignments) {
        if (assignment.selector.text != content.selector.text || assignment.selector.pseudo != content.selector.pseudo) {
            continue;
        }
        if (assignment.property == "Height") height = LengthOrZero(assignment.value.text);
        if (assignment.property == "Margin") {
            std::string_view sides = assignment.value.text;  // left,top,right,bottom
            size_t top = sides.find(','), right = sides.find(',', top + 1), bottom = sides.find(',', right + 1);
            if (bottom == std::string_view::npos) continue;
            margins = LengthOrZero(sides.substr(top + 1, right - top - 1)) + LengthOrZero(sides.substr(bottom + 1));
        }
    }
    return std::max(0.0, height + margins);
}

double DecorRoom(const Theme& theme, const FixedSlot& slot) {
    double room = 0;
    for (const Assignment& assignment : theme.assignments) {
        if (assignment.selector.pseudo == PseudoElement::None || assignment.property != kContentProperty ||
            assignment.value.text.empty() || !InSlot(assignment.selector.text, slot)) {
            continue;
        }
        room = std::max({room, slot.asked, DecorLayoutHeight(theme, assignment)});
    }
    return room;
}

bool DrawsTrack(const std::string& selector, const FixedSlot& slot) {
    return std::any_of(std::begin(slot.tracks), std::end(slot.tracks),
                       [&](std::string_view name) { return selector.find(name) != std::string::npos; });
}

// The thickest track a theme asks for, by the height of the elements that draw it or by the slot's own track-height; 0
// when it leaves Apple's.
double TrackThickness(const Theme& theme, const FixedSlot& slot) {
    double thickness = 0;
    for (const Assignment& assignment : theme.assignments) {
        const std::string& text = assignment.selector.text;
        if (assignment.media || assignment.selector.trigger != Trigger::Always ||
            assignment.selector.pseudo != PseudoElement::None || !InSlot(text, slot)) {
            continue;
        }
        bool drawn = assignment.property == "Height" && DrawsTrack(text, slot);
        bool own = assignment.property == "TrackHeight" && (text == slot.name || text == slot.selector);
        if (drawn || own) thickness = std::max(thickness, LengthOrZero(assignment.value.text));
    }
    return thickness;
}

// Where a theme's track ends in the slot's template, in whole pixels: Apple's own (0) keeps the place it has.
double TrackBottom(const FixedSlot& slot, double track) {
    if (track <= 0) return slot.height;
    return std::ceil(slot.trackTop + (std::max(slot.trackRow, track) + track) / 2);
}

// The volume slider takes the accent through WinUI's slider keys (DefineAccentControls), but Apple Music's progress line
// reads keys of its own and stays white: it takes the accent too, as Spicetify's playback bar takes `button`.
std::string ScrubberInAccent(const Theme& theme) {
    if (Sets(theme, ".scrubber", "Foreground") || Sets(theme, "ScrubberControl#LCDScrubber", "Foreground")) return {};
    if (!theme.palette.Find(kAccent)) return {};
    return std::string(".scrubber { color: $") + kAccent + "; }\n";
}

// Apple's "now playing" bars take the accent once, when their row is built: a new song's row is built before its
// accent comes (theme.accent = artwork) and kept the colour of the song before. Set on the bars, it follows each change.
std::string NowPlayingBarsInAccent(const Theme& theme) {
    if (Sets(theme, ".now-playing-indicator", "Foreground")) return {};
    if (!theme.palette.Find(kAccent)) return {};
    return std::string(".now-playing-indicator { color: $") + kAccent + "; }\n";
}

// Apple Music's shuffle icon, while on, takes a blue of Apple's own (its button's ToggleButtonForegroundChecked keys),
// while repeat and both dots take the accent: shuffle takes the accent too.
std::string ShuffleInAccent(const Theme& theme) {
    if (Sets(theme, ".shuffle-button", kCheckedForegroundProperty)) return {};
    if (!theme.palette.Find(kAccent)) return {};
    return std::string(".shuffle-button { checked-color: $") + kAccent + "; }\n";
}

// Apple Music's Up Next paints "Clear" in its own red and the autoplay toggle, while on, in its own blue, colours of
// their templates that no theme key reaches: both take the accent, so the pane reads in the theme's colours.
std::string UpNextInAccent(const Theme& theme) {
    if (!theme.palette.Find(kAccent)) return {};
    std::string css;
    if (!Sets(theme, ".queue-clear", "Foreground")) css += std::string(".queue-clear { color: $") + kAccent + "; }\n";
    if (!Sets(theme, ".queue-autoplay", kCheckedForegroundProperty)) {
        css += std::string(".queue-autoplay { checked-color: $") + kAccent + "; }\n";
    }
    return css;
}

// Apple Music paints an album's artist in its own blue, in code: the theme's link colour takes over, as WinUI's links
// do (DefineAccentControls). On a light theme, Apple's light blue barely read.
std::string ArtistInLinkColour(const Theme& theme) {
    if (Sets(theme, ".album-artist", "Foreground")) return {};
    for (const char* key : {"AccentTextFillColorPrimaryBrush", kAccent}) {
        if (theme.palette.Find(key)) return ".album-artist { color: $" + std::string(key) + "; }\n";
    }
    return {};
}

// A tile's subtitle that links to the artist keeps the colour Apple gives its link in code, for the mode the page was
// built in: white on a light theme's page, unreadable. It takes the theme's secondary text, as the plain subtitles do,
// at full opacity: Apple fades it to 70% for its own grey, which put a theme's secondary text under 4.5:1.
std::string CardLinksInSecondaryText(const Theme& theme) {
    if (!theme.palette.Find("TextFillColorSecondaryBrush")) return {};
    std::string declarations;
    if (!Sets(theme, ".card-subtitle", "Foreground")) declarations += " color: $TextFillColorSecondaryBrush;";
    if (!Sets(theme, ".card-subtitle", "Opacity")) declarations += " opacity: 1;";
    return declarations.empty() ? std::string() : ".card-subtitle {" + declarations + " }\n";
}

// The song's title and artist in the display keep the colours Apple gave them for the mode the window was in when they
// were built: a light theme on Windows in dark mode left the artist's line white on its light bar (Paper light), and a
// dark theme on a light Windows left it dark on dark. They take the theme's primary and secondary text, unless the
// theme colours them itself.
std::string DisplayTextInThemeColours(const Theme& theme) {
    constexpr struct {
        const char* name;
        const char* element;
        const char* key;
    } kLines[] = {
        {".track-title", "TickerTapeControl#TrackTitleTickerTape", "TextFillColorPrimaryBrush"},
        {".track-artist", "TickerTapeControl#ArtistTitleTickerTape", "TextFillColorSecondaryBrush"},
    };
    std::string css;
    for (const auto& line : kLines) {
        const std::string name(line.name), element(line.element);
        bool own = Sets(theme, name, "Foreground") || Sets(theme, name + " TextBlock", "Foreground") ||
                   Sets(theme, element, "Foreground") || Sets(theme, element + " TextBlock", "Foreground");
        if (own || !theme.palette.Find(line.key)) continue;
        css += name + " TextBlock { color: $" + line.key + "; }\n";
    }
    return css;
}

// Apple Music paints the tiles of a search's top results in its own grey, in code: the theme's card colour instead.
std::string SearchTilesInCardColour(const Theme& theme) {
    if (Sets(theme, ".search-top-result", "Background")) return {};
    if (!theme.palette.Find("CardBackgroundFillColorDefaultBrush")) return {};
    return ".search-top-result { background: $CardBackgroundFillColorDefaultBrush; }\n";
}

// Apple Music's lists give their rows' selection colours in their own style, which never reads the ListViewItem keys
// of colors.ini: the rows take them from these rules instead (DefineSelectedRows fills the keys a theme leaves out).
std::string SelectedRowsInThemeColour(const Theme& theme) {
    constexpr std::pair<const char*, const char*> kStates[] = {
        {"selected-background", "ListViewItemBackgroundSelected"},
        {"selected-hover-background", "ListViewItemBackgroundSelectedPointerOver"},
        {"selected-active-background", "ListViewItemBackgroundSelectedPressed"},
    };
    std::string declarations;
    for (const auto& [property, key] : kStates) {
        if (!theme.palette.Find(key) || Sets(theme, ".list-item", FindProperty(property)->xaml)) continue;
        declarations += std::string(" ") + property + ": $" + key + ";";
    }
    return declarations.empty() ? std::string() : ".list-item {" + declarations + " }\n";
}

// A theme that lights a song of a list under the pointer (.track-row:hover) showed two lights: its own, and Apple's
// list row across the whole width, a square edge out of the theme's. The theme's light stays alone.
std::string OneLightPerSong(const Theme& theme) {
    bool lit = std::any_of(theme.assignments.begin(), theme.assignments.end(), [](const Assignment& assignment) {
        const Selector& selector = assignment.selector;
        return selector.trigger == Trigger::Hover && assignment.property == "Background" && !selector.parts.empty() &&
               selector.parts.back().type == "TracklistGrid" && selector.parts.back().name == "RootGrid";
    });
    if (!lit || Sets(theme, ".track-list-item", "PointerOverBackground")) return {};
    return ".track-list-item { hover-background: transparent; active-background: transparent; }\n";
}

// A theme that lights a tile under the pointer (.card:hover) drew its light right against the artwork and the titles:
// the tile keeps room inside, as Spotify's do.
std::string RoomInsideALitCard(const Theme& theme) {
    bool lit = std::any_of(theme.assignments.begin(), theme.assignments.end(), [](const Assignment& assignment) {
        const Selector& selector = assignment.selector;
        return selector.trigger == Trigger::Hover && assignment.property == "Background" && !selector.parts.empty() &&
               selector.parts.back().type == "HoverGrid" && selector.parts.back().name == "SquareLockupContent";
    });
    if (!lit || Sets(theme, ".card", "Padding")) return {};
    return ".card { padding: 8; }\n";
}

// Apple centres the bar's toggles in their cell, leaving an empty stretch on their right, and Ampify's own buttons
// joined them edge to edge. Under every theme they keep to the right end, a little apart.
std::string TogglesAtTheRightEnd(const Theme& theme) {
    std::string css;
    if (!Sets(theme, ".playbar-toggles", "HorizontalAlignment")) {
        css += ".playbar-toggles { horizontal-alignment: right; }\n";
    }
    css += ".playbar-toggles > Button, .playbar-toggles > VolumeControl, .playbar-toggles > StackPanel > ToggleButton "
           "{ margin: 0 2 0 2; }\n";
    return css;
}

// The veil Apple lays over the playing song's artwork under the pointer is a Rectangle with square corners: over an
// artwork the theme rounds, the pointer showed a square. It takes the artwork's radius (a Rectangle has one only).
std::string LcdVeilFollowsArtwork(const Theme& theme) {
    constexpr std::string_view kVeil = ".lcd-artwork-hover > Rectangle";
    if (Sets(theme, kVeil, "CornerRadius")) return {};
    const Assignment* winner = nullptr;
    for (const Assignment& assignment : theme.assignments) {
        const std::string& text = assignment.selector.text;
        if (assignment.media || assignment.selector.trigger != Trigger::Always ||
            assignment.selector.pseudo != PseudoElement::None || assignment.property != "CornerRadius" ||
            (text != ".lcd-artwork-image" && text != ".artwork")) {
            continue;
        }
        if (!winner || Beats(assignment, *winner)) winner = &assignment;
    }
    if (!winner) return {};
    return std::string(kVeil) + " { corner-radius: " + FormatNumber(LargestCorner(winner->value.text)) + "; }\n";
}

}  // namespace

namespace detail {

bool Sets(const Theme& theme, std::string_view selector, std::string_view property) {
    return std::any_of(theme.assignments.begin(), theme.assignments.end(), [&](const Assignment& assignment) {
        return !assignment.media && assignment.selector.trigger == Trigger::Always &&
               assignment.selector.text == selector && assignment.property == property;
    });
}

// How far a thicker track rose above Apple's, its bottom kept (TrackBottom). With the bar at the top, Apple shows the
// times just above the line (FixedSlot::above): they go up as much, else a 5-pixel line covered their foot. At the
// bottom they sit on the artist's line and in the centred bar beside the line (layouts/): they stay. 0 for Apple's
// own track or a thinner one.
double TrackRise(const FixedSlot& slot, double track) {
    if (track <= slot.track) return 0;
    double bottom = TrackBottom(slot, track);
    double top = slot.height - bottom + slot.trackTop + (std::max(slot.trackRow, track) - track) / 2;
    double appleTop = slot.trackTop + (slot.trackRow - slot.track) / 2;
    return std::max(0.0, std::round(appleTop - top));
}

// The bar's place for the composed layers: the theme's, else Ampify's own layout's (no theme, apple.playbar), else Top.
bool BarAtTop(const Theme& theme, const Theme* layout) {
    for (const Theme* source : {&theme, layout}) {
        if (!source) continue;
        if (auto place = source->settings.find("bar-position"); place != source->settings.end()) {
            return ToLowerAscii(place->second) == "top";
        }
    }
    return true;
}

// The room a slot needs for its decors and its track, moved down so the track's bottom stays where Apple's is.
std::string RoomInFixedSlots(const Theme& theme, const Theme* layout) {
    std::string css;
    const bool atTop = BarAtTop(theme, layout);
    for (const FixedSlot& slot : kFixedSlots) {
        double thickness = TrackThickness(theme, slot);
        double bottom = TrackBottom(slot, thickness);
        double room = std::max({slot.height, DecorRoom(theme, slot), bottom});
        const std::string full(slot.selector);
        if (room <= slot.height || Sets(theme, slot.name, "Height") || Sets(theme, full, "Height") ||
            Sets(theme, slot.name, "RenderTransform") || Sets(theme, full, "RenderTransform")) {
            continue;
        }
        double shift = room - bottom;
        css += std::string(slot.name) + " { height: " + FormatNumber(room) + ";";
        if (shift > 0) css += " transform: translateY(" + FormatNumber(shift) + "px);";
        css += " }\n";
        double rise = atTop ? TrackRise(slot, thickness) : 0;
        for (std::string_view name : slot.above) {
            if (rise <= 0 || Sets(theme, name, "RenderTransform")) continue;
            css += std::string(name) + " { transform: translateY(-" + FormatNumber(rise) + "px); }\n";
        }
    }
    return css;
}

// Rules a theme's own choices call for, below its user.css so that a theme which sets the same thing keeps its value.
void AddDerivedRules(const ValueContext& context, Theme& theme, const ThemeParts& parts) {
    std::string css = ScrubberClearOfCorners(theme) + OneLightPerSong(theme) + RoomInsideALitCard(theme) +
                      LcdVeilFollowsArtwork(theme) + SmoothProgressLine(theme) + SuggestionsAgainstTheBox(theme);
    if (std::string room = RoomInFixedSlots(theme); !room.empty()) {
        RuleSource slots;
        slots.file = kFixedSlotSource;
        slots.layer = -1;
        slots.applied = parts.resources;
        LoadRuleText(room, context, theme, slots);
    }
    if (parts.colors) {
        css += ScrubberInAccent(theme) + NowPlayingBarsInAccent(theme) + ShuffleInAccent(theme) + UpNextInAccent(theme) +
               ArtistInLinkColour(theme) +
               CardLinksInSecondaryText(theme) + DisplayTextInThemeColours(theme) +
               SearchTilesInCardColour(theme) + SelectedRowsInThemeColour(theme);
    }
    if (css.empty()) return;
    RuleSource source;
    source.file = "Ampify (derived from the theme)";
    source.layer = -1;
    source.applied = parts.resources;
    LoadRuleText(css, context, theme, source);
}

}  // namespace detail

namespace {

constexpr const char* kTogglesSource = "Ampify (playback bar toggles)";
constexpr const char* kVeilSource = "Ampify (the veil over the bar's artwork)";

// Rules worked out for the composed theme, under every layer, in place of the ones a previous composition added.
void PutComposedRules(Theme& composed, const char* name, const std::string& css) {
    std::erase_if(composed.assignments, [&](const Assignment& assignment) { return assignment.source == name; });
    if (css.empty()) return;
    struct NoFiles final : FileSource {
        std::optional<std::string> Read(std::string_view) override { return std::nullopt; }
        bool Exists(std::string_view) override { return false; }
    } files;
    ValueContext context{composed.palette, files, std::string(), false};
    RuleSource source;
    source.file = name;
    source.layer = -1;
    Theme derived;
    LoadRuleText(css, context, derived, source);
    for (Assignment& assignment : derived.assignments) composed.assignments.push_back(std::move(assignment));
}

}  // namespace

void ShareRoomInFixedSlots(Theme& composed, const Theme* layout) {
    std::erase_if(composed.assignments,
                  [](const Assignment& assignment) { return assignment.source == kFixedSlotSource; });
    std::string css = RoomInFixedSlots(composed, layout);
    if (css.empty()) return;
    struct NoFiles final : FileSource {
        std::optional<std::string> Read(std::string_view) override { return std::nullopt; }
        bool Exists(std::string_view) override { return false; }
    } files;
    ValueContext context{composed.palette, files, std::string(), false};
    RuleSource source;
    source.file = kFixedSlotSource;
    source.layer = -1;
    Theme derived;
    LoadRuleText(css, context, derived, source);
    for (Assignment& assignment : derived.assignments) composed.assignments.push_back(std::move(assignment));
}

void KeepTogglesAtTheRightEnd(Theme& composed) {
    PutComposedRules(composed, kTogglesSource, TogglesAtTheRightEnd(composed));
}

// The icon in the veil over the bar's artwork stretches to its font's box, wider than the artwork: the veil grew with
// it past the artwork's right edge and was cut there, square under its rounded corners (Cinema, Glass, Console, Cosy,
// Mirror). A small centred icon leaves the veil the artwork's size.
void KeepLcdVeilInItsArtwork(Theme& composed) {
    constexpr std::string_view kIcon = ".lcd-artwork-hover > FontIcon";
    if (Sets(composed, kIcon, "Width")) return;
    PutComposedRules(composed, kVeilSource,
                     std::string(kIcon) + " { width: 32px; height: 32px; horizontal-alignment: center; "
                                          "vertical-alignment: center; }\n");
}

}  // namespace ampify::theme
