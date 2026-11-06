// The stable class names (src/core/theme/builtins.cpp) against paths read from saved trees of Apple Music 1.1792: the
// home page and the welcome dialog of the CI bench. A path is written root first, one element per word
// (Type#Name, or Type when it has no name); the last element is the one a class must find or leave alone.

#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../src/core/theme/builtins.hpp"
#include "../src/core/theme/selector.hpp"
#include "test.hpp"

namespace th = ampify::theme;

namespace {

std::vector<th::NodeInfo> PathNodes(const std::string& path) {
    std::vector<th::NodeInfo> nodes;
    std::istringstream words(path);
    std::string word;
    while (words >> word) {
        th::NodeInfo node;
        size_t hash = word.find('#');
        node.type = word.substr(0, hash);
        if (hash != std::string::npos) node.name = word.substr(hash + 1);
        node.id = nodes.size() + 1;
        node.parent = static_cast<int>(nodes.size()) - 1;
        node.depth = static_cast<int>(nodes.size());
        node.position = 0;
        node.siblings = 1;
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool ClassFindsLast(const std::string& name, const std::string& path) {
    std::optional<std::vector<std::string>> forms = th::ClassSelectors(name);
    CHECK(forms.has_value());
    if (!forms) return false;
    std::vector<th::NodeInfo> nodes = PathNodes(path);
    for (const std::string& form : *forms) {
        std::optional<th::Selector> selector = th::ParseSelector(form);
        if (selector && th::Matches(*selector, nodes, static_cast<int>(nodes.size()) - 1)) return true;
    }
    return false;
}

const std::string kDialog = "Popup GDPRWelcomeDialog Border#Container Grid#LayoutRoot Border#BackgroundElement";
const std::string kDialogSpace = kDialog + " Grid#DialogSpace";
const std::string kMainWindow = "MainPage Grid#PageRoot Sidebar#MainSidebar Grid NavigationView#NavView Grid#RootGrid";
const std::string kSidebar = kMainWindow + " SplitView#RootSplitView Grid Grid#PaneRoot";
const std::string kContent = kMainWindow + " SplitView#RootSplitView Grid Grid#ContentRoot Border Grid#ContentGrid";
const std::string kPlaybar = kContent + " Grid#TransportBar";
const std::string kInfoBar = kContent + " InfoBar#topInfoBar Border#ContentRoot Grid";
const std::string kStackedPage = kContent + " Frame#ContentFrame ContentPage Grid#MainContent Grid#StackedPageContent";
const std::string kPageList = kStackedPage + " ListView Border ScrollViewer#ScrollViewer";
const std::string kShelfTitle = kPageList + " ScrollContentPresenter ItemsStackPanel ListViewItemPresenter#Root " +
                                "StackPanel#StackedGridSectionContent Grid#DefaultSectionHeaderContent";

struct Found {
    const char* name;
    std::string path;
};

}  // namespace

TEST_CASE(Names_EveryClassIsLowercaseKebabAndOnlyOnce) {
    std::set<std::string> seen;
    for (const std::string& name : th::ClassNames()) {
        CHECK(seen.insert(name).second);
        CHECK(!name.empty() && name.find_first_not_of("abcdefghijklmnopqrstuvwxyz-") == std::string::npos);
    }
}

TEST_CASE(Names_DialogPartsAreFoundWhateverTheDialogsType) {
    const std::vector<Found> found{
        {"dialog", kDialog},
        {"dialog-scrim", "Popup Rectangle#SmokeLayerBackground"},
        {"dialog-title", kDialogSpace + " ScrollViewer#ContentScrollViewer Border#Root Grid ContentControl#Title"},
        {"dialog-content", kDialogSpace + " ScrollViewer#ContentScrollViewer Border#Root Grid ContentPresenter#Content"},
        {"dialog-buttons", kDialogSpace + " Grid#CommandSpace"},
        {"dialog-primary", kDialogSpace + " Grid#CommandSpace Button#PrimaryButton"},
        {"dialog-secondary", kDialogSpace + " Grid#CommandSpace Button#SecondaryButton"},
        {"dialog-close", kDialogSpace + " Grid#CommandSpace Button#CloseButton"},
        {"dialog-checkbox", kDialogSpace + " ScrollViewer#ContentScrollViewer CheckBox#AskUserCheckbox"},
    };
    for (const Found& item : found) CHECK(ClassFindsLast(item.name, item.path));
    CHECK(ClassFindsLast("dialog", "Popup AMP.UI.Views.EditPlaylistDialog Border#Container Grid#LayoutRoot "
                                   "Border#BackgroundElement"));
}

TEST_CASE(Names_DialogNamesLeaveTheWindowAlone) {
    // A banner has a close button of its own name, and other templates have a Grid#LayoutRoot and a ContentPresenter.
    CHECK(!ClassFindsLast("dialog-close", kInfoBar + " Button#CloseButton"));
    CHECK(ClassFindsLast("info-bar-close", kInfoBar + " Button#CloseButton"));
    CHECK(!ClassFindsLast("dialog", kSidebar + " Border Grid#PaneContentGrid Grid#LayoutRoot Border#BackgroundElement"));
    CHECK(!ClassFindsLast("dialog-content", kPlaybar + " ContentPresenter#Content"));
    CHECK(!ClassFindsLast("dialog-checkbox", kSidebar + " CheckBox"));
}

TEST_CASE(Names_BannerSidebarAndSearchPartsAreFound) {
    const std::vector<Found> found{
        {"info-bar-icon", kInfoBar + " Grid#StandardIconArea"},
        {"info-bar-icon", kInfoBar + " Viewbox#UserIconBox"},
        {"info-bar-title", kInfoBar + " InfoBarPanel TextBlock#Title"},
        {"info-bar-message", kInfoBar + " InfoBarPanel TextBlock#Message"},
        {"info-bar-content", kInfoBar + " ContentPresenter#ContentArea"},
        {"more-button", kSidebar + " Border Grid#PaneContentGrid Grid#PaneHeaderGrid ContentControl#PaneHeaderContentBorder "
                                   "ContentPresenter Grid MenuButton#MoreButton"},
        {"more-icon", kSidebar + " Border Grid#PaneContentGrid Grid#PaneHeaderGrid ContentControl#PaneHeaderContentBorder "
                                 "ContentPresenter Grid MenuButton#MoreButton Grid#LayoutRoot Border#IconColumn "
                                 "Viewbox#IconBox Border ContentPresenter#Icon"},
        {"topnav", kMainWindow + " Grid StackPanel#TopNavArea"},
        {"search-input", kSidebar + " Border Grid#PaneContentGrid Grid#AutoSuggestArea ContentControl#PaneAutoSuggestBoxPresenter "
                                    "ContentPresenter AutoSuggestBox Grid#LayoutRoot TextBox#TextBox"},
        {"search-button", kSidebar + " Border Grid#PaneContentGrid Grid#AutoSuggestArea AutoSuggestBox Grid#LayoutRoot "
                                     "TextBox#TextBox Grid Button#QueryButton"},
        {"sidebar-border", kSidebar + " Rectangle#HCPaneBorder"},
        {"sidebar-chevron", kSidebar + " Border Grid#PaneContentGrid ScrollViewer#MenuItemsScrollViewer "
                                       "ItemsRepeater#MenuItemsHost NavigationViewItem Grid#NVIRootGrid "
                                       "NavigationViewItemPresenter#NavigationViewItemPresenter Grid#LayoutRoot "
                                       "Grid#ExpandCollapseChevron AnimatedIcon#ExpandCollapseChevronIcon"},
        {"sidebar-footer-scroll", kSidebar + " Border Grid#PaneContentGrid Grid#ItemsContainerGrid "
                                             "ItemsRepeaterScrollHost ScrollViewer#FooterItemsScrollViewer"},
        {"account-badge", kSidebar + " PersonPicture#smallPersonPicture Grid#RootGrid Grid#BadgeGrid"},
    };
    for (const Found& item : found) CHECK(ClassFindsLast(item.name, item.path));
}

TEST_CASE(Names_TheSidebarEdgeAndThePanelEdgeAreTold) {
    const std::string lyricsPane = kContent + " SplitView#ContentSplitView Grid Grid#PaneRoot Rectangle#HCPaneBorder";
    CHECK(ClassFindsLast("lyrics-panel-border", lyricsPane));
    CHECK(!ClassFindsLast("sidebar-border", lyricsPane));
    CHECK(!ClassFindsLast("lyrics-panel-border", kSidebar + " Rectangle#HCPaneBorder"));
}

TEST_CASE(Names_MoreButtonIsTheSidebarsAndKeepsItsOldName) {
    const std::string more = kSidebar + " Border Grid#PaneContentGrid Grid#PaneHeaderGrid " +
                             "ContentControl#PaneHeaderContentBorder ContentPresenter Grid MenuButton#MoreButton";
    CHECK(ClassFindsLast("more-button", more));
    CHECK(ClassFindsLast("lcd-more-button", more));  // the name written before it, for the themes that use it
}

TEST_CASE(Names_PlaybarGlyphsAndDisplayPartsAreFound) {
    const std::string transport = kPlaybar + " Grid TransportControl#TransportControl";
    const std::string lcd = kPlaybar + " Grid NowPlayingControl#LCD";
    const std::vector<Found> found{
        {"shuffle-icon", transport + " StackPanel ToggleButton#ShuffleButton ContentPresenter#ContentPresenter Grid "
                                     "FontIcon#ShuffleButtonIcon"},
        {"repeat-icon", transport + " StackPanel Button#RepeatButton ContentPresenter#ContentPresenter Grid "
                                    "FontIcon#RepeatButtonIcon"},
        {"transport-icon", transport + " StackPanel AppBarButton Grid#Root Grid#ContentRoot Viewbox#ContentViewbox"},
        {"airplay-icon", kPlaybar + " Grid StackPanel#NowPlayingToggleButtons AirPlayButton Grid Button#AirPlayButton "
                                    "ContentPresenter#ContentPresenter Border FontIcon#AirPlayIcon"},
        {"lcd-loading", lcd + " Grid#LCD Rectangle#Indeterminate"},
        {"lcd-artwork-hover", lcd + " Grid#LCD HoverGrid#ThumbnailHoverGrid Grid#ArtworkHoverContent"},
    };
    for (const Found& item : found) CHECK(ClassFindsLast(item.name, item.path));
    // The shuffle button is a toggle, not one of the three round transport buttons.
    CHECK(!ClassFindsLast("transport-icon", transport + " StackPanel ToggleButton#ShuffleButton Viewbox#ContentViewbox"));
}

TEST_CASE(Names_HomeShelvesCardsAndPagesAreFound) {
    const std::vector<Found> found{
        {"shelf-title-link", kShelfTitle + " StackPanel Hyperlink#Title"},
        {"shelf-title-text", kShelfTitle + " StackPanel Hyperlink#Title Grid TextBlock#JustTextBlock"},
        {"shelf-title-text", kShelfTitle + " StackPanel Hyperlink#Title Grid TextBlock#HyperlinkTextBlock"},
        {"shelf-subtitles", kShelfTitle + " StackPanel ItemsRepeater#SubtitlesRepeater"},
        {"stacked-page-header", kPageList + " ScrollContentPresenter Grid ContentControl#StackedPageHeader"},
        {"page-scroll", kPageList},
        {"card-text-grid", "HoverGrid#BrickLockupSmallContent TitleSubtitleControl Grid "
                           "FirstColumnAdjustableGrid#FirstColumnAdjustableGrid"},
    };
    for (const Found& item : found) CHECK(ClassFindsLast(item.name, item.path));
}

TEST_CASE(Names_PageScrollIsThePagesNotAShelfs) {
    const std::string shelf = kPageList + " ScrollContentPresenter ItemsStackPanel ListViewItemPresenter#Root " +
                              "StackPanel#StackedGridSectionContent GridView#ContentGridView Border ScrollViewer#ScrollViewer";
    CHECK(!ClassFindsLast("page-scroll", shelf));
    CHECK(!ClassFindsLast("shelf-title-text", kPageList + " Hyperlink Grid TextBlock#JustTextBlock"));  // in a dialog
}

TEST_CASE(Names_ScrollBarPartsAreFoundInBothDirections) {
    const std::string vertical = kContent + " ScrollBar#VerticalScrollBar Grid#VerticalRoot";
    const std::string horizontal = kContent + " ScrollBar#HorizontalScrollBar Grid#HorizontalRoot";
    CHECK(ClassFindsLast("scrollbar-thumb", vertical + " Thumb#VerticalThumb"));
    CHECK(ClassFindsLast("scrollbar-thumb", horizontal + " Thumb#HorizontalThumb"));
    CHECK(ClassFindsLast("scrollbar-track", vertical + " Rectangle#VerticalTrackRect"));
    CHECK(ClassFindsLast("scrollbar-track", horizontal + " Rectangle#HorizontalTrackRect"));
    CHECK(ClassFindsLast("scrollbar-arrow", vertical + " RepeatButton#VerticalSmallDecrease"));
    CHECK(ClassFindsLast("scrollbar-arrow", horizontal + " RepeatButton#HorizontalSmallIncrease"));
    // The large steps (the track either side of the thumb) are not arrows.
    CHECK(!ClassFindsLast("scrollbar-arrow", vertical + " RepeatButton#VerticalLargeIncrease"));
}

TEST_CASE(Names_PlayerWindowsAndViewsAreNamedByTheirTypes) {
    CHECK(ClassFindsLast("fullscreen-volume", "FullScreenPlayerPage FullScreenPlayerView Grid VolumeControl#VolumeControl"));
    CHECK(ClassFindsLast("fullscreen-airplay", "FullScreenPlayerPage FullScreenPlayerView Grid AirPlayButton"));
    CHECK(ClassFindsLast("mini-player", "MiniPlayerPage"));
    CHECK(ClassFindsLast("mini-lcd", "MiniPlayerPage MiniPlayerView Grid MiniNowPlayingControl#LCD"));
    CHECK(ClassFindsLast("mini-player-background", "MiniPlayerPage Grid ImmersionLockup#immersionLockup"));
    CHECK(ClassFindsLast("airplay-panel", "Popup FlyoutPresenter AirPlayView"));
    CHECK(ClassFindsLast("equalizer", "Grid EqualizerView"));
    CHECK(ClassFindsLast("info-panel", "Grid InfoPanelControl"));
    CHECK(!ClassFindsLast("fullscreen-volume", kPlaybar + " Grid VolumeControl#VolumeControl"));
}
