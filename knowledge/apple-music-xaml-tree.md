# The real XAML tree of Apple Music

Read with Ampify's inspector (`theme.inspect = 1`) on the CI bench, Apple Music `1.1792.39682.0`, with no account signed in
and the home page open: 482 elements. A second look on a signed-in PC added the duplicate-name notes below. The selectors
are those that `user.css` accepts.

## Main zones

```
MainPage (AMP.UI.Views.MainPage)                      content of the window "MainWindow"
└─ Grid#PageRoot
   ├─ Sidebar#MainSidebar (AMP.UI.Views.Sidebar)
   │  └─ NavigationView#NavView                        the sidebar and everything else in the window
   │     ├─ Grid#PaneRoot (width 290, collapsed to 48)  left pane
   │     │  ├─ Grid#PaneHeaderGrid                     logo, TextBlock#AppName, MenuButton#MoreButton
   │     │  ├─ Grid#AutoSuggestArea                    search (AutoSuggestBox, TextBox#TextBox)
   │     │  ├─ ItemsRepeater#MenuItemsHost             NavigationViewItem (AMP.UI.Views.NavigationViewItem) x 4
   │     │  └─ AccountControl (AMP.UI.Views.AccountControl)
   │     └─ content of the NavigationView
   │        ├─ BackDrop (AMP.UI.Controls.BackDrop)      background of the top bar (height 6x)
   │        ├─ Grid#TransportBar                       playback bar, at the top
   │        │  ├─ TransportControl#TransportControl    ToggleButton#ShuffleButton, previous / play / next
   │        │  │                                       (AppBarButton), Button#RepeatButton
   │        │  ├─ NowPlayingControl#LCD                the LCD display in the middle
   │        │  │  └─ Grid#LCD: HoverGrid#ThumbnailHoverGrid (ArtworkImage#ActualArtwork, Image#StaticArtwork),
   │        │  │     StackPanel#TrackTitleAndArtist (TickerTapeControl#TrackTitleTickerTape,
   │        │  │     TickerTapeControl#ArtistTitleTickerTape, TextBlock#CurrentTime, TextBlock#Duration),
   │        │  │     Image#Apple, ScrubberControl#LCDScrubber, AudioBadgeControl#AudioBadgeControl
   │        │  ├─ StackPanel#NowPlayingToggleButtons   VolumeControl#VolumeControl, AirPlayButton,
   │        │  │                                       ToggleButton#LyricsToggleButton, ToggleButton#PlayQueueToggleButton
   │        │  └─ Border#CaptionButtonsOcclusionBorder
   │        ├─ SplitView#ContentSplitView              page area (the pane serves the queue and lyrics)
   │        │  └─ Grid > Grid#ContentRoot > Border > Grid > Frame#ContentFrame > ContentPage (AMP.UI.Views.ContentPage)
   │        │     (safe selector: SplitView#ContentSplitView > Grid > Grid#ContentRoot; see below)
   │        │     ├─ Grid#ContentPageShadowGrid
   │        │     └─ Spinner#Spinner
   │        └─ InfoBar#topInfoBar, InfoBar#bottomInfoBar
   ├─ Canvas#DraggableTitlebarRegion (collapsed)
   ├─ Border#Titlebar
   └─ Rectangle#Frost (collapsed)
```

## What to know for themes and extensions

- **The playback bar is at the top**, in the content of the `NavigationView`, not at the bottom: `Grid#TransportBar`.
- **Colors:** text is `#D8000000` in light mode (TextFillColorPrimary). Backgrounds are theme brushes ("present" in the
  dump: a brush that is not a solid color, often acrylic or Mica).
- **Resources:** the application dictionary holds only one key of its own (`ContentPageShadow`). Colors are in its merged
  and theme dictionaries; the inspector lists them.
- **Pages** go through `Frame#ContentFrame`; a loaded page is a `ContentPage`. The home content was not yet loaded when
  the dump was taken.
- **Duplicate names:** `Grid#RootGrid`, `Grid#LayoutRoot`, `Border#Root` and `ContentPresenter#ContentPresenter` come from
  WinUI templates and exist dozens of times. Target the named parent instead (`Grid#TransportBar Button`).
- **`Grid#ContentRoot` exists at least three times** (read on a signed-in PC): the one in the `NavigationView` template
  (`SplitView#RootSplitView > Grid > Grid#ContentRoot`, which holds the playback bar and all pages), one in each playback
  `AppBarButton` (previous, play, next), and the one of the page area (`SplitView#ContentSplitView > Grid >
  Grid#ContentRoot`). A margin set on `Grid#ContentRoot` alone pushed the playback bar down and hid its three buttons.
- **The page area goes under the playback bar:** `Grid#TransportBar` is at the top of the same grid, with `BackDrop`
  (height 64) as a frosted background. A page therefore scrolls under the bar. Original draw order: `Grid#TransportBar`
  `Canvas.ZIndex` 10, `BackDrop` 9, `SplitView#ContentSplitView` 0. A `z-index` below 10 on the bar puts it under the pages
  (it disappears).
- **Icons:** the badges (explicit, stars) are `TextBlock` elements in private-use characters. Ampify never changes their
  font (an empty square would show), even if a `TextBlock { font-family: ... }` rule targets them.
- **MainPage is not a panel:** its content is `Grid#PageRoot`. Apple Music removes the children it does not know from it (a
  host added by Ampify disappeared within seconds). Ampify's panels go through a Popup of the window.
- **Settings:** `PlaybackSettingsPage` (the Playback page), recognized by its class name, as AMExclusive does.
