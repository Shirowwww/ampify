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
    {"page-background", {"ContentPresenter#PageBackground"}},
    // Apple Music paints each settings page in code, like the content pages: they are pages too.
    {"page",
     {"ContentPage", "SongsContentPage", "SettingsPage", "MainSettingsPage", "GeneralSettingsPage",
      "PlaybackSettingsPage", "RestrictionsSettingsPage", "FilesSettingsPage", "AdvancedSettingsPage",
      "ImportSettingsPage"}},
    // One name per page layout, to give a kind of page its own look: Home is a stacked page, an album or a playlist a
    // list page, Albums a grid page, Artists a side-by-side page.
    {"stacked-page", {"Grid#StackedPageContent"}},
    {"list-page", {"Grid#FixedListPageContent"}},
    {"grid-page", {"Grid#FixedGridPageContent", "Grid#FixedGridWithoutHeaderBarPageContent"}},
    {"side-by-side-page", {"Grid#SideBySidePageContent"}},
    {"table-page", {"Grid#DataGridPageContent"}},
    {"section-page", {"Grid#SingleSectionPageContent"}},
    {"tracklist-page", {"Grid#TracklistPageContent"}},
    {"songs-page", {"SongsContentPage"}},
    {"settings-page",
     {"SettingsPage", "MainSettingsPage", "GeneralSettingsPage", "PlaybackSettingsPage", "RestrictionsSettingsPage",
      "FilesSettingsPage", "AdvancedSettingsPage", "ImportSettingsPage"}},
    {"settings-card", {"SettingContainerView", "MainSettingsPage Button"}},
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
    {"list-header", {"Grid#HeaderStack"}},
    {"list-page-shelves", {"Grid#FixedListPageContent StackPanel#ShelfSectionContent"}},

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
    {"now-playing-indicator", {"BouncyBarControl#BouncyBarAnimation"}},  // the bars: their Grid has no colour

    // Albums, playlists and songs
    {"album-header", {"Grid#ContainerDetailHeaderLockupRegularContent", "Grid#AlbumDetailHeaderLockupContent"}},
    {"album-play-button", {"Button#PlayButtonElement"}},
    {"album-shuffle-button", {"Button#ShuffleButtonElement"}},
    {"album-description", {"ContentControl#DescriptionText"}},
    {"album-artist",
     {"Grid#ContainerDetailHeaderLockupRegularContent > Grid > StackPanel > Grid > TextBlock",
      "Grid#AlbumDetailHeaderLockupContent > Grid > StackPanel > TextBlock"}},
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
    {"list-item", {"ListViewItemPresenter"}},

    // Search
    {"search-top-spacer", {"SearchPage > Grid > Grid:first-child"}},
    {"search-header", {"Grid#SearchPageHeader"}},
    {"search-scopes", {"Grid#SearchPageSelector"}},
    {"search-scope", {"ToggleButton#SearchAppleMusic", "ToggleButton#SearchLibrary", "ToggleButton#SearchStore"}},
    {"search-status", {"TextBlock#SearchPageStatus"}},
    {"search-top-result", {"Frame#SearchContentFrame TileControl > HoverGrid#RootGrid > ContentPresenter > Grid"}},
    // Apple's full-screen player, a window of its own: only selectors that name it reach it (selector.cpp).
    {"fullscreen-player", {"FullScreenPlayerPage"}},
    {"fullscreen-background", {"FullScreenPlayerPage ImmersionLockup#immersionLockup"}},
    {"fullscreen-artwork", {"FullScreenPlayerView ArtworkImage#ActualArtwork"}},
    {"fullscreen-title", {"FullScreenPlayerView TickerTapeControl#TrackTitleTickerTape TextBlock#ScrollingText"}},
    {"fullscreen-artist", {"FullScreenPlayerView TickerTapeControl#ArtistTitleTickerTape TextBlock#ScrollingText"}},
    {"fullscreen-transport", {"FullScreenPlayerView Grid#TransportGrid"}},
    {"fullscreen-time", {"FullScreenPlayerView TextBlock#CurrentTime", "FullScreenPlayerView TextBlock#Duration"}},
    {"fullscreen-lyrics", {"FullScreenPlayerView LyricsHostView#LyricsView"}},
    {"fullscreen-exit", {"FullScreenPlayerView Button#ExitFullScreenButton", "FullScreenPlayerView Button#CloseFullScreenButton"}},
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
    {"lyrics-panel-top-spacer", {"SplitView#ContentSplitView > Grid > Grid#PaneRoot > Border > Grid > Grid:first-child"}},
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

/* Apple's line under the bar started at the sidebar panel's edge, a seam against its rounded side. */
.playbar {
    background: transparent;
    border-width: 0;
}

.sidebar {
    background: var(--panel-background, #17171A);
    corner-radius: var(--panel-radius, 10px);
    margin: var(--gap, 8) calc(var(--gap, 8) * -1) var(--gap, 8) var(--gap, 8);
}

/* Apple Music pins the back arrow to the window's corner, where it straddled the sidebar panel's rounded corner: it
   moves into the panel, centred on the line of the app's name (4 lower than the panel's edge plus a gap). */
.back-button {
    margin: calc(var(--gap, 8) + 4) 0 0 calc(var(--gap, 8) + 4);
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

/* Apple Music lays an artist's video and a page's coloured backdrop 64 down, under its own playback bar. The panel
   already starts below the bar: they go back up to its top edge, where a dark band showed above them. */
.page-background {
    margin: -64 0 0 0;
}

/* The lyrics and queue pane: a third panel on the right, below the bar like the pages, instead of a strip glued to the
   window's edge and running up under its buttons. Its first 64 were room for Apple's bar: the panel starts below it. */
.lyrics-panel {
    background: var(--panel-background, #17171A);
    corner-radius: var(--panel-radius, 10px);
    margin: var(--content-top, 64) var(--gap, 8) var(--gap, 8) 0;
}

.lyrics-panel-top-spacer {
    height: var(--page-top, 8);
}

/* The column titles of the Songs list stay at the top as it scrolls: they take the panel's colour, not Apple's grey. */
.list-header {
    background: var(--panel-background, #17171A);
}

/* Under an album or a playlist, Apple paints the other shelves on a grey band: a shade of the panel instead. */
.list-page-shelves {
    background: var(--band-background, #1E1E22);
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

/* The song stands a gap in from the window's edge, and the bar's height around its 54-pixel artwork leaves it square:
   flush with the corner, it looked cut off. */
.now-playing {
    grid-column: 0;
    margin: 4 0 4 var(--gap, 8);
}

/* With nothing loaded, Apple Music centres its logo in the display, far from everything else on the bar. Its opacity,
   not its visibility: Apple Music binds the visibility to what plays, so the value read before the theme would come
   back stale when the theme goes, and show the logo over the title. */
.apple-logo {
    opacity: 0;
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

/* The pane starts below the window's buttons, which sit at the top right with no bar under them any more. */
.lyrics-panel {
    margin: var(--caption-room, 40) var(--gap, 8) var(--bar-room, 80) 0;
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

// Scroll bars that sit inside their panel: off its edge, clear of its rounded corners, without the arrows Windows adds
// with "Always show scroll bars" (they stuck out at both ends). Part of floating-panels; alone for the other layouts.
constexpr std::string_view kScrollbars = R"css(
ScrollBar RepeatButton#VerticalSmallDecrease,
ScrollBar RepeatButton#VerticalSmallIncrease,
ScrollBar RepeatButton#HorizontalSmallDecrease,
ScrollBar RepeatButton#HorizontalSmallIncrease {
    visibility: collapsed;
}

.scrollbar {
    background: transparent;
}

ScrollBar#VerticalScrollBar {
    margin: var(--panel-radius, 8px) 3 var(--panel-radius, 8px) 0;
}

ScrollBar#HorizontalScrollBar {
    margin: 0 var(--panel-radius, 8px) 3 var(--panel-radius, 8px);
}
)css";

struct BuiltinEntry {
    std::string_view name;
    std::vector<std::string_view> parts;
};

const std::array<BuiltinEntry, 3> kBuiltins{{
    {"floating-panels", {kFloatingPanels, kScrollbars}},
    {"bottom-bar", {kBottomBar}},
    {"scrollbars", {kScrollbars}},
}};

constexpr size_t kMaxExpansions = 32;

bool IsClassChar(char c) {
    return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-';
}

bool IsTypeChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
}

// A class starts a part of a selector: at the start, or after a space or a combinator (>, +, ~).
bool StartsPart(std::string_view selector, size_t at) {
    if (at == 0) return true;
    char before = selector[at - 1];
    return before == '>' || before == '+' || before == '~' || std::isspace(static_cast<unsigned char>(before));
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

        // A class stands for a path of several parts, which :not() cannot hold.
        if (i > 0 && selector[i - 1] == '(') {
            return fail("a class cannot go inside :not(): write a type, a #name or a structural pseudo-class, in \"" +
                        std::string(selector) + "\"");
        }
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

std::optional<std::string> BuiltinStylesheet(std::string_view name) {
    for (const BuiltinEntry& entry : kBuiltins) {
        if (entry.name != name) continue;
        std::string css;
        for (std::string_view part : entry.parts) css += part;
        return css;
    }
    return std::nullopt;
}

std::vector<std::string> BuiltinStylesheetNames() {
    std::vector<std::string> names;
    for (const BuiltinEntry& entry : kBuiltins) names.emplace_back(entry.name);
    return names;
}

}  // namespace ampify::theme
