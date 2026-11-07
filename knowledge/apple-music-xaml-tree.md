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

## A dialog

Read on the welcome dialog (`GDPRWelcomeDialog`) of the CI bench, 1.1792. A dialog is a root of its own: a `Popup` that
holds the dialog, beside a second `Popup` whose only child is the veil (`Rectangle#SmokeLayerBackground`, the size of the
window). The dialog's type changes with each dialog; the template under it does not, which is what `.dialog` and the
names after it (docs/themes/names.md) stand on.

```
Popup
└─ GDPRWelcomeDialog            (Background and CornerRadius of the box are set here too)
   └─ Border#Container
      └─ Grid#LayoutRoot
         └─ Border#BackgroundElement     the painted box
            └─ Grid#DialogSpace
               ├─ ScrollViewer#ContentScrollViewer
               │  └─ ... ContentControl#Title (collapsed when the dialog has none), ContentPresenter#Content
               └─ Grid#CommandSpace (collapsed here: the dialog draws its own button)
                  └─ Button#PrimaryButton, Button#SecondaryButton, Button#CloseButton
Popup
└─ Rectangle#SmokeLayerBackground
```

Not read yet, to take from a dialog that is open: the title of a dialog that has one, a check box (`AskUserCheckbox`), and
the name of every other dialog's own content.

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

## Where Apple Music cuts what a theme adds

Apple 1.1792. "Seen" comes from a file or a dump, "deduced" from two facts, "to check" was never looked at on screen.

| Element | What limits it | How Ampify or a theme gets around it |
|---|---|---|
| Progress line, `ScrubberControl#LCDScrubber` (`.scrubber`) | `Height` 6, bottom-aligned (seen in the dump); its template asks for about 18, so WinUI clips the template and all it holds to 6 (seen on screen). The display around it, `Grid#LCD`, has corners of 8 and cuts what passes its edge at the start and end of a song (deduced) | An image put inside makes the line grow to fit it and move down by what it gained (`RoomForDecors`, `src/core/theme/theme.cpp`; the table of such slots is there). `track-height`, `track-radius` and `thumb-visibility` write the line's own `TrackHeight`, `TrackRectRadius` and `ThumbVisibility` (`IScrubberControl`, `src/win/apple_properties.cpp`); whether the track grows past 6 is to check |
| Display artwork, `ArtworkImage#ActualArtwork`, `Border#PlaceHolderArtwork`, `AspectRatioGrid` | Width 54 each; `NowPlayingControl.xbf` defines `ArtworkDimension`, `ActionButtonHeight`, `ActionButtonWidth`, `TickerTapeHeight` in its own resources (seen) | Size the three together (the spinning-artwork snippet). Writing those keys in the control's resources is to check: a `StaticResource` would freeze them at load |
| `ArtworkImage` elsewhere (album header, cards, queue) | `ArtworkMaxSize` and Apple's own `MaxHeight` 270 (seen) | `FollowArtworkCap` lifts both with a theme's size |
| Karaoke lyrics (`KaraokeLyricsLine`, `LyricsListViewItem`) | `SungColor`, `UnsungColor`, `LyricsOpacity_*`, `LineFontSize` are keys local to those views, not in the app's dictionaries; margins come from `TSLMargin` (seen) | None yet |
| Sidebar (`Sidebar#MainSidebar`) | Narrow mode at 1324 px and below; the folded `SplitView` clips `Grid#PaneRoot` to `CompactPaneLength`. `Sidebar` has a public `ExpandedModeThresholdWidth` (seen) | `compact-pane-length` and `PinPushesPages`; the threshold is to check |
| Play bar buttons (`RepeatButton`, `ShuffleButton` 40 by 40, `AppBarButton`) | Fixed sizes; a button clips its content (seen) | Move the button, not its glyph; no `padding` on every `Button` |
| `Grid#TransportBar` | 64 high, and the first 64 pixels of each page are kept for it (`BackDrop` 64) (seen on the CI bench; a 76 in a home dump is the Mirror theme's own `--bar-height`) | Take those 64 back where they are when panels move |
| Cards and artworks Apple animates (`Translation`, `Scale`, `Rotation`) | Their composition visual cannot be taken (seen) | `overflow` uses `UIElement.Clip`, rectangles only |

The names above come from the string tables of Apple's compiled XAML: `makepri dump /if AMP.UI.Controls.pri /of
out.xml /dt detailed` (Windows SDK) gives each `.xbf` in Base64, and a `.xbf`'s string table starts 8 bytes before its
first `http://` in UTF-16 (see `knowledge/apple-music-native-views.md`). Its node stream, where the numbers are, is not
decoded. The properties of Apple's classes come from their winmd (`knowledge/winmd/`).
