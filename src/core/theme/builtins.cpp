#include "builtins.hpp"

#include "builtin_layouts.hpp"
#include "css_values.hpp"

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
// zone; a zone with several forms lists each. The playback bar is a Grid of four columns: buttons, the now-playing
// display, volume and toggles, then the cover Apple keeps under the window buttons.
const std::vector<ClassEntry> kClasses{
    // Window
    {"window", {"Grid#PageRoot"}},
    {"titlebar", {"Border#Titlebar"}},
    {"titlebar-drag-area", {"Canvas#DraggableTitlebarRegion"}},
    {"caption-buttons-cover", {"Border#CaptionButtonsOcclusionBorder"}},
    {"modal", {"ContentPresenter#ModalContentPresenter"}},
    {"focus-ring", {"Border#FocusBorder"}},
    {"popup-scrim", {"Rectangle#LightDismissLayer"}},
    {"info-bar", {"InfoBar#topInfoBar", "InfoBar#bottomInfoBar"}},
    {"info-bar-icon", {"InfoBar Grid#StandardIconArea", "InfoBar Viewbox#UserIconBox"}},
    {"info-bar-title", {"InfoBar TextBlock#Title"}},
    {"info-bar-message", {"InfoBar TextBlock#Message"}},
    {"info-bar-content", {"InfoBar ContentPresenter#ContentArea"}},
    {"info-bar-close", {"InfoBar Button#CloseButton"}},
    {"scrollbar", {"ScrollBar#VerticalScrollBar", "ScrollBar#HorizontalScrollBar"}},
    {"scrollbar-thumb", {"ScrollBar#VerticalScrollBar Thumb", "ScrollBar#HorizontalScrollBar Thumb"}},
    {"scrollbar-track",
     {"ScrollBar#VerticalScrollBar Rectangle#VerticalTrackRect",
      "ScrollBar#HorizontalScrollBar Rectangle#HorizontalTrackRect"}},
    {"scrollbar-arrow",
     {"ScrollBar#VerticalScrollBar RepeatButton#VerticalSmallDecrease",
      "ScrollBar#VerticalScrollBar RepeatButton#VerticalSmallIncrease",
      "ScrollBar#HorizontalScrollBar RepeatButton#HorizontalSmallDecrease",
      "ScrollBar#HorizontalScrollBar RepeatButton#HorizontalSmallIncrease"}},

    // Dialogs: a ContentDialog's template, whatever the dialog's own type (GDPRWelcomeDialog, AskUserDialog...). Each
    // opens in a popup of its own, so a rule must start at the dialog: `.window .dialog` matches nothing. The box is the
    // painted layer: its colour and corners are the dialog's.
    {"dialog", {"Border#Container > Grid#LayoutRoot > Border#BackgroundElement"}},
    {"dialog-scrim", {"Popup > Rectangle#SmokeLayerBackground"}},
    {"dialog-title", {"Border#BackgroundElement ContentControl#Title"}},
    {"dialog-content", {"Border#BackgroundElement ContentPresenter#Content"}},
    {"dialog-buttons", {"Border#BackgroundElement Grid#CommandSpace"}},
    {"dialog-primary", {"Border#BackgroundElement Grid#CommandSpace > Button#PrimaryButton"}},
    {"dialog-secondary", {"Border#BackgroundElement Grid#CommandSpace > Button#SecondaryButton"}},
    {"dialog-close", {"Border#BackgroundElement Grid#CommandSpace > Button#CloseButton"}},
    {"dialog-checkbox", {"Border#BackgroundElement CheckBox"}},

    // Sidebar
    {"sidebar", {"SplitView#RootSplitView > Grid > Grid#PaneRoot"}},
    {"navigation", {"NavigationView#NavView"}},
    {"sidebar-header", {"Grid#PaneHeaderGrid"}},
    {"sidebar-toggle", {"Button#TogglePaneButton"}},
    {"sidebar-toggle-area", {"Grid#PaneToggleButtonGrid"}},
    {"sidebar-column", {"SplitView#RootSplitView > Grid > Grid#PaneRoot > Border > Grid#PaneContentGrid"}},
    // apple.sidebar = hover: Ampify's pin button, beside "···" in the open sidebar's header.
    {"sidebar-pin", {"Button#AMPIFY_SidebarPin"}},
    {"back-button", {"Button#NavigationViewBackButton"}},
    {"app-name", {"TextBlock#AppName"}},
    {"search-area", {"Grid#AutoSuggestArea"}},
    {"search-box", {"AutoSuggestBox"}},
    {"search-suggestions", {"Popup#SuggestionsPopup"}},
    {"search-input", {"AutoSuggestBox TextBox#TextBox"}},
    {"search-placeholder", {"AutoSuggestBox ContentControl#PlaceholderTextContentPresenter"}},
    {"search-button", {"AutoSuggestBox Button#QueryButton"}},
    {"search-clear", {"AutoSuggestBox Button#DeleteButton"}},
    // The "..." button at the top of the sidebar (its menu holds Settings). .lcd-more-button was its first name, written
    // for the playback bar's display, which has no such button.
    {"more-button", {"ContentControl#PaneHeaderContentBorder MenuButton#MoreButton"}},
    {"more-icon", {"ContentControl#PaneHeaderContentBorder MenuButton#MoreButton ContentPresenter#Icon"}},
    // The row of buttons that replaces the sidebar's header in a narrow window.
    {"topnav", {"StackPanel#TopNavArea"}},
    // apple.topbar = search: the strip at the top of the window that holds Apple's search field and a back arrow.
    {"topbar-back", {"Button#AMPIFY_TopbarBack"}},
    {"topbar-search", {"Grid#AMPIFY_TopbarSearch"}},
    {"sidebar-scroll", {"ScrollViewer#MenuItemsScrollViewer"}},
    {"sidebar-items", {"ItemsRepeater#MenuItemsHost"}},
    {"sidebar-item", {"NavigationViewItem"}},
    {"sidebar-item-artwork", {"HoverGrid#ArtworkGrid"}},
    {"sidebar-item-image", {"NavigationViewItem ArtworkImage#ArtworkIcon"}},
    {"sidebar-subitems", {"ItemsRepeater#NavigationViewItemMenuItemsHost"}},
    {"sidebar-chevron", {"NavigationViewItem AnimatedIcon#ExpandCollapseChevronIcon"}},
    {"sidebar-border",
     {"SplitView#RootSplitView > Grid > Grid#PaneRoot > Rectangle#HCPaneBorder"}},  // the high contrast edge
    {"sidebar-selection", {"NavigationViewItem Rectangle#SelectionIndicator"}},
    {"sidebar-count", {"ContentPresenter#InfoBadgePresenter TextBlock#BadgeNumberTextBlock"}},
    {"sidebar-drag-indicator", {"Grid#DragTopIndicator", "Grid#DragBottomIndicator"}},
    {"sidebar-play-button", {"Button#PlayButtonWithBackground"}},
    {"sidebar-footer", {"ContentControl#FooterContentBorder"}},
    {"sidebar-footer-items", {"ItemsRepeater#FooterMenuItemsHost"}},
    {"sidebar-separator", {"NavigationViewItemSeparator#Separator"}},
    // Ampify's own entries (the Marketplace, an extension's page), buttons in the panel that holds Apple's footer.
    {"sidebar-entry", {"StackPanel#AMPIFY_SidebarHost > Button"}},
    {"sidebar-footer-scroll", {"ScrollViewer#FooterItemsScrollViewer"}},
    {"account-picture", {"PersonPicture#smallPersonPicture"}},
    {"account-badge", {"PersonPicture#smallPersonPicture Grid#BadgeGrid"}},
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
    {"loading", {"Spinner#Spinner"}},
    {"frost", {"BackDrop"}},
    {"filter-box", {"TextBox#FilterTextBox"}},
    {"filter-button", {"ToggleButton#FilterButton"}},
    {"sort-button", {"Button#SortButton"}},
    {"sticky-header-background", {"Border#StickyBackgroundBorder"}},
    {"list-header", {"Grid#HeaderStack"}},
    {"list-page-shelves", {"Grid#FixedListPageContent StackPanel#ShelfSectionContent"}},

    // Shelves and sections
    {"section-header", {"ContentControl#SectionHeader"}},
    // The sections of a stacked page (Home, New) by kind, each a template of its own: Apple's titles are text, and
    // change with the language and the person, so a kind is what a theme can hide safely.
    // Apple 1.1792 makes every section a shelf (ShelfSectionContent); the kind then shows in what it holds: large
    // posters straight in the shelf's rows for the picks, tiles under no title for the banners at the top of New.
    {"section-grid",
     {"Grid#StackedPageContent StackPanel#StackedGridSectionContent",
      "Grid#StackedPageContent StackPanel#ShelfSectionContent:has(GridViewItem > ListViewItemPresenter > "
      "HoverGrid#RootGrid)"}},
    {"section-single",
     {"Grid#StackedPageContent StackPanel#SingleItemSectionContent",
      "Grid#StackedPageContent StackPanel#ShelfSectionContent:except(:has(Grid#DefaultSectionHeaderContent))"
      ":has(GridViewItem > ListViewItemPresenter > TileControl)"}},
    {"section-shelf", {"Grid#StackedPageContent StackPanel#ShelfSectionContent"}},
    {"shelf", {"ShelfView"}},
    {"shelf-title", {"Grid#DefaultSectionHeaderContent"}},
    // The title as Apple's own link control, and the text in it (a link that goes somewhere, or a plain text).
    {"shelf-title-link", {"Grid#DefaultSectionHeaderContent Hyperlink#Title"}},
    {"shelf-title-text",
     {"Grid#DefaultSectionHeaderContent Hyperlink#Title TextBlock#JustTextBlock",
      "Grid#DefaultSectionHeaderContent Hyperlink#Title TextBlock#HyperlinkTextBlock"}},
    {"shelf-subtitles", {"Grid#DefaultSectionHeaderContent ItemsRepeater#SubtitlesRepeater"}},
    {"stacked-page-header", {"ContentControl#StackedPageHeader"}},
    // The scroller of a page made of a list: Home, an album, a playlist, the Songs table. A shelf's own sideways
    // scroller is a GridView's, and stays out.
    {"page-scroll",
     {"Grid#StackedPageContent > ListView > Border > ScrollViewer#ScrollViewer",
      "Grid#FixedListPageContent > ListView > Border > ScrollViewer#ScrollViewer",
      "ListView#DataGridListView > Border > ScrollViewer#ScrollViewer"}},
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
    // Home's large editorial tiles: the item's grid and the artwork frame in it clip with Apple's own corner, so
    // .artwork alone left them rounded. A bare HoverGrid#RootGrid also names the hover grid inside wide tiles.
    {"card-poster", {"ListViewItemPresenter#Root > HoverGrid#RootGrid", "HoverGrid#RootGrid > AspectRatioGrid"}},
    // The band under a poster's text, rounded at the bottom only: themes give it "0 0 r r".
    {"card-poster-chin", {"HoverGrid#RootGrid Grid#BottomContent > Border"}},
    // Artists' pictures are round, and so is the light Apple lays over them under the pointer (an Ellipse): .artwork
    // leaves them out, or a theme's corners squared the picture under a round light.
    // :except(), not :not(): leaving the artist covers out must not make .artwork outweigh .lcd-artwork-image.
    {"artwork", {"ArtworkImage:except(#ArtworkHasImage):except(#CircleArtworkImage)"}},
    {"artist-artwork", {"ArtworkImage#ArtworkHasImage", "ArtworkImage#CircleArtworkImage"}},
    {"artwork-shadow", {"Border#ShadowElement"}},
    {"card-play-button", {"HoverGrid#SquareLockupContent Button#PlayButton"}},
    {"cover-play-button", {"Button#PlayOverCoverArtButton", "Button#PlayWithoutCoverArtButton"}},
    {"card-text-grid", {"TitleSubtitleControl > Grid > FirstColumnAdjustableGrid#FirstColumnAdjustableGrid"}},
    {"card-title", {"TitleSubtitleControl TextBlock#TitleTextBlock"}},
    {"card-subtitle", {"TitleSubtitleControl TextBlock#SubtitleTextBlock"}},
    {"explicit-badge", {"FontIcon#ExplicitBadgeFontIcon"}},
    {"explicit-group", {"StackPanel#ExplicitFavoriteContainer"}},
    {"favorite-button", {"Button#FavoriteButton"}},
    {"favorite-icon", {"FontIcon#FavoriteIcon"}},
    {"add-button", {"Button#AddButton"}},
    {"download-button", {"Button#DownloadButton"}},
    {"library-status", {"LibraryItemStatusButton"}},
    {"downloaded-button", {"Button#DownloadedButton"}},
    {"download-progress", {"ProgressRing#DownloadingProgress"}},
    {"now-playing-indicator", {"BouncyBarControl#BouncyBarAnimation"}},  // the bars: their Grid has no colour

    // Albums, playlists and songs
    // A playlist's, an album's, and an editorial playlist's of Apple (Best Songs of 2026): three headers.
    {"album-header",
     {"Grid#ContainerDetailHeaderLockupRegularContent", "Grid#AlbumDetailHeaderLockupContent",
      "Grid#PlaylistDetailHeaderLockupContent"}},
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
    // The end of the line under an album's title: its dot and the run of formats after it (Lossless, Hi-Res Lossless,
    // Dolby Atmos, Apple Digital Master), each an icon and its word, whatever the album has.
    {"audio-formats", {"TextBlock#AudioBadgeDot", "StackPanel:has(> TextBlock#AudioBadgeDot) > ContentPresenter"}},
    {"motion-artwork", {"MediaPlayerElement#MotionArtworkPlayer"}},
    {"artist-picture", {"PersonPicture#ArtworkPicture"}},
    {"radio-play-button", {"Button#RadioPlayButton"}},
    {"track-list", {"TracklistGrid#ColumnsTracklistGrid"}},
    // A playlist's rows are TracklistGrids; an album's are HoverGrids in a UserControl, the row's only child.
    // The library's Songs page draws its rows in a grid of columns (DataGridRowControl), not a track list.
    // Not the Songs page's HoverGrid#RowGrid: lit by a theme, it flashed Apple's light for 100 ms first and took
    // other colours than Apple's; those rows keep Apple's own light.
    {"track-row", {"TracklistGrid#RootGrid", "ListViewItemPresenter > UserControl > HoverGrid#RootGrid"}},
    {"track-play-button", {"Button#PlayPauseButton"}},
    {"reactions-button", {"Button#ReactionsButton"}},
    {"song-list", {"ListView#DataGridListView"}},
    {"song-row", {"StackPanel#DataGridRowStack"}},
    {"list-item", {"ListViewItemPresenter"}},
    {"track-list-item", {"Grid#FixedListPageContent > ListView ListViewItemPresenter",
                         "ListViewItemPresenter:has(> DataGridRowControl)"}},
    {"suggested-songs", {"StackPanel#SuggestedSongSectionContent"}},

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
    {"fullscreen-volume", {"FullScreenPlayerView VolumeControl"}},
    {"fullscreen-airplay", {"FullScreenPlayerView AirPlayButton"}},
    {"fullscreen-reactions", {"FullScreenPlayerView Button#ReactionsButton"}},
    {"fullscreen-audio-badge", {"FullScreenPlayerView AudioBadgeControl"}},
    // Apple's mini player, a window of its own like the full-screen one: it opens from the artwork in the playback bar.
    {"mini-player", {"MiniPlayerPage"}},
    {"mini-player-background", {"MiniPlayerPage ImmersionLockup"}},
    {"mini-lcd", {"MiniPlayerPage MiniNowPlayingControl"}},
    {"mini-player-lyrics", {"MiniPlayerPage LyricsHostView"}},
    {"mini-player-queue", {"MiniPlayerPage PlayQueueView"}},
    {"mini-player-volume", {"MiniPlayerPage VolumeControl"}},
    // Playback bar
    {"playbar", {"Grid#TransportBar"}},
    {"playbar-controls", {"Grid#TransportBar > Grid:nth-child(1)"}},
    {"now-playing", {"Grid#TransportBar > Grid:nth-child(2)"}},
    {"playbar-extras", {"Grid#TransportBar > Grid:nth-child(3)"}},
    {"transport", {"TransportControl#TransportControl"}},
    {"shuffle-button", {"ToggleButton#ShuffleButton"}},
    {"shuffle-dot", {"Ellipse#ShuffleButtonDot"}},
    {"shuffle-icon", {"FontIcon#ShuffleButtonIcon"}},
    {"transport-icon", {"TransportControl#TransportControl AppBarButton Viewbox#ContentViewbox"}},
    {"previous-button", {"TransportControl#TransportControl > StackPanel > AppBarButton:nth-child(2)"}},
    {"play-button", {"TransportControl#TransportControl > StackPanel > AppBarButton:nth-child(3)"}},
    {"next-button", {"TransportControl#TransportControl > StackPanel > AppBarButton:nth-child(4)"}},
    {"repeat-button", {"Button#RepeatButton"}},
    {"repeat-dot", {"Ellipse#RepeatButtonDot"}},
    {"repeat-icon", {"FontIcon#RepeatButtonIcon"}},
    {"lcd", {"NowPlayingControl#LCD > Grid#LCD"}},
    {"lcd-artwork", {"HoverGrid#ThumbnailHoverGrid"}},
    {"lcd-artwork-image", {"ArtworkImage#ActualArtwork"}},
    // The picture inside that frame: the frame and its area refuse a transform, the picture zooms within the frame.
    {"lcd-artwork-picture", {"ArtworkImage#ActualArtwork > AspectRatioGrid"}},
    {"lcd-artwork-hover", {"HoverGrid#ThumbnailHoverGrid Grid#ArtworkHoverContent"}},  // shown under the pointer
    {"lcd-loading", {"NowPlayingControl#LCD Rectangle#Indeterminate"}},
    {"lcd-placeholder", {"Border#PlaceHolderArtwork"}},
    {"track-text", {"StackPanel#TrackTitleAndArtist"}},
    {"track-lines", {"Grid#TickerTapeGrid"}},
    {"track-line", {"Grid#TickerTapeGrid > Grid"}},
    {"track-title", {"TickerTapeControl#TrackTitleTickerTape"}},
    {"track-artist", {"TickerTapeControl#ArtistTitleTickerTape"}},
    // Unnamed, beside the title; Apple binds its visibility to the song, so hide it with opacity, not visibility.
    {"lcd-explicit-badge", {"Grid#TickerTapeGrid > Grid > StackPanel > FontIcon"}},
    {"scrubber", {"ScrubberControl#LCDScrubber"}},
    {"current-time", {"TextBlock#CurrentTime"}},
    {"duration", {"TextBlock#Duration"}},
    {"audio-badge", {"AudioBadgeControl#AudioBadgeControl"}},
    {"live-badge", {"Grid#Live"}},
    {"autoplay-badge", {"Border#AutoPlayBadge"}},
    {"apple-logo", {"Image#Apple"}},
    {"lcd-more-button", {"MenuButton#MoreButton"}},  // kept for themes that wrote it: the same button as .more-button
    {"playbar-toggles", {"StackPanel#NowPlayingToggleButtons"}},
    {"lyrics-button", {"ToggleButton#LyricsToggleButton"}},
    {"queue-button", {"ToggleButton#PlayQueueToggleButton"}},
    {"airplay-button", {"Button#AirPlayButton"}},
    {"airplay-icon", {"FontIcon#AirPlayIcon"}},
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
    {"lyrics-panel-border", {"SplitView#ContentSplitView > Grid > Grid#PaneRoot > Rectangle#HCPaneBorder"}},
    {"lyrics-panel-top-spacer", {"SplitView#ContentSplitView > Grid > Grid#PaneRoot > Border > Grid > Grid:first-child"}},
    {"lyrics", {"LyricsHostView#LyricsView"}},
    {"lyrics-menu-button", {"Button#LyricsMenuButton"}},
    {"lyrics-synced", {"KaraokeLyricsView#RootControl"}},
    {"lyrics-static", {"ListView#StaticLyrics"}},
    {"lyrics-message", {"TextBlock#UnloadedTextBlock", "TextBlock#LoadingTextBlock", "TextBlock#NotAvailableTextBlock"}},
    {"lyrics-line", {"KaraokeLyricsLine"}},
    {"lyrics-line-button", {"ItemsControl#TimeBasedLyricsItemsControl > ItemsPresenter > StackPanel > ContentPresenter > Button"}},
    {"lyrics-line-content", {"KaraokeLyricsLine Grid#ContentGrid"}},
    {"lyrics-words", {"KaraokeLyricsLine TextBlock#LineContainer", "KaraokeLyricsLine Border#WordByWordHost"}},
    {"lyrics-background-words", {"KaraokeLyricsLine Border#BackgroundWordByWordHost"}},
    {"lyrics-instrumental", {"KaraokeLyricsLine StackPanel#InstrumentalContainer"}},
    {"lyrics-footer", {"StackPanel#LyricsFooter"}},
    {"lyrics-writers", {"TextBlock#WritersTextBlock"}},
    {"queue", {"PlayQueueView#PlayQueueView"}},
    {"queue-clear", {"StackPanel#PlayQueueViewContents > Border > Grid > Button"}},
    {"queue-autoplay", {"StackPanel#PlayQueueViewContents > Border > Grid > ContentControl > ContentPresenter > ToggleButton"}},

    // Apple's other views, by their type (knowledge/winmd): a name finds the view wherever Apple opens it.
    // Types read from the metadata and not yet seen in a saved tree: names.md lists them as unconfirmed.
    {"airplay-panel", {"AirPlayView"}},
    {"airplay-device", {"AirPlayDeviceItem"}},
    {"audio-quality-flyout", {"AudioQualityFlyout"}},
    {"reactions-panel", {"ReactionsView"}},
    {"equalizer", {"EqualizerView"}},
    {"eq-slider", {"EQSlider"}},
    {"sound-enhance-slider", {"SoundEnhanceSlider"}},
    {"info-panel", {"InfoPanelControl"}},
    {"activity-window", {"ActivityWindowControl"}},
    {"activity-status-button", {"ActivityWindowItemStatusButton"}},
    {"video-page", {"VideoPlaybackPage"}},
    {"column-browser", {"ColumnBrowserView"}},
    {"breadcrumb", {"Breadcrumb"}},
    {"rating", {"RatingControl"}},
};

struct BuiltinEntry {
    std::string_view name;
    std::vector<std::string_view> parts;
};

const std::array<BuiltinEntry, 4> kBuiltins{{
    {"floating-panels", {layouts::kFloatingPanels, layouts::kScrollbars}},
    {"bottom-bar", {layouts::kBottomBar}},
    {"centre-bar", {layouts::kCentreBar}},
    {"scrollbars", {layouts::kScrollbars}},
}};

constexpr size_t kMaxExpansions = 32;

bool IsClassChar(char c) {
    return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-';
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

namespace {

class Zones final : public StylesheetNames {
public:
    std::optional<std::vector<std::string>> Expand(std::string_view selector, std::string* problem) const override {
        return ExpandClasses(selector, problem);
    }
    std::optional<std::string> Layout(std::string_view name) const override { return BuiltinStylesheet(name); }
    std::vector<std::string> LayoutNames() const override { return BuiltinStylesheetNames(); }
};

}  // namespace

const StylesheetNames& ZoneNames() {
    static const Zones zones;
    return zones;
}

}  // namespace ampify::theme
