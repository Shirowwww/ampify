# Stable names

Apple Music names few of its elements, and the paths to them are long: the sidebar is
`SplitView#RootSplitView > Grid > Grid#PaneRoot`, and the play button has no name at all. A theme can write a stable
name instead, as a CSS class:

```css
.sidebar { corner-radius: 10px; }
.card:hover { transform: scale(1.04); }
.playbar Button { color: #FFFFFF; }
```

Each class stands for the path to one zone of Apple Music. Ampify keeps the table of paths (`core/theme/builtins.cpp`),
so when an update of Apple Music moves a zone, the table changes once and every theme keeps working, the way
Spicetify's `css-map.json` follows Spotify's class names.

A class works anywhere a part of a selector goes: alone (`.sidebar`), with a pseudo-class (`.card:hover`,
`.track-row:nth-child(2)`), or inside a longer selector (`.album-header TextBlock`, `.playbar > Grid`). It must start
its own part: `Grid.card` is refused, because Ampify cannot tell it from a full type name such as
`Microsoft.UI.Xaml.Controls.Grid`. An unknown class makes the whole rule ignored, with a line in the log, as any other
selector mistake does.

Some zones have several forms (the top spacer of each kind of page, the three search scopes). Their class stands for
all of them.

The table was built from inspector surveys of every page of Apple Music 1.1792. `ampify_theme_check --names` checks it
against trees saved by the inspector (see "Check the names after an update" below).

## Window

| Class | Zone |
|---|---|
| `.window` | The whole window, behind everything else |
| `.titlebar` | The title bar strip, on the pages that show it |
| `.titlebar-drag-area` | The area that drags the window |
| `.caption-buttons-cover` | The strip that keeps the minimize, maximize and close buttons readable over the playback bar |
| `.modal` | Where Apple Music shows its dialogs |
| `.info-bar` | The banners at the top and at the bottom of the window |

## Sidebar

| Class | Zone |
|---|---|
| `.sidebar` | The sidebar panel |
| `.navigation` | The navigation view that holds the sidebar and the pages |
| `.sidebar-header` | The top of the sidebar, with the app name |
| `.sidebar-toggle` | The button that folds the sidebar |
| `.back-button` | The back button |
| `.app-name` | The "Music" title |
| `.search-area` | The area around the search box |
| `.search-box` | The search box |
| `.sidebar-scroll` | The scrolling list of the sidebar |
| `.sidebar-items` | The items of that list |
| `.sidebar-item` | One item: Home, Radio, a playlist... |
| `.sidebar-item-artwork` | The artwork of a pinned item |
| `.sidebar-play-button` | The play button over that artwork |
| `.sidebar-footer` | The bottom of the sidebar, with the account and the Ampify entry |
| `.sidebar-footer-items` | The items of that bottom part |
| `.sidebar-separator` | The line above it |
| `.account-picture` | The account picture |
| `.account-name` | The account name |

## Content and pages

| Class | Zone |
|---|---|
| `.main-column` | The whole column right of the sidebar: the playback bar and the pages, on a light gray veil |
| `.content` | The panel that holds the pages |
| `.content-background` | Its background layer |
| `.content-frame` | The frame the pages are shown in |
| `.content-shadow` | The shadow along its edge |
| `.page` | A page |
| `.page-top-spacer` | The empty space at the top of a page, 64 pixels high, that keeps it below the playback bar |
| `.stacked-list` | The list of Home and Search, whose top padding plays the same part |
| `.page-header` | The title bar of a page, which stays at the top when the page scrolls |
| `.page-title` | The title of a page |
| `.frost` | The frosted layer behind the playback bar and the page headers |
| `.filter-box` | The filter box of the library pages |
| `.filter-button` | The button that opens it |
| `.sort-button` | The sort button of the Songs page |
| `.sticky-header-background` | The background of a header that stays at the top |

## Shelves and sections

| Class | Zone |
|---|---|
| `.section-header` | The header of a section |
| `.shelf` | A row of cards that scrolls sideways |
| `.shelf-title` | Its title |
| `.shelf-grid` | The cards of the row |
| `.shelf-arrows` | The arrows that scroll it |
| `.shelf-arrow-left`, `.shelf-arrow-right` | Each arrow |

## Cards

| Class | Zone |
|---|---|
| `.card` | A square card: an album, a playlist, a station |
| `.card-brick` | A wide card, such as the categories of Search |
| `.card-wide` | A card with its artwork on the left |
| `.card-track` | A song in a shelf |
| `.card-play-button` | The play button that shows over a card under the pointer |
| `.card-title`, `.card-subtitle` | The two lines of text under a card |
| `.artwork` | Every artwork, in cards, lists and the playback bar |
| `.artwork-shadow` | The shadow under an artwork |
| `.explicit-badge` | The "E" badge |
| `.favorite-button`, `.favorite-icon` | The star, and its icon |
| `.add-button` | The button that adds to the library |
| `.download-button`, `.downloaded-button`, `.download-progress` | The download button, its done state, and its progress ring |
| `.now-playing-indicator` | The bars that mark the song playing |

## Albums, playlists and songs

| Class | Zone |
|---|---|
| `.album-header` | The header of an album or a playlist: artwork, title, buttons |
| `.album-play-button`, `.album-shuffle-button` | Its Play and Shuffle buttons |
| `.album-description` | Its description |
| `.add-description-button` | The button that adds a description to a playlist |
| `.share-button` | The share button |
| `.lossless-badge`, `.digital-master-badge` | The Lossless and Apple Digital Master badges |
| `.motion-artwork` | The moving artwork of some albums |
| `.artist-picture` | The picture of an artist |
| `.radio-play-button` | The play button of a station |
| `.track-list` | The list of songs |
| `.track-row` | One song of that list |
| `.track-play-button` | The play button of a song |
| `.reactions-button` | The reactions button of a song |
| `.song-list` | The table of the Songs page |
| `.song-row` | One row of that table |

## Search

| Class | Zone |
|---|---|
| `.search-top-spacer` | The empty space above the search page's header, 64 pixels high, that keeps it below the playback bar |
| `.search-header` | The top of the search page |
| `.search-scopes` | The buttons that choose where to search |
| `.search-scope` | One of them: Apple Music, Library, Store |
| `.search-status` | The status line ("No results"...) |

## Playback bar

| Class | Zone |
|---|---|
| `.playbar` | The playback bar |
| `.playbar-controls` | Its first column: shuffle, previous, play, next, repeat |
| `.now-playing` | Its second column: the song playing |
| `.playbar-extras` | Its third column: lyrics, queue, AirPlay, volume |
| `.transport` | The buttons of the first column |
| `.shuffle-button`, `.shuffle-dot` | Shuffle, and the dot that shows it is on |
| `.previous-button`, `.play-button`, `.next-button` | Previous, play or pause, next |
| `.repeat-button`, `.repeat-dot` | Repeat, and its dot |
| `.lcd` | The display of the song playing |
| `.lcd-artwork` | Its artwork area |
| `.lcd-artwork-image`, `.lcd-placeholder` | The artwork, and what shows when there is none |
| `.track-text` | The title and the artist |
| `.track-lines`, `.track-line` | The block of the two lines, and each line |
| `.track-title`, `.track-artist` | Each of them |
| `.scrubber` | The progress bar |
| `.current-time`, `.duration` | The time played and the length |
| `.audio-badge` | The badge of the audio format (Lossless, Dolby Atmos) |
| `.live-badge`, `.autoplay-badge` | The Live and Autoplay badges |
| `.apple-logo` | The Apple logo shown when nothing plays |
| `.lcd-more-button` | The "..." button of the song playing |
| `.playbar-toggles` | The buttons of the third column |
| `.lyrics-button`, `.queue-button`, `.airplay-button` | Lyrics, Up Next, AirPlay |
| `.volume`, `.volume-button` | The volume control, and its speaker button |
| `.volume-slider` | The slider in the volume flyout: `color` fills the part left of the thumb, `background` the rest of the track |
| `.volume-mute-button` | The mute button on its left |

Ampify never moves the volume. A theme can change how the volume control looks, and nothing else about it. The
flyout that holds the slider is a `.flyout` (see "Flyouts, menus and tooltips"). The slider's thumb is
`.volume-slider Thumb`, and its track is `.volume-slider Rectangle#HorizontalTrackRect` (its `height` gives the track's
thickness). The track, the fill and the thumb keep the theme's colours under the pointer and while dragged.

```css
.volume-slider {
    color: #FFFFFF;
    background: #4D4D4D;
}

.volume-slider Thumb {
    background: #FFFFFF;
}
```

## Flyouts, menus and tooltips

Apple Music opens them in popups, outside the window's tree: each popup is a root of its own in the inspector,
`Popup`, and a selector reaches what is inside it (`.flyout Slider`), never what opened it (`.playbar .flyout`
matches nothing). A popup takes the theme the first time it opens, and keeps it.

| Class | Zone |
|---|---|
| `.flyout` | A flyout, such as the volume's: its `background`, `corner-radius` and `border-color` |
| `.menu` | A menu, such as the "..." menu of a song, and each of its submenus |
| `.menu-item` | An entry of a menu, including those that open a submenu or show a check mark |
| `.menu-separator` | The line between two groups of entries |
| `.tooltip` | The tip that shows under a button after a moment |

## Lyrics and queue

| Class | Zone |
|---|---|
| `.lyrics-panel` | The side panel of the lyrics and of Up Next |
| `.lyrics` | The lyrics view |
| `.lyrics-menu-button` | Its menu button |
| `.lyrics-synced` | The lyrics that follow the song |
| `.lyrics-static` | The lyrics without timing |
| `.queue` | The Up Next list |

## Everything else

The table names the zones a theme usually styles. Every other element can still be reached as `Type#Name`, or by its
place (`:nth-child`), and the inspector shows the selector of each element (see "Inspector" in [format.md](format.md)).
Apple Music 1.1792 also names these elements, which have no class yet:

`ArtworkImage#ArtworkHasImage`, `ArtworkImage#ArtworkIcon`, `ArtworkImage#ArtworkImage`,
`ArtworkImage#CoverArtworkImage`, `ArtworkImage#TopSearchArtwork`, `AspectRatioGrid#AspectRatioGrid`,
`Border#FavoriteButtonContainer`, `Border#ReactionsTapTarget`, `Border#TopNavContentOverlayAreaGrid`,
`Button#ActionButton`, `Button#AudioBadgeButton`, `Button#CirclePlayButton`, `Button#leftPlayButton`,
`Button#MoreButton`, `Button#NavigationViewCloseButton`, `Button#PaneAutoSuggestButton`, `Button#RightMoreButton`,
`Button#TopNavOverflowButton`, `Button#ViewAppearedAutomationButton`, `ContentControl#HeaderContent`,
`ContentControl#PageHeaderBar`, `ContentControl#PaneAutoSuggestBoxPresenter`,
`ContentControl#PaneCustomContentBorder`, `ContentControl#PaneCustomContentOnTopPane`,
`ContentControl#PaneFooterOnTopPane`, `ContentControl#PaneHeaderContentBorder`, `ContentControl#PaneHeaderOnTopPane`,
`ContentControl#PaneTitleOnTopPane`, `ContentControl#PaneTitlePresenter`, `ContentControl#StackedPageHeader`,
`ContentControl#TopPaneAutoSuggestBoxPresenter`, `ContentPresenter#ColumnBrowserPresenter`,
`ContentPresenter#SocialBadgingContent`, `ContentPresenter#TransportControlsPresenter`,
`FirstColumnAdjustableGrid#FirstColumnAdjustableGrid`, `FirstColumnAdjustableGrid#TitleContainerGrid`,
`FontIcon#AccentThemedGlyph`, `FontIcon#AirPlayIcon`, `FontIcon#DescendingIcon`, `FontIcon#DownloadedFontIcon`,
`FontIcon#DownloadedGlyph`, `FontIcon#FavoriteButtonIcon`, `FontIcon#FontIcon`, `FontIcon#NormalThemedGlyph`,
`FontIcon#PersonPlusFont`, `FontIcon#RepeatButtonIcon`, `FontIcon#ShuffleButtonIcon`, `Frame#SearchContentFrame`,
`Grid#ArtworkHoverContent`, `Grid#BottomContent`, `Grid#ButtonHolderGrid`, `Grid#ContentGridViewWrapper`,
`Grid#ContentLeftPadding`, `Grid#ContentPaneTopPadding`, `Grid#ContentTopPadding`, `Grid#DescriptionGrid`,
`Grid#DisplayName`, `Grid#DragIndicator`, `Grid#DummyPlayWithoutCoverArtButton`, `Grid#FixedDetailListSectionContent`,
`Grid#FixedDetailListSectionHeaderContent`, `Grid#FixedGroupedGridSectionContent`, `Grid#GroupedItemsHeaderContent`,
`Grid#HeaderStack`, `Grid#HostRoot`, `Grid#InnerGrid`, `Grid#ItemsContainerGrid`, `Grid#MainContent`,
`Grid#PageFlatHeaderContent`, `Grid#PaneContentGrid`, `Grid#PaneTitleHolder`, `Grid#PaneToggleButtonGrid`,
`Grid#RootGrid`, `Grid#ShadowCaster`, `Grid#TickerTapeGrid`, `Grid#TimedTextSourcePresenter`,
`Grid#TogglePaneTopPadding`, `Grid#TopNavGrid`, `Grid#TopNavLeftPadding`, `Grid#TopNavTopPadding`,
`Grid#TopPaneAutoSuggestArea`, `HoverGrid#RootGrid`, `HoverGrid#RowGrid`, `Hyperlink#Title`,
`Image#DragIndicatorImage`, `Image#PosterImage`, `Image#StaticArtwork`, `ItemsRepeater#HeaderRepeater`,
`ItemsRepeater#SubtitlesRepeater`, `ItemsRepeater#TopFooterMenuItemsHost`, `ListView#FixedListSectionContent`,
`ListView#List`, `MediaPlayerPresenter#MediaPlayerPresenter`, `Rectangle#DropLocationIndicator`, `Rectangle#Frost`,
`Rectangle#HCPaneBorder`, `Rectangle#Indeterminate`, `Rectangle#LightDismissLayer`,
`ScrollViewer#FooterItemsScrollViewer`, `ScrollViewer#TimeBasedLyrics`, `Sidebar#MainSidebar`,
`StackPanel#ColumnSectionHeaderContent`, `StackPanel#ContentRoot`, `StackPanel#ExplicitFavoriteContainer`,
`StackPanel#PlayQueueViewContents`, `StackPanel#ShelfSectionContent`, `StackPanel#SingleItemSectionContent`,
`StackPanel#StackedGridSectionContent`, `StackPanel#TopNavArea`, `TextBlock#AppleDigitalMasterText`,
`TextBlock#AudioBadgeDot`, `TextBlock#DescriptionText`, `TextBlock#DescriptionTextBlock`,
`TextBlock#FlowCaseSubtitle`, `TextBlock#FlowCaseTitle`, `TextBlock#HeadlineTextBlock`,
`TextBlock#HorizontalLockupDefaultHeadline`, `TextBlock#HyperlinkTextBlock`, `TextBlock#JustTextBlock`,
`TextBlock#LoadingTextBlock`, `TextBlock#LosslessImageDot`, `TextBlock#LosslessText`,
`TextBlock#NotAvailableTextBlock`, `TextBlock#UnloadedTextBlock`

These are template parts and inner pieces: they may change more often than the zones above.

## Check the names after an update

When Apple Music updates, save a few trees with the inspector (a page of each kind: Home, an album, Search, the
lyrics), then run:

```
ampify_theme_check.exe --names C:\trees\home.json C:\trees\album.json ...
```

The tool prints, for each class, how many trees and elements it found, and lists the classes and the forms it found
nowhere. It exits with code 1 when a class matches nothing in any tree: that class needs a new path in
`core/theme/builtins.cpp`.
