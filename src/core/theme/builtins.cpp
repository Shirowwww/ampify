#include "builtins.hpp"

#include <array>
#include <cctype>
#include <utility>

namespace ampify::theme {

namespace {

struct ClassEntry {
    std::string_view name;
    std::vector<std::string_view> selectors;
};

// Apple Music 1.1792, from inspector surveys of every page (docs/themes/names.md describes each name). One line per
// zone; a zone with several forms lists each. The playback bar is a Grid of three columns: buttons, the now-playing
// display, then volume and toggles.
const std::vector<ClassEntry> kClasses{
    // Window
    {"window", {"Grid#PageRoot"}},
    {"titlebar", {"Border#Titlebar"}},
    {"titlebar-drag-area", {"Canvas#DraggableTitlebarRegion"}},
    {"caption-buttons-cover", {"Border#CaptionButtonsOcclusionBorder"}},
    {"modal", {"ContentPresenter#ModalContentPresenter"}},
    {"info-bar", {"InfoBar#topInfoBar", "InfoBar#bottomInfoBar"}},
    {"scrollbar", {"ScrollBar#VerticalScrollBar", "ScrollBar#HorizontalScrollBar"}},

    // Sidebar
    {"sidebar", {"SplitView#RootSplitView > Grid > Grid#PaneRoot"}},
    {"navigation", {"NavigationView#NavView"}},
    {"sidebar-header", {"Grid#PaneHeaderGrid"}},
    {"sidebar-toggle", {"Button#TogglePaneButton"}},
    {"back-button", {"Button#NavigationViewBackButton"}},
    {"app-name", {"TextBlock#AppName"}},
    {"search-area", {"Grid#AutoSuggestArea"}},
    {"search-box", {"AutoSuggestBox"}},
    {"sidebar-scroll", {"ScrollViewer#MenuItemsScrollViewer"}},
    {"sidebar-items", {"ItemsRepeater#MenuItemsHost"}},
    {"sidebar-item", {"NavigationViewItem"}},
    {"sidebar-item-artwork", {"HoverGrid#ArtworkGrid"}},
    {"sidebar-play-button", {"Button#PlayButtonWithBackground"}},
    {"sidebar-footer", {"ContentControl#FooterContentBorder"}},
    {"sidebar-footer-items", {"ItemsRepeater#FooterMenuItemsHost"}},
    {"sidebar-separator", {"NavigationViewItemSeparator#Separator"}},
    {"account-picture", {"PersonPicture#smallPersonPicture"}},
    {"account-name", {"TextBlock#lowerAccountName"}},

    // Content and pages
    {"main-column",
     {"SplitView#RootSplitView > Grid > Grid#ContentRoot > Border",
      "SplitView#RootSplitView > Grid > Grid#ContentRoot > Border > Grid#ContentGrid"}},
    {"content", {"SplitView#ContentSplitView > Grid > Grid#ContentRoot"}},
    {"content-background", {"SplitView#ContentSplitView > Grid > Grid#ContentRoot > Border > Grid"}},
    {"content-frame", {"Frame#ContentFrame"}},
    {"content-shadow", {"Grid#ContentPageShadowGrid"}},
    {"page", {"ContentPage"}},
    {"page-top-spacer",
     {"Grid#FixedGridPageContent > Grid:first-child", "Grid#FixedListPageContent > Grid:first-child",
      "Grid#SideBySidePageContent > Grid:first-child", "Grid#SingleSectionPageContent > Grid:first-child",
      "Grid#DataGridPageContent > Grid:first-child"}},
    {"stacked-list", {"Grid#StackedPageContent > ListView"}},
    {"page-header", {"Grid#PageHeaderBar"}},
    {"page-title", {"TextBlock#PageHeaderTitle"}},
    {"frost", {"BackDrop"}},
    {"filter-box", {"TextBox#FilterTextBox"}},
    {"filter-button", {"ToggleButton#FilterButton"}},
    {"sort-button", {"Button#SortButton"}},
    {"sticky-header-background", {"Border#StickyBackgroundBorder"}},

    // Shelves and sections
    {"section-header", {"ContentControl#SectionHeader"}},
    {"shelf", {"ShelfView"}},
    {"shelf-title", {"Grid#DefaultSectionHeaderContent"}},
    {"shelf-grid", {"GridView#ContentGridView"}},
    {"shelf-arrows", {"Grid#ArrowGrid"}},
    {"shelf-arrow-left", {"Button#LeftArrowButton"}},
    {"shelf-arrow-right", {"Button#RightArrowButton"}},

    // Cards
    {"card", {"HoverGrid#SquareLockupContent"}},
    {"card-brick",
     {"HoverGrid#BrickLockupContent", "HoverGrid#BrickLockupSmallContent", "HoverGrid#SearchLandingBrickLockup"}},
    {"card-wide", {"TileControl#HorizontalLockupContent"}},
    {"card-track", {"TileControl#TrackLockupShelfTracklistContent"}},
    {"artwork", {"ArtworkImage"}},
    {"artwork-shadow", {"Border#ShadowElement"}},
    {"card-play-button", {"HoverGrid#SquareLockupContent Button#PlayButton"}},
    {"card-title", {"TitleSubtitleControl TextBlock#TitleTextBlock"}},
    {"card-subtitle", {"TitleSubtitleControl TextBlock#SubtitleTextBlock"}},
    {"explicit-badge", {"FontIcon#ExplicitBadgeFontIcon"}},
    {"favorite-button", {"Button#FavoriteButton"}},
    {"favorite-icon", {"FontIcon#FavoriteIcon"}},
    {"add-button", {"Button#AddButton"}},
    {"download-button", {"Button#DownloadButton"}},
    {"downloaded-button", {"Button#DownloadedButton"}},
    {"download-progress", {"ProgressRing#DownloadingProgress"}},
    {"now-playing-indicator", {"Grid#NowPlayingIndicator"}},

    // Albums, playlists and songs
    {"album-header", {"Grid#ContainerDetailHeaderLockupRegularContent"}},
    {"album-play-button", {"Button#PlayButtonElement"}},
    {"album-shuffle-button", {"Button#ShuffleButtonElement"}},
    {"album-description", {"ContentControl#DescriptionText"}},
    {"add-description-button", {"Button#AddDescriptionButton"}},
    {"share-button", {"Button#ShareButton"}},
    {"lossless-badge", {"Canvas#LosslessImage"}},
    {"digital-master-badge", {"Canvas#DigitalMasterImage"}},
    {"motion-artwork", {"MediaPlayerElement#MotionArtworkPlayer"}},
    {"artist-picture", {"PersonPicture#ArtworkPicture"}},
    {"radio-play-button", {"Button#RadioPlayButton"}},
    {"track-list", {"TracklistGrid#ColumnsTracklistGrid"}},
    {"track-row", {"TracklistGrid#RootGrid"}},
    {"track-play-button", {"Button#PlayPauseButton"}},
    {"reactions-button", {"Button#ReactionsButton"}},
    {"song-list", {"ListView#DataGridListView"}},
    {"song-row", {"StackPanel#DataGridRowStack"}},

    // Search
    {"search-top-spacer", {"SearchPage > Grid > Grid:first-child"}},
    {"search-header", {"Grid#SearchPageHeader"}},
    {"search-scopes", {"Grid#SearchPageSelector"}},
    {"search-scope", {"ToggleButton#SearchAppleMusic", "ToggleButton#SearchLibrary", "ToggleButton#SearchStore"}},
    {"search-status", {"TextBlock#SearchPageStatus"}},

    // Playback bar
    {"playbar", {"Grid#TransportBar"}},
    {"playbar-controls", {"Grid#TransportBar > Grid:nth-child(1)"}},
    {"now-playing", {"Grid#TransportBar > Grid:nth-child(2)"}},
    {"playbar-extras", {"Grid#TransportBar > Grid:nth-child(3)"}},
    {"transport", {"TransportControl#TransportControl"}},
    {"shuffle-button", {"ToggleButton#ShuffleButton"}},
    {"shuffle-dot", {"Ellipse#ShuffleButtonDot"}},
    {"previous-button", {"TransportControl#TransportControl > StackPanel > AppBarButton:nth-child(2)"}},
    {"play-button", {"TransportControl#TransportControl > StackPanel > AppBarButton:nth-child(3)"}},
    {"next-button", {"TransportControl#TransportControl > StackPanel > AppBarButton:nth-child(4)"}},
    {"repeat-button", {"Button#RepeatButton"}},
    {"repeat-dot", {"Ellipse#RepeatButtonDot"}},
    {"lcd", {"NowPlayingControl#LCD > Grid#LCD"}},
    {"lcd-artwork", {"HoverGrid#ThumbnailHoverGrid"}},
    {"lcd-artwork-image", {"ArtworkImage#ActualArtwork"}},
    {"lcd-placeholder", {"Border#PlaceHolderArtwork"}},
    {"track-text", {"StackPanel#TrackTitleAndArtist"}},
    {"track-lines", {"Grid#TickerTapeGrid"}},
    {"track-line", {"Grid#TickerTapeGrid > Grid"}},
    {"track-title", {"TickerTapeControl#TrackTitleTickerTape"}},
    {"track-artist", {"TickerTapeControl#ArtistTitleTickerTape"}},
    {"scrubber", {"ScrubberControl#LCDScrubber"}},
    {"current-time", {"TextBlock#CurrentTime"}},
    {"duration", {"TextBlock#Duration"}},
    {"audio-badge", {"AudioBadgeControl#AudioBadgeControl"}},
    {"live-badge", {"Grid#Live"}},
    {"autoplay-badge", {"Border#AutoPlayBadge"}},
    {"apple-logo", {"Image#Apple"}},
    {"lcd-more-button", {"MenuButton#MoreButton"}},
    {"playbar-toggles", {"StackPanel#NowPlayingToggleButtons"}},
    {"lyrics-button", {"ToggleButton#LyricsToggleButton"}},
    {"queue-button", {"ToggleButton#PlayQueueToggleButton"}},
    {"airplay-button", {"Button#AirPlayButton"}},
    {"volume", {"VolumeControl#VolumeControl"}},
    {"volume-button", {"Button#VolumeButton"}},
    {"volume-slider", {"Slider#VolumeSlider"}},
    {"volume-mute-button", {"Button#MuteButton"}},

    // Popups: flyouts, menus and tooltips, each a root of its own outside the window's tree
    {"flyout", {"Popup > FlyoutPresenter"}},
    {"menu", {"Popup > MenuFlyoutPresenter"}},
    {"menu-item", {"MenuFlyoutItem", "ToggleMenuFlyoutItem", "MenuFlyoutSubItem"}},
    {"menu-separator", {"MenuFlyoutSeparator"}},
    {"tooltip", {"Popup > ToolTip"}},

    // Lyrics and queue panels
    {"lyrics-panel", {"SplitView#ContentSplitView > Grid > Grid#PaneRoot"}},
    {"lyrics", {"LyricsHostView#LyricsView"}},
    {"lyrics-menu-button", {"Button#LyricsMenuButton"}},
    {"lyrics-synced", {"KaraokeLyricsView#RootControl"}},
    {"lyrics-static", {"ListView#StaticLyrics"}},
    {"queue", {"PlayQueueView#PlayQueueView"}},
};

// Rounded panels floating on the window, with even gaps and aligned edges: the sidebar on the left, the pages on the
// right below the playback bar. Colours come from variables a theme sets in its :root. The title bar strip keeps its
// own background: with a colour, it catches the clicks meant for the buttons under it.
constexpr std::string_view kFloatingPanels = R"css(
.window {
    background: var(--window-background, #0B0B0D);
}

.navigation,
.main-column,
.page,
.content-background {
    background: transparent;
}

.main-column {
    border-width: 0;
}

.frost {
    visibility: collapsed;
}

.playbar {
    background: transparent;
}

.sidebar {
    background: var(--panel-background, #17171A);
    corner-radius: var(--panel-radius, 10px);
    margin: var(--gap, 8) calc(var(--gap, 8) * -1) var(--gap, 8) var(--gap, 8);
}

/* Folded, the sidebar shows the first 48 pixels from its own edge, so it ends a gap into the page column: the pages
   start two gaps into it, and the open sidebar reaches one gap into it. The space between the panels is one gap either
   way. Wider than 48 shows the start of the texts Apple Music hides there. */
.navigation {
    compact-pane-length: var(--compact-pane, 48);
}

.content {
    background: var(--panel-background, #17171A);
    corner-radius: var(--panel-radius, 10px);
    margin: var(--content-top, 64) var(--gap, 8) var(--gap, 8) calc(var(--gap, 8) * 2);
}

.page-top-spacer {
    height: var(--page-top, 8);
}

.stacked-list {
    padding: var(--page-top, 8) 0 0 0;
}

/* The search page's header is the top of the panel, and the results scroll in from under it (48 high). */
.search-top-spacer {
    height: 0;
}

.search-header {
    background: var(--panel-background, #17171A);
    corner-radius: var(--panel-radius, 10px) var(--panel-radius, 10px) 0 0;
}

Frame#SearchContentFrame .stacked-list {
    padding: calc(48 + var(--gap, 8)) 0 0 0;
}
)css";

// The playback bar along the bottom of the window, under both panels: the song on the left, the controls in the
// middle, volume and toggles on the right. Pairs with floating-panels (import it first).
constexpr std::string_view kBottomBar = R"css(
.playbar {
    vertical-alignment: bottom;
    height: var(--bar-height, 72);
    margin: 0 0 0 var(--bar-reach, calc(var(--gap, 8) - 290));
}

/* At 1324 pixels and less Apple Music folds the sidebar to its icons: the page column starts there instead. */
@media (max-width: 1324px) {
    .playbar {
        margin: 0 0 0 var(--bar-reach-folded, calc(var(--gap, 8) - var(--compact-pane, 48)));
    }
}

.playbar-controls {
    grid-column: 1;
}

.now-playing {
    grid-column: 0;
}

/* The song's title and artist start next to its artwork instead of in the middle of the display. */
.track-lines {
    grid-column: 0;
    grid-column-span: 3;
    margin: 0 0 0 12;
    horizontal-alignment: left;
}

.track-line {
    horizontal-alignment: left;
}

/* Apple Music centres each line between a spacer and the More button; at the left, the title would sit apart from the
   artist. */
.track-title,
.track-artist {
    grid-column: 0;
    grid-column-span: 2;
    horizontal-alignment: left;
}

/* Apple Music pins the Lossless and Dolby Atmos badge to the display's top left corner, clear of its centred title.
   The title starts there now, so the badge moves to the other corner, just before the favourite star. */
.audio-badge {
    horizontal-alignment: right;
    margin: 2 36 2 1;
}

/* Volume, lyrics and queue at the right end of the bar. */
.playbar-toggles {
    horizontal-alignment: right;
    margin: 0 16 0 0;
}

.frost {
    visibility: collapsed;
}

.sidebar {
    margin: var(--gap, 8) calc(var(--gap, 8) * -1) var(--bar-room, 80) var(--gap, 8);
}

.content {
    margin: var(--gap, 8) var(--gap, 8) var(--bar-room, 80) calc(var(--gap, 8) * 2);
}

.page-top-spacer {
    height: var(--page-top, 40);
}

.stacked-list {
    padding: var(--page-top, 40) 0 0 0;
}

/* The window's buttons sit over the right end of the search header: its scope buttons stop before them. */
.search-header {
    padding: 0 150 0 40;
}
)css";

struct BuiltinEntry {
    std::string_view name;
    std::string_view css;
};

const std::array<BuiltinEntry, 2> kBuiltins{{
    {"floating-panels", kFloatingPanels},
    {"bottom-bar", kBottomBar},
}};

constexpr size_t kMaxExpansions = 32;

bool IsClassChar(char c) {
    return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-';
}

bool IsTypeChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
}

// A class starts a part of a selector: at the start, or after a space or ">".
bool StartsPart(std::string_view selector, size_t at) {
    return at == 0 || selector[at - 1] == '>' || std::isspace(static_cast<unsigned char>(selector[at - 1]));
}

}  // namespace

std::optional<std::vector<std::string>> ClassSelectors(std::string_view name) {
    for (const ClassEntry& entry : kClasses) {
        if (entry.name == name) return std::vector<std::string>(entry.selectors.begin(), entry.selectors.end());
    }
    return std::nullopt;
}

std::vector<std::string> ClassNames() {
    std::vector<std::string> names;
    for (const ClassEntry& entry : kClasses) names.emplace_back(entry.name);
    return names;
}

std::optional<std::vector<std::string>> ExpandClasses(std::string_view selector, std::string* problem) {
    auto fail = [&](std::string reason) -> std::optional<std::vector<std::string>> {
        if (problem) *problem = std::move(reason);
        return std::nullopt;
    };

    std::vector<std::string> results{""};
    size_t i = 0;
    while (i < selector.size()) {
        if (selector[i] != '.') {
            for (std::string& result : results) result += selector[i];
            i++;
            continue;
        }
        size_t end = i + 1;
        while (end < selector.size() && IsClassChar(selector[end])) end++;
        std::string_view name = selector.substr(i + 1, end - i - 1);
        std::optional<std::vector<std::string>> alternatives = ClassSelectors(name);

        if (!StartsPart(selector, i)) {
            // A dot inside a full type name stays; a known class glued to a type is a mistake worth naming.
            if (alternatives && (end == selector.size() || !IsTypeChar(selector[end]))) {
                return fail("a class starts its own part: write \"." + std::string(name) + "\" alone, not glued to a type, in \"" +
                            std::string(selector) + "\"");
            }
            for (std::string& result : results) result += '.';
            i++;
            continue;
        }
        if (!alternatives) {
            return fail("unknown class \"." + std::string(name) + "\" (docs/themes/names.md lists them), in \"" +
                        std::string(selector) + "\"");
        }

        std::vector<std::string> next;
        for (const std::string& result : results) {
            for (const std::string& alternative : *alternatives) next.push_back(result + alternative);
        }
        if (next.size() > kMaxExpansions) return fail("too many combinations of classes in \"" + std::string(selector) + "\"");
        results = std::move(next);
        i = end;
    }
    return results;
}

std::optional<std::string_view> BuiltinStylesheet(std::string_view name) {
    for (const BuiltinEntry& entry : kBuiltins) {
        if (entry.name == name) return entry.css;
    }
    return std::nullopt;
}

std::vector<std::string> BuiltinStylesheetNames() {
    std::vector<std::string> names;
    for (const BuiltinEntry& entry : kBuiltins) names.emplace_back(entry.name);
    return names;
}

}  // namespace ampify::theme
