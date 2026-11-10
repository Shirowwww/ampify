# XAML lessons

Problems met while drawing inside Apple Music, and what fixed them. Read this before changing `src/win/`.

1. Hold everything you create with a strong reference, and release it on the UI thread. `src/win/xaml_chrome.hpp` has
   the shared helpers: `AppWindowOf` for Apple's windows, `HeldElement` and `ElementMap` for elements,
   `PropertyLedger` to give Apple's local values back, `MotionAllowed`, `CursorOver`.
2. Check a screenshot. The log has said "mounted" or "applied" while nothing was visible.
3. Compare before and after side by side. Colours close to Apple Music's own show no difference.

## Objects, popups and lifetime

| Problem | Cause and fix |
|---|---|
| A React panel never visible, "mounted" every 2 to 5 seconds | A weak reference to a XAML object that Ampify created never resolves inside Apple Music, even while it is displayed. Weak references to Apple's own objects work. Hold every object Ampify creates strongly: popups (`WindowContext::panelPopup`), frames, slots, renders, dialogs, buttons, page hosts |
| Host placed in `Grid#PageRoot` lost | Apple Music empties that grid. Panels live in a Popup of the window; app pages in the grid that holds `Frame#ContentFrame` (next rows) |
| With the sidebar pane open over the pages, the Marketplace and Settings > Ampify drew over the pane | Every Popup draws above the window's whole tree. The pages' host is a child of the grid that holds `Frame#ContentFrame`, in its cell and z-index, attached while a page shows (`AttachPageHost`). The theme walk skips it |
| Apple's Home or Library stayed lit beside Ampify's entry | Apple lights its own page's item, which stays under the app page. Clear the sidebar's `SelectedItem` while an app page covers Apple's, and give it back when Ampify's arrow closes it |
| From Settings > Ampify, the top bar's back arrow went to the page before Settings | Apple's frame, hidden under the app page, still sat on Settings. While an app page covers Apple's, `Frame.Navigating` cancels a `NavigationMode::Back` and does the app page's own back (`GoBackInPage`), so every back (arrow, top bar, mouse button) agrees |
| Dark panel under a light theme | A Popup is outside the window tree and does not follow its mode. Copy `RequestedTheme` at each refresh |
| Crash in `Microsoft.UI.Xaml.dll` at close | The theme service walked the tree during close. `Window.Closed` sets a flag and nothing touches XAML after it |
| Crashes from XAML objects released on another thread | Release everything on the UI thread (`ReleaseXaml`, `DropContext`). Static services are never destroyed |
| Navigating twice to the same page was not seen | Only the route was compared. `RouteSerial` counts navigations |
| A large cover never replaced the blurry one | A `BitmapImage` that nothing in the window shows is never decoded, so `ImageOpened` and `ImageFailed` never fire. Load it through an element in the tree (the image node's invisible sizer), then give it to the visible brush |
| Old handlers on a bitmap or frame kept running | `auto_revoke` goes through a weak reference to the source, which never resolves for Ampify's objects. Remove handlers by token and check the sender is still the latest one |
| The full-screen player kept the first song's cover | Writing `Image.Source` on Apple's cover replaced the binding that brings each song's image. Never write a bound property of Apple's element: lay Ampify's own element over it in the same cell and follow Apple's with `RegisterPropertyChangedCallback` (`src/win/sharp_artwork.cpp`) |
| A settings card's Reset, Import and status line did nothing on every card but the last | Each new card dropped the cards whose panel had no `Parent()`, and while the page is being built nothing is connected to the window yet. Never ask `Parent()` whether something Ampify built is still shown. The page owns its cards |
| Pages made room for the panels, then ran under them again | Themes write `Grid#ContentRoot`'s margin repeatedly. The room is the padding of its parent grid, which themes do not write (`PushPages`) |

## Resources, colours and themes

| Problem | Cause and fix |
|---|---|
| Replaced colours had no effect | `{ThemeResource}` is only re-read at a mode change. Switch light, dark, and back after each resource change |
| White text on white under a dark scheme | The root `RequestedTheme` must follow the scheme |
| A brush written in place of a colour (or the reverse) | Fluent names colours with no `Brush` suffix. The write keeps the type already in place |
| A resource stayed fixed after removing the theme | The key added above the theme dictionaries hid the light and dark values. Remove the key on removal |
| LCD white after removing a theme | The brush colour was kept without its opacity (white at 8%). Keep the original brush as it is |
| Apple's backgrounds ignore Fluent brushes (`SolidBackgroundFillColorBaseBrush`...) | They use their own brushes. Target the elements by name in `user.css` |
| Apple Music crashed (0xc000027b) at the theme change after an inspector pass | Enumerating the application's dictionaries realizes deferred resources, and the next refresh fails on them. Never enumerate. The inspector asks `HasKey` for a fixed list of colour keys (`src/core/theme/colour_keys.cpp`) |
| Resource inspector failing in a loop (0x80004005) | Walking dictionaries throws E_FAIL. try/catch per dictionary and per key |
| Inspector: empty tree | The snapshot came before the window had content. Skip an empty tree and retry at the next tick |
| A button lost the theme's colours under the pointer and while pressed | The button's visual states set Fluent resources, which win over local values. Also write the colours into the control's own resources under `{Type}{Property}PointerOver` and `Pressed` keys |
| The window froze for about half a second every 3 seconds (Greenroom) | Each write of the play button's background put new state keys, which asked for a refresh of the whole window's resources (400 to 800 ms). Write state keys only when their colour changes. The log gives the duration and cause of every refresh |
| A 670 ms freeze the first time the volume flyout opened | Same refresh. The control that holds the keys switches to the other mode and back alone (`ReadThemeAgain`) |
| A theme reached nothing in the volume flyout, menus or tooltips | They open in popups outside the window's content. The snapshot walks each popup seen open (`GetOpenPopupsForXamlRoot`), a click or key looks for one that just opened, and a quarter-second timer looks too |
| Entries of a closed menu kept the theme's colours after removal | WinUI takes a menu's entries out of its tree on close and puts the same objects back on opening, so the engine took them for gone. It keeps what it owes an element out of the tree while it exists (`TreeBackend::Alive`) and writes it back on removal. A popup that opens again gets a pass |
| Apple Music stopped (0xc000027b) when Rounded buttons replaced Midnight | The button template binds `Padding` and `BorderThickness` into a `Border`. The search box's buttons have padding `0,0,-2,0`, and a `Border` refuses a negative side at layout (`ValidateThicknessValue`, E_INVALIDARG), where WinUI fails fast. `user.css` refuses a negative padding, border or corner, no template goes on a control with one, and `Restore` and `GiveBack` write the control's template before its other values |
| A 360 album cover stayed cut at 270 high | Apple also sets the `ArtworkImage`'s own `MaxHeight` to 270. `FollowArtworkCap` lifts `MaxWidth` and `MaxHeight` with the cap and gives Apple's back afterwards |
| Centred lyrics: wrapped lines fell back left for up to 3 seconds | Apple rewrites a lyrics line's `TextAlignment` in code. A text alignment the engine wrote is followed (`FollowAppleRewrites`), and Apple's write asks for a pass at once |
| Large text rewrote every text size every 3 seconds | WinUI keeps sizes in single precision (16.8 reads back as 16.799999237060547). Compare numbers as WinUI keeps them (`SameValue`) |
| The Settings card's button showed symbols instead of text | Apple's `AccentButtonStyle` sets `FontFamily` to WebDings. Use the page's plain button style with Ampify's accent template (`PrimaryButtonTemplate`) |
| A refused `Foreground` on `FontIcon#SubItemChevron` | The hidden chevron of an `AppBarButton` template is listed by the walk but cannot be reached (never realized). A value for an element gone since the snapshot is skipped, not refused |
| After a live theme switch, the Marketplace kept the old accent | Popups without a parent do not follow the window's mode switch. After the window, each open popup reads its theme alone (`ReadPopupsAgain`) |
| No way into Apple's menus | Apple builds them from its own lists (`ExtensibleMenuFlyout`) and nothing tells when one opens. After a click or key, look for the open `MenuFlyoutPresenter`: its `ItemsSource` is the menu's own items, where Ampify's entries go while it is open. A menu is an item's when one entry quotes a name ("Play «Name»"); the main menu is the one opened from `Grid#PaneHeaderGrid` |
| The album artist stayed Apple blue under every theme | The name is a `Hyperlink` inside the `TextBlock`, its runs keep their own colour, and Apple fills the link after the theme's first pass. A text colour also goes to the inlines of a text that holds a link, and is read back from its last run |
| A theme without colours (Hide chrome) turned a dark Apple Music light | The app mode follows the theme's `dark` flag, then its scheme name, and is left alone when neither says anything (`Theme::dark`, set by the loader) |
| Glass: a button under the pointer showed Apple's red under black text | A theme gave `AccentFillColorDefaultBrush` only. The secondary and tertiary brushes are worked out from the accent at 90% and 80% unless the theme names them |
| `--art-*` colours followed a new song 3 to 6 seconds late | The play bar image was found only by the tick's walk. It is now followed through `ImageOpened` and its `Source`, and new colours wake the service (condition variable) |
| Settings pages stayed grey (#1C1C1C), toggles and sliders Apple blue | Each settings page is a class of its own whose background Apple sets in code, and WinUI's accent control keys are set once when its dictionaries load. The settings pages are `.page` too, and control keys are derived from the theme's accent (`DefineAccentControls`) |
| Playing-row bars kept the previous song's colour | The bars are composition sprites whose brush copies the Foreground once, when the row is built. After writing a Foreground, give the new colour to the colour brushes of the child visuals when they are all one colour (`FollowForegroundInSprites`) |
| Extension panels stayed Fluent grey under every theme | Their frame reads `AcrylicInAppFillColorDefaultBrush`, which no theme names. The engine derives the acrylic fills from the theme's card colour when it is opaque |

The application dictionary has one key of its own (`ContentPageShadow`). Colours are in merged and theme dictionaries.
Do not walk them while Apple Music runs.

Visual states cannot be read before a control is first hovered, since WinUI builds an `AppBarButton`'s state groups
lazily. Writing the state keys into the control's resources works before and after.

A setter read from a template's visual state rarely gives its element: `TargetPropertyPath.Target` throws E_FAIL
(0x80004005). Read `Path` and `Value` instead, each in its own try. A `Visibility` read that way is boxed as
`IReference<Int32>` (Collapsed is 1), so a cast to the enum fails.

## Selectors and layout

| Problem | Cause and fix |
|---|---|
| Glass: playback bar and buttons gone; a margin on `Grid#ContentRoot` pushed the bar down | That name exists in the NavigationView, in every playback button and in the page area. Select it under `SplitView#ContentSplitView`, without z-index |
| Top bar button not found | Apple Music has no separate top bar. Ampify's buttons go in `StackPanel#NowPlayingToggleButtons` |
| A text size on every `TextBlock` overwrote page titles | Too broad. Target under a named parent, or use a relative size (`font-size: 120%`) |
| Large text: playback bar and search icons cut or gone | `Button { padding: 8 16 }` reached fixed-size icon buttons. Pad buttons only under a named parent |
| Kit buttons stretched across the page after a render that changed nothing about them | An implicit style applies only once the element is in the tree. The renderer writes the alignment as a local value whenever none is set (buttons, dropdowns, images and icons start left) |
| About a hundred native controls rebuilt when a status line appeared | The kernel matched nodes by position and React keys stayed in the script's tree. The key of a React element goes on the nodes it draws |
| A kit page title looked like body text | The renderer's type ramp: `title` 28 px and `subtitle` 20 px, semibold |
| Blue selection bar left on the start page (Radio) after opening the Marketplace from the folded sidebar | NavigationView puts out only the `Rectangle#SelectionIndicator` it lit itself, and the start page's bar was lit without it. Under an app page, put out every lit bar of `Grid#PaneContentGrid`, keep each with its opacity, and put them back when the sidebar's next selection is their item (`ClearSidebarSelection`, `WatchSelectionBars`) |
| Folded sidebar with floating panels: account name showing, panel over the page's edge | The folded `SplitView` clips `Grid#PaneRoot` to `CompactPaneLength` from the pane's own edge. Use `compact-pane-length: 48` and take the space between panels with the page's left margin and a negative right margin on the sidebar |
| Apple's logo 9 to 12 pixels right of the icons' axis in the folded sidebar | `Grid#PaneHeaderGrid` starts 26 left of the column, so its content sits 15 in. While the back arrow shows, Apple moves that grid to `Margin=5,...`. `floating-panels` counts the 15; read the grid's margin in the inspector in both states before placing anything in the header |
| Glass: folded sidebar entries 2 pixels left of Apple's icons | Glass gives Apple's items no side margin. Entries' icons move onto the axis measured on Apple's first item (`IconBox`, `EntryIconShift`), measured from `ActualOffset` so a hover slide does not count |
| Square frame of shadow down the whole window beside the open pane (floating panels, narrow window) | In Compact or Minimal display mode, NavigationView gives `Grid#ShadowCaster` a `ThemeShadow` as wide as `OpenPaneLength` and as tall as the window. `floating-panels` collapses it. Its states animate opacity, width and alignment but never visibility, so the local value holds |
| With that shadow gone, a pane of the page's own colour had no edge | While the pane is open over the pages and the caster is collapsed, Ampify adds a 1 px Border in `SurfaceStrokeColorDefaultBrush` over the grid that paints the pane (`EdgeOverPages`). A theme that draws its own border there (Glass, High contrast, Console) keeps it alone |
| Cinema: pages showed through the open pane's entries | Cinema dims `.sidebar` with `opacity`, which makes the whole pane see-through. While the pane stands over the pages (`ViewSize::sidebarOverPages`), the engine leaves out a theme's opacity on the pane layers (`KeepSidebarOpaque`, `SidebarPaneLayer`) |
| Hover sidebar pinned in a narrow window (about 1094 px at 125%): folded column, cut search box, no hamburger | At 1324 and less, Apple's layout is the narrow one, and a pane opened beside the pages (`PaneDisplayMode` Left) is clipped. The pin pushes the pages only above 1324 (`PinPushesPages`). Narrower, the pane stays open over the pages (LeftCompact, light dismiss off). A mode change closes the pane first so it reopens with Apple's header for that mode |
| Hover sidebar: hamburger over the logo, missing title and "···", or nothing to unpin | Apple lays out the header in its own `PaneOpening` and `PaneClosing` handlers. WinUI raises `PaneClosing` late, and Apple hides the title and "···" in its handler even when Ampify refuses the close. When Ampify refuses a close, show again the header elements Apple showed at the last opening. Only show, never hide: hiding the pin under the keyboard sent the focus to search |
| Hover sidebar pinned: squashed column after a click, header reduced to logo and pin | NavigationView turns its own `IsPaneOpen` off before its SplitView raises `PaneClosing`. After the refused close has run every handler, set `IsPaneOpen` back on and show again `TextBlock#AppName` and `MenuButton#MoreButton` if collapsed |
| Hover sidebar: Enter on the hamburger showed no pane | The hold read the hamburger's `FocusState` in `Click`, but WinUI has already moved the focus into the pane. A `Click` with the real cursor off the button came from a key or UI Automation; that, or a key down in the open pane outside a text field, holds it |
| Hover sidebar under Mirror: pane closed at once | Hit tests read the cursor as an offset from `TransformToVisual(nullptr).TransformPoint({0,0})`, which goes negative inside a right-to-left split view. Place the point along each axis from where local 0 and 1 land (`LocalOffset`) |
| Hover sidebar: after a tap on Alt, hovering did not open the pane | The window under the cursor was a `Microsoft.UI.Content.PopupWindowSiteBridge` owned by Apple's window. The cursor counts as over the window when the window under it is Apple's or owned by it (`GA_ROOTOWNER`) |
| Hover sidebar: Space on the hamburger did nothing | Apple marks Space handled before the content sees it. Handlers on the hamburger take handled events too: a Space released there without a `Click` toggles the pane and holds it for the keyboard |
| Hover sidebar: entries moved 1 px sideways on every second open, and 1 px down from folded to open | Half-pixel icon shifts round differently per layout. The shift is in whole device pixels at the window's scale, measured again on each entry's `SizeChanged`, and the button's content stretches to its height so the icon is centred in both states |
| Hover sidebar open over pages with no theme or Fluent: pages showed through its entries | Apple's pane is see-through over its backdrop, and Fluent writes `.sidebar-column { background: transparent }`. Ampify adds its own Border as first child of `Grid#PaneContentGrid`, with `SolidBackgroundFillColorBaseBrush`, shown only while the pane is open over the pages and neither the grid nor `Grid#PaneRoot` paints |
| `.sidebar-item:hover` lit the group above too | A parent `NavigationViewItem` holds its children, so `PointerEntered` fires on it. The first `NavigationViewItem` above `OriginalSource` must be the item itself (`PointerOnOwnRow`) |
| Bottom bar: hamburger on the logo in a wide window | `Grid#PaneToggleButtonGrid` lies outside the pane. `.sidebar-toggle-area` follows the panel's top and left margins at every width |
| Floating panels: back arrow across the sidebar panel's corner | Apple pins `Button#NavigationViewBackButton` to the window corner. `floating-panels` moves `.back-button` one gap in and down to the middle of the app's logo |
| Bottom bar: buttons about 30 px left of centre, volume short of the right edge | The middle buttons and extras spanned only the columns left of Apple's caption-button cover. `bottom-bar` spans `.playbar-controls` and `.playbar-extras` across all four columns and collapses `.caption-buttons-cover` |
| Floating panels, narrow window: song title cut with empty space before the volume | Apple splits the bar in star columns. `floating-panels` spans `.now-playing` over two columns and the kernel (`FollowSongRoom`) sets its right margin so it ends 24 short of the first toggle |
| Bottom bar: artwork flush against the window's edges | `bottom-bar` gives `.now-playing` a gap on the left and 4 above and below, so the 54 px artwork is square again |
| Bottom bar: extension panels covered the window's buttons | With the play bar moved down, the pages reach the window's top. The panels start under `Border#Titlebar` |
| Floating panels: the More button (`···`) stopped opening its menu | `Border#Titlebar` lies over the top 64 px of the window. Apple's brush lets clicks through; a colour written by a theme, even transparent, catches them. Layouts leave `.titlebar` alone, and `docs/themes/names.md` tells creators the same |
| Full-screen player: dragging its background left the window stuck to the pointer | A move loop started from a press XAML had taken never sees the button go up. Use `InputNonClientPointerSource`: the whole client is a `Caption` region and controls are `Passthrough`, laid out again every half second. The drag is Windows' own, so snap layouts and FancyZones work |
| Closing that player restored a maximized main window on the wrong screen | Restore the main window maximized only if the player is maximized or never moved. Otherwise it takes the player's place |
| Apple's logo over the song title after leaving a theme that hid it | Apple binds `Image#Apple`'s visibility to what plays, as a local value. Hide it with `opacity: 0`, which Apple leaves alone |
| Apple Music ended by WinUI (0xc000027b) when the window narrowed and play bar buttons folded | The fold was decided inside `SizeChanged`, during layout, so it folded and unfolded in turn until WinUI stopped the cycle. `SizeChanged` queues one low-priority evaluation on the dispatcher, each step queues the next, and no fold is decided in the round where the song's margin just changed |
| Centred bar: no progress line and no times | The line's template repeats its margin on all four sides, and the times' visibility comes from `NowPlayingControl`'s state setters, which outrank a local value. `centre-bar` gives the line side margins only. `visibility: visible` is held by a storyboard begun after Apple's state and begun again when a later state collapses the element (`HoldVisible`). It is never a local value, which replaced Apple's thumb binding |
| Centred bar: Apple Music closed at the first hover (stowed 0x800F1000) | `NowPlayingControl`'s states find the times by name in its template, and moved out of it they had no target. A moved element hands its states over to a never-shown stand-in of the same class and name (`HandStatesOver`) |
| 0xc000027b about 2 s after the lyrics pane opened under Glass (layout cycle) | `floating-panels` gave `Grid#PaneRoot` of `SplitView#ContentSplitView` `width: auto` and `horizontal-alignment: stretch`, but the template binds that width to `OpenPaneLength`. The engine keeps a SplitView pane to its template for every theme (`src/core/theme/pane.cpp`): alignment and grid column are refused, and `width: auto` becomes the open pane length less the horizontal margins |
| Glass: square patches over the lyrics pane's rounded corners | WinUI rounds only a Grid's own background and border, so what its children paint stays square. The engine clips the pane's content host to the corners through that Border's hand-out visual (`src/win/pane_corners.cpp`) |
| Glass: folded column outline missing on its left corners | The pane (290 wide, corners 24) cut the column's outline (48 wide, corners 16). The folded rules give `.sidebar` square corners |
| Console: folded column's top edge missing | The column was lifted 1 out of `Grid#PaneRoot`, which clips to its bounds. The column stands flush (`margin: 0`) |
| Glass's rim (135deg `linear-gradient` border) dark at two corners of a tall narrow block | A `LinearGradientBrush` in relative points is stretched with the element. The engine fits the points to the element's size and follows size changes (`theme::FitGradient`, `src/win/gradient_fit.cpp`). `:hover` gradients in state keys keep the square's points |
| The play bar's "More" button cut at the bar's edge | A bar too narrow for its columns clips the toggles. The spare room also counts the toggles' slot (`LayoutInformation::GetLayoutSlot`) less their width |

Names from WinUI templates (`Grid#RootGrid`, `Grid#LayoutRoot`, `Border#Root`, `ContentPresenter#ContentPresenter`)
appear dozens of times. Target them under a named parent.

## Animations

| Problem | Cause and fix |
|---|---|
| A theme `transform` blocked `@keyframes` rotation; most artworks refused an animated scale | `UIElement.Scale`, `Rotation`, `Translation` and `CenterPoint` cannot be used with `RenderTransform` on the same element: `StartAnimation` throws. Apple moves many artworks itself. `Opacity` is exempt. The animation is refused with a log line, and `docs/themes/format.md` tells creators to put the transform in the stops |
| `overflow: hidden` clipped an artwork but was refused on cards | `ElementCompositionPreview.GetElementVisual` throws on an element using `Translation`, `Scale` or `Rotation`, and once taken, the visual makes those properties throw for Apple. Never take the hand-out visual: clip with `UIElement.Clip`, a `RectangleGeometry` resized on `SizeChanged` |
| A finite animation without `forwards` stayed on its last frame | A completed composition animation leaves the final value. It runs with `IterationBehavior::Forever` and `StopBehavior::SetToInitialValue`, and a `DispatcherQueueTimer` stops it when its iterations end |
| Rotation around the top left corner | `CenterPoint` is 0,0 and the element may not be laid out yet. An expression animation on `CenterPoint` reads `this.Target.ActualSize` |
| Glass: a sidebar entry jumped 6 px on hover | `NavigationViewItem` refuses the facade's transitions, so each change applied at once. The transform eases on Ampify's `CompositeTransform` through a Storyboard (`WriteStoryTransform`). `opacity` and `background` stay refused there |
| A button that grows on hover grew towards its bottom right | XAML scales around the top left, CSS around the centre. A `CompositeTransform` on an element with no theme origin turns around its centre (`CentreOn`) |
| Text glyphs moved by `translate` vanished from the playback bar | The buttons clip their content. Translate the button itself |
| A colour animation made the log say "1 value applied" every 3 seconds | The animation draws the `background` the theme also sets, so the engine read back an animated brush. The backend keeps the background the theme asked for while the animation owns it (`WantBackground`) |
| A colour animation on the sidebar's root did nothing visible | Its children draw an opaque background over it. Pick the element that draws the colour with `ampify devtools pick` |
| The spinning record drifted about 2.5 px and was not round | Apple gives the artwork, its placeholder and the grid inside a fixed width of 54, and the turn's centre was an expression on the old size. The snippet sizes those three elements to the record, and `StepLoops` sets `CenterPoint` from `ActualSize` at each step |
| The sidebar opened under the pointer with a slide and snapped shut | WinUI's SplitView folds in 119 ms against 349 to open, on an ease that does most of the move in the first frames, and Apple narrows its `Grid#PaneContentGrid` to 48 px the moment the pane closes (from no state Ampify can hold), so the fold slid its clip over an empty pane. Stretching the fold's key frames or holding `NotClosedCompact` and `ListSizeFull` changed nothing on screen. Ampify slides the SplitView's own `PaneClipRectangleTransform` while the pane is still open, to where the fold leaves it, then folds it without transitions (`SlideShut` in `src/win/sidebar_pane_dress.cpp`). Measured frame by frame with `PrintWindow` |

## Fonts and icons

| Problem | Cause and fix |
|---|---|
| Icons as empty squares | A `TextBlock { font-family }` rule changed the font of private-use glyphs. The engine never changes the font of a text made of icons |
| "Primary" buttons as symbols | Apple's `AccentButtonStyle` draws text in a symbol font. Use the plain button style and set accent colours on top |
| Part of an SVG icon missing | XAML path markup does not go back to the start of a closed subpath: a relative `m` after `z` moves from the last point. Start each subpath after `z` with an absolute `M` |
| An extension's active play bar button stayed white | A `FontIcon` takes the button's foreground and ignores the brush key given to `MakeIconElement`. `TintControl` sets the glyph's `Foreground` to the accent brush, and leaves it unset when off |
| A button did not go back to white when switched off | `SurfaceTracker` compares a control's signature, which left out `active`. Every field the backend draws belongs in `Signature` (`src/core/ui_render/surfaces.cpp`) |

## Windows

| Problem | Cause and fix |
|---|---|
| Apple's full-screen player lost its resize border after `WS_THICKFRAME` was set | The FullScreen presenter puts its own style back. Give the window an `OverlappedPresenter` with `SetBorderAndTitleBar(true, false)`, on its interface thread |
| 3 px light line around the screen above the taskbar | The window has `WS_DLGFRAME`, which the AppWindow puts back. Place the window so its client area fills the work area and cut the frame with `SetWindowRgn` (removed again for window modes) |
| Apple's shrink button left only the mini player in a corner | `ExitFullScreenButton` turns the player into the mini player and the main window stays hidden. Collapse it while Ampify places the player. `CloseFullScreenButton` and Escape bring Apple Music back |
| Opening the player from the mini player gave both windows back on Escape | In mini player mode Apple minimizes the main window. Hide and restore only a main window on screen (`PlayerReplacesMain`) |
| Which new window is Apple's player? | Enumerating the process's windows also finds dialogs and Ampify's. Ask by name: `GetAppProperty("FullScreenPlayerWindow")` |

## Build and capture

| Problem | Cause and fix |
|---|---|
| MSVC build failed on a PC | `build-msvc.ps1` took the first `cppwinrt.exe` (SDK 17763, C++/WinRT 1.0). It now takes the newest SDK with an x64 `cppwinrt.exe` |
| XAML link error on `RoGetActivationFactory` | Link `runtimeobject` |
| CI: `'GetClassNameA': is not a member of ... IAutomationPeer` | `windows.h` between two WinRT headers renamed the method through its `GetClassName` macro (CI builds without `UNICODE`). Include every WinRT header before `windows.h` |
| Screenshot cut off on the right | PowerShell is not DPI aware and the screen is at 125%. `SetProcessDpiAwarenessContext(-4)` before capturing |
| CI build back to six minutes | A header included by the slow XAML files changed. Keep headers they include small, since ccache runs without the precompiled header |

## Apple Music's quirks

Finding the window and the UI thread is in [how-it-works.md](how-it-works.md#the-window-and-the-ui-thread). A
`WH_GETMESSAGE` hook never fired on Windows Server 2025; the DispatcherQueue works.

- Apple Music keeps the first 64 pixels of a page for its playback bar, in more places than the list's padding: the
  page backdrop (`ContentPresenter#PageBackground`, an artist's video) is laid 64 down, and the lyrics and queue pane has
  a 64-high first row. A layout that moves the panels below the bar must take those 64 back too, or a dark band shows.
  Look for a fixed 64 when something sits lower than its panel's top.
- Apple paints some surfaces in code with a fixed colour, outside any key: the search's top-result tiles (`#FF282828`)
  and an album's artist (its blue). Only a rule on the element reaches them (Ampify derives one from the theme).
- The progress bar (`AMP.UI.Controls.ScrubberControl`) is a Slider whose template reads Apple's own keys, even at rest:
  `ScrubberDecreaseRectFill`, `ScrubberTrackRectFill`, `ScrubberThumbNormal`, each with `PointerOver`, `Pressed` and
  `Disabled` variants. The mini player defines them, and the Slider keys, in its own dictionary over the application's,
  so neither the control's `Foreground` nor a theme's keys reached the bar. `HoldThroughStates` writes them in the bar's
  own resources. The volume slider reads WinUI's Slider keys and followed the accent already.
- WinUI resolves `ToggleButtonBackgroundChecked`, `AccentButtonBackground` and `AccentButtonForeground` from the accent
  once, when its dictionaries load. They are derived from the theme's accent with the other accent keys.
- Apple's lists (`AMP.UI.Controls.ListView`, the Songs table, album track lists) set their selection colours in their
  own style, so a theme's `ListViewItemBackgroundSelected*` keys never reach them. The row's `ListViewItemPresenter`
  takes a local `SelectedBackground`, `SelectedPointerOverBackground` and `SelectedPressedBackground`
  (`.list-item { selected-background: ... }`).
- Apple's main window closes through `WM_CLOSE`, which WinUI turns into `AppWindow.Closing`. A subclass of the main
  window that answers `WM_CLOSE` itself keeps Apple Music from shutting down, and `ShowWindow(SW_HIDE)` hides it while
  the music goes on (`src/win/close_to_tray.cpp`).
- Scrolling Home moves in steps with or without Ampify, and after a few minutes of browsing the interface thread stalls
  for about 240 ms at a time while shelves load. Ampify's passes after new rows add 1 to 4 seconds a minute of that
  thread, logged as "passes after changes in the last minute" when above 200 ms.
- XAML Diagnostics (`xaml.watcher`, `src/win/xaml_watcher.cpp`) works in 1.1792 without screen flashes. Apple Music does
  not expose the hooks that release the handles it gives, so it keeps a record of every element added (130,000 in five
  minutes), browsing slowed down and UI Automation calls stalled up to 2.8 s. It is off by default; frames, lists and
  popups already ask for a pass.
- Menu entries of extensions are added after a real pointer release or key press on the window. A UI Automation Invoke
  does not count. To test without the mouse, post Enter to the sidebar actions button (`MoreButton`), which opens the main
  menu. A song's menu needs a real right click.
- Apple's volume slider is in decibels and is the model. Setting `IVolumeControl.Level` (0 to 1) on the main window's
  `VolumeControl` makes Apple's own binding set the player volume and redraw. The control's DataContext is empty, and
  the ABI is written by hand in `src/win/player_controls.cpp`. `src/win/win_host.cpp` is compiled without
  `AMPIFY_WITH_XAML`: code that needs XAML there is reached through a function pointer installed from `xaml_host.cpp`,
  or it silently compiles to nothing.
- The row a context menu is about: the data context of the row (a `DataGridRow` for a right click, a `TrackLockup` for
  its "..." button). `IContentDescriptorDriven.get_ContentDescriptor` gives the catalog id (`AdamId`), Apple's
  `CanonicalId` and kind flags (`src/win/amp_item.cpp`). A song of the user's library has neither id, only the "local
  library identifiers" flag. The page the row sits in is driven by a descriptor too (a playlist), so the walk up the tree
  accepts only a data context titled like the menu ("Play «Name»").
- Lyrics: a song timed by line draws each line in a `TextBlock#LineContainer`, one timed by word in a
  `Border#WordByWordHost`. Both sit in the line's `Grid#ContentGrid`, which Apple fades by state (`NotCurrent` 0.5,
  `Current` 1). The tree holds only the lines Apple Music has realized.
- First start: an MPEG LA licence (Agree, Decline, Save As) shows before the main window, then after sign-in a "Welcome
  to Apple Music" screen (Start Listening). No XAML window exists while they are open. The bench closes them through UI
  Automation.

See also [knowledge/apple-music-xaml-tree.md](../../knowledge/apple-music-xaml-tree.md) and
[knowledge/amexclusive-notes.md](../../knowledge/amexclusive-notes.md).
