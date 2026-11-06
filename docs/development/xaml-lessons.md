# XAML lessons

Problems met while drawing inside Apple Music, and what fixed them. Read this before changing `src/win/`.

1. In a process you do not control, hold everything you create with a strong reference, and release it on the UI
   thread.
2. Check a screenshot, since the log has said "mounted" or "applied" while nothing was visible.
3. Compare before and after side by side. Colors close to Apple Music's own show no difference.

## Objects, popups and lifetime

| Problem | Cause | Fix |
|---|---|---|
| A React panel never visible, "mounted" every 2 to 5 seconds | The host's Popup was held only by a weak reference and destroyed at once | Popup held by a strong reference (`WindowContext::panelPopup`) |
| Same with the host placed in the page | Apple Music removes unknown children from `Grid#PageRoot` | Host in a Popup of the window, held strongly |
| Panel remounted every 5 s, then an empty frame | **A weak reference to a XAML object created by Ampify never resolves inside Apple Music**, even when it is displayed (proved: the content slot could not be found right after creating it). Weak references to Apple Music's own objects work | Everything Ampify creates is held strongly: frames, slots, renders, dialogs, buttons, page hosts |
| App pages host lost | Added to `Grid#PageRoot`, which Apple Music empties | Popup of the window, like the panels |
| Dark panel under a light theme | A Popup is outside the window tree, so it does not follow its mode | `RequestedTheme` copied at each refresh |
| Crash in `Microsoft.UI.Xaml.dll` at close | The theme service walked the tree during close | `Window.Closed` sets a flag, and nothing touches XAML after it |
| XAML objects released from another thread | Possible crashes | Everything XAML is released on the UI thread (`ReleaseXaml`, `DropContext`). Static services are never destroyed |
| Navigating twice to the same page was not seen | Only the route was compared | A counter (`RouteSerial`) fixes it |
| A large cover never replaced the blurry one | A `BitmapImage` that nothing in the window shows is never decoded: `ImageOpened` and `ImageFailed` never fire | Load it through an element in the tree (the invisible sizer of the image node, held at its height meanwhile), then give it to the visible brush |
| Old handlers on a bitmap or a frame kept running | `auto_revoke` revokes through a weak reference to the source, which never resolves for an object Ampify made | Remove handlers by token (`ImageOpened(token)`), and check the sender is still the latest one |
| The full-screen player kept the first song's cover for all the next ones | A value set on `Image.Source` of Apple's cover replaced the binding that brings each song's image | Never write a bound property of Apple's element: lay Ampify's own element over it, in the same cell, and follow Apple's with `RegisterPropertyChangedCallback` (`src/win/sharp_artwork.cpp`) |
| A settings card's Reset, Reset all, Import and status line did nothing on every card but the last one built | Each new card dropped the cards whose panel had no `Parent()`; while the page is built, nothing in it is connected to the window yet, so every earlier card was dropped and its buttons reached nothing | Never ask `Parent()` whether something Ampify built is still shown: the page owns its cards and a card replaces only the one of the same element |
| Pages made room for the panels, then ran under them again | Themes write `Grid#ContentRoot`'s margin, and write it again while they run | The room is the padding of its parent grid, which themes do not write (`PushPages`) |

## Resources, colors and themes

| Problem | Cause | Fix |
|---|---|---|
| Midnight theme: white text on a white background | The theme lightened the text without darkening the background | The root `RequestedTheme` follows the scheme (Dark for a dark scheme) |
| Midnight looked like native dark mode | The palette was too close to Fluent dark, and resources have no effect on backgrounds | A night-blue palette, backgrounds targeted by name |
| Replaced colors had no effect on screen | `{ThemeResource}` is only re-read at a mode change | Switch light then dark, then back, after each resource change |
| Risk of breaking: a brush written in place of a color | Fluent names colors with no `Brush` suffix | The write keeps the type already in place |
| A resource stayed fixed after removing the theme | The key added above the theme dictionaries hid the light and dark values | The key added by Ampify is removed on removal |
| LCD white after removing a theme | Ampify kept the brush color, not its opacity (white at 8%) | The original brush is kept as it is |
| Apple Music's backgrounds ignore Fluent brushes (`SolidBackgroundFillColorBaseBrush`...) | They use their own brushes | Target the elements by name in `user.css` |
| The resource inspector failed in a loop (0x80004005) | Walking the resource dictionaries | try/catch per dictionary and per key |
| Inspector: empty tree | The snapshot was taken before the window had content | No snapshot on an empty tree, try again at the next tick |
| Apple Music crashed (0xc000027b) at the theme change after an inspector pass | Enumerating the application's dictionaries realizes deferred resources, and the next refresh fails on them | No enumeration: the inspector asks `HasKey` for a fixed list of Fluent colour keys (`src/core/theme/colour_keys.cpp`) |
| A button lost the theme's colours under the pointer and while pressed (Greenroom's play button turned black) | The button's visual states set Fluent resources, which win over local values | The colours are also written into the control's own resources under `{Type}{Property}PointerOver` and `Pressed` keys |
| The window froze for about half a second every 3 seconds with Greenroom | Each write of the play button's background put new state keys, which asked for a refresh of the whole window's resources (400 to 800 ms on the interface thread); the refresh reset that background, which the next pass wrote again | State keys are written only when their colour changes. The log gives the duration and the cause of every refresh |
| The window froze for 670 ms the first time the volume flyout opened with Greenroom | The slider's new state keys still asked for a refresh of the whole window | The control that holds the keys switches to the other mode and back, alone (`ReadThemeAgain`): its states read the keys again, the rest of the window does not move |
| A theme reached nothing in the volume flyout, the menus or the tooltips | They open in popups, outside the window's content, and the snapshot walked that content only | The snapshot walks each popup seen open (`GetOpenPopupsForXamlRoot`), from then on even closed. A click or a key looks for a popup that opened at once, a timer every quarter of a second |
| Removing a theme while a menu was closed left its entries with the theme's colours under the pointer | WinUI takes a menu's entries out of its tree when the menu closes, and puts the same objects back at the next opening: the engine took them for gone and forgot their originals | The engine keeps what it owes an element out of the tree while it exists (`TreeBackend::Alive`), and writes it back when the theme comes off. A popup that opens again gets a pass, since Apple Music sets its entries' colours again |
| Apple Music stopped (0xc000027b) when Rounded buttons replaced Midnight | The theme's button template binds the control's `Padding` and `BorderThickness` into a `Border`. The search box's own buttons have a padding of `0,0,-2,0`, and the `Border` refuses a negative side at the next layout (`ValidateThicknessValue`, E_INVALIDARG), where WinUI fails fast instead of throwing | `user.css` refuses a negative padding, border or corner; no template goes on a control whose padding or border is negative; `Restore` and `GiveBack` write a control's own template before its other values |
| An album cover of 360 stayed cut at 270 high, with square bottom corners, even once its `ArtworkMaxSize` was lifted | Apple also sets the `ArtworkImage`'s own `MaxHeight` to 270 (not its `MaxWidth`); a `Height` above `MaxHeight` is clamped, and the 360 picture inside overflowed and was clipped | `FollowArtworkCap` lifts the element's `MaxWidth` and `MaxHeight` with the cap and gives Apple's back when the size goes |
| Centred lyrics: the second row of a wrapped line went back to the left for up to 3 seconds, again and again | Apple writes a lyrics line's `TextBlock#LineContainer` `TextAlignment` again in code; the theme's value came back only at the next tick | A text alignment the engine wrote is followed (`FollowAppleRewrites`): Apple's write asks for a pass at once |
| Large text rewrote every text size every 3 seconds | WinUI keeps sizes in single precision: 16.8 reads back as 16.799999237060547, and the text comparison saw a new value on each pass | Numbers are compared as WinUI keeps them (`SameValue`) |
| The "Open the Marketplace" button of the Settings card showed symbols instead of its text | Apple Music's `AccentButtonStyle` sets its `FontFamily` to WebDings, and its template draws the label with it | The plain button style of the page with Ampify's accent template (`PrimaryButtonTemplate`), as kit buttons do |
| Greenroom reported a refused `Foreground` on `FontIcon#SubItemChevron` | The hidden chevron of an `AppBarButton`'s template is listed by the tree walk but cannot be reached afterwards (never realized) | A value for an element gone since the snapshot is skipped, not refused |
| After a live switch of theme, the Marketplace page kept the accent of the theme before | Ampify's pages and panels are popups without a parent: switching the mode of the window's content does not reach them | After the window, each open popup reads its theme again alone (`ReadPopupsAgain`) |
| Extensions had no way into Apple Music's menus | Apple Music builds them from its own lists (`ExtensibleMenuFlyout`), and nothing tells when one opens | After a click or a key, the open `MenuFlyoutPresenter` is looked for: its `ItemsSource` is the menu's own items, where Ampify's entries go while it is open (with the style of Apple Music's entries) and leave when its popup closes. A menu is an item's when one entry quotes a name ("Play «Name»"); the main menu is the one opened from `Grid#PaneHeaderGrid` |
| The album artist stayed Apple blue under every theme, although the inspector showed the theme's colour on its `TextBlock` | The name is a `Hyperlink` inside the `TextBlock`, its runs keep their own colour, and Apple Music fills the link after the theme's first pass | A text colour also goes to the inlines of a text that holds a link, and is read back from its last run, so a later pass sees the difference and writes again |
| A theme without colours (Hide chrome) turned a dark Apple Music light | The app mode was set from the scheme name, light when there was none | The mode follows the theme's `dark` flag, then its scheme name, and is left alone when neither says anything (`AppModeOf`) |
| Glass: a button under the pointer showed Apple Music's red under black text | The theme gave `AccentFillColorDefaultBrush` only; the pointer-over and pressed states use `AccentFillColorSecondaryBrush` and `...TertiaryBrush` | Both are worked out from the accent at 90% and 80%, as Fluent does, unless the theme names them |
| The artwork's colours (`--art-*`, Cover ambience) followed a new song 3 to 6 seconds late | The play bar's image was found only by the tick's walk, and the service's thread read its colours at the start of the next tick | The image is followed through `ImageOpened` and its `Source`, and new colours wake the service (a condition variable): the halo changes about 0.2 s after the song |
| Settings pages stayed grey (#1C1C1C) and their toggles and sliders Apple blue under every theme | Each settings page is a class of its own (`PlaybackSettingsPage`...) whose background Apple sets in code; WinUI's `ToggleSwitchFillOn`, `SliderTrackValueFill`... are set from the accent once, when its dictionaries load | The settings pages are `.page` too, and the control keys are worked out from the theme's accent (`DefineAccentControls`) |
| With the accent following the artwork, the bars next to the row playing kept the song before's colour, although the inspector showed the new one on `BouncyBarControl.Foreground` | The bars are composition sprites (a `ShapeVisual` per `Canvas#barNHost`, set with `SetElementChildVisual`) whose brush copies the Foreground once, when the row is built, and a new song's row is built before its accent comes | After writing a control's Foreground, the engine gives the new colour to the colour brushes of the child visuals under it, when they are all of one colour (`FollowForegroundInSprites`); never the element's hand-out visual |

The application dictionary has only one key of its own (`ContentPageShadow`). Colors are in merged and theme dictionaries.
Walking some of them fails with E_FAIL, so each part is read inside a try/catch. Do not walk them at all while
Apple Music runs: see the crash above.

Visual states are not there to read before a control is first hovered: WinUI builds the state groups of an
`AppBarButton` lazily, so a search for the setters of `PointerOver` finds nothing on a fresh button. Writing the state
keys into the control's resources works before and after the groups exist.

## Selectors and layout

| Problem | Cause | Fix |
|---|---|---|
| Glass theme: playback bar and playback buttons gone | `Grid#ContentRoot` exists in the NavigationView, in every playback button and in the page area. `z-index: 1` put the bar (z-index 10) under the pages | Selector under `SplitView#ContentSplitView`, no z-index |
| A margin on `Grid#ContentRoot` alone pushed the playback bar down and hid its three buttons | Same duplicate name | Same fix |
| Top bar button not found | Apple Music has no separate top bar | Ampify's buttons go in `StackPanel#NowPlayingToggleButtons` |
| A text size on every `TextBlock` overwrote page titles | Too broad a selector | Target under a named parent, or use a relative size (`font-size: 120%`), which scales each text from its own size |
| Large text: icons of the playback bar and the search box cut or gone | `Button { padding: 8 16 }` reached the icon buttons, which have a fixed size | Button padding only under a named parent |
| Kit buttons stretched across the page after a render that changed nothing about them | An implicit style applies only once the element is in the tree: the renderer saw the alignment it wanted, wrote nothing, and the style then put its own | The renderer writes the alignment as a local value whenever none is set; buttons, dropdowns, images and icons start on the left |
| About a hundred native controls rebuilt when a status line appeared above the Marketplace cards | The kernel matched nodes by position, and React keys stayed in the script's tree | The key of a React element goes on the nodes it draws |
| A kit page title looked like body text | No type ramp in the renderer | `title` 28 px and `subtitle` 20 px, semibold, as in Apple Music's own pages |
| Folded sidebar with floating panels: the start of the account name showed, and the panel covered the page's edge | The folded `SplitView` clips `Grid#PaneRoot` to `CompactPaneLength` counted from the pane's own edge, so its left margin pushes the visible part into the page column instead of shrinking it | `compact-pane-length: 48`, and the space between the panels taken by the page's left margin (two gaps) with a negative right margin on the sidebar (one gap) |
| Hover sidebar pinned in a narrow window (1094 px wide at 125%): the column stayed folded, its search box cut to a letter, the pages pushed 300 px right over a blank band, and the hamburger gone, so it could not be unpinned | At 1324 and less, Apple's own layout is the narrow one: a pane opened beside the pages (`PaneDisplayMode` Left) is clipped to the column and the toggle hidden | The pin pushes the pages only above 1324 (`PinPushesPages`); narrower, the pane stays open over the pages (LeftCompact, light dismiss off), and a change of width pins again for the new one. A mode switched under the open pane left the template folded (the same squashed column after a resize from wide to narrow): the pane closes with the switch and opens again at the next tick |
| Glass: Ampify's folded sidebar entries sat 2 pixels left of Apple's icons, though in line with them under the other themes | The entries are centred in the column; Glass gives Apple's items no side margin, which moves their icons off the column's middle | The entries' icons move onto the axis measured on Apple's first item (`IconBox`, from its item's `ActualOffset` so a hover slide does not count; `EntryIconShift`). The entry's own axis comes from its `ActualOffset` too: Glass's column is narrower than the button, which overflows its slot, and the slot's middle put the icons 1 px right. The open row has its own shift, measured open: its icon does not start where the folded one is centred |
| Floating panels: the back arrow sat across the sidebar panel's rounded corner, half on the window | Apple pins `Button#NavigationViewBackButton` to the window's corner (margin 4,0,0,0); the panel starts one gap in | `floating-panels` moves `.back-button` one gap in, and down to the middle of the app's logo (one gap less 1; one gap and 4 left it 5 pixels low, measured at 125%) |
| Folded sidebar: Apple's logo 9 to 12 pixels right of the icons' axis in every theme (Mirror: a sliver at the column's edge) | `Grid#PaneHeaderGrid` starts 26 left of the column (`Margin=-26,-42,0,0`), and the header's column 2 starts 41 into it, so its content is 15 in from the column's edge; a margin worked out from 6 missed 9. While the back arrow shows, Apple moves that grid to `Margin=5,...` (31 right), which pushes the logo out of the folded column for the arrow; Mirror's fixed -32 held only in that state, and a right margin on its header left no room for the logo | `floating-panels` counts the 15 (`(compact - 22) / 2 - 15`); Mirror spans the header over all its columns from 41, and keeps "···" clear of its buttons with that button's own right margin. Read the grid's margin in the inspector in both states before placing anything in the header |
| Bottom bar: the buttons sat about 30 pixels left of the window's middle, the volume a column short of the right edge, and themes collapsed the window buttons' column by hand | The middle buttons and the extras spanned only the columns left of Apple's caption-button cover | `bottom-bar` spans `.playbar-controls` and `.playbar-extras` across all four columns and collapses `.caption-buttons-cover`; the times sit under the scrubber's ends |
| Floating panels, narrow window: the song's title cut while an empty stretch stood before the volume | Apple splits the bar in star columns (about 3:4:3 plus a narrow fourth); with the bar inset and the toggles moved to its right end, the song kept its column and fixed margins could not follow the toggles' count | `floating-panels` spans `.now-playing` over two columns and the kernel (`FollowSongRoom`) sets its right margin so that it ends 24 short of the first toggle; a layout that gives the song a width or another alignment keeps its own |
| Full-screen player in a window: a drag from its background left the window stuck to the pointer after the release, and FancyZones never saw it | A move loop started from a press XAML had taken (`SC_MOVE`, then `WM_NCLBUTTONDOWN` sent from `PointerMoved`) does not see the button go up: Windows never had that press | `InputNonClientPointerSource`: the whole client is a `Caption` region and the controls (buttons, sliders, list rows, scrolling lists) are `Passthrough`, laid out again every half second. The drag is Windows' own, so snap layouts and FancyZones work and it ends on the release |
| Closing that player brought a maximized main window back maximized on the first screen, though the player had been snapped smaller on another | The main window was restored maximized whenever it had been | It is maximized again only if the player is maximized or never moved; otherwise it takes the player's place |
| `.sidebar-item:hover` lit the group above as well (Library while over Artists) | A parent `NavigationViewItem` holds its children, so `PointerEntered` fires on it too | The hover of an item counts only when the pointer's own row is under it: the first `NavigationViewItem` above `OriginalSource` must be the item itself (`PointerOnOwnRow`), checked again on `PointerMoved` |
| Bottom bar: the song's artwork flush against the window's left and bottom edges, its placeholder stretched to the bar's height | The display filled its column from the window's edge | `bottom-bar` gives `.now-playing` a gap on the left and 4 above and below: the 54-pixel artwork is square again |
| Apple's logo over the song's title after leaving a theme that hid it | Apple binds `Image#Apple`'s visibility to what plays, a local value: the engine recorded "Visible" before the theme and wrote it back when the theme went | Hide it by `opacity: 0`, which Apple leaves alone, so the engine clears it instead of restoring a stale value |
| Extension panels stayed in Fluent's grey under every theme | Their frame reads `AcrylicInAppFillColorDefaultBrush`, which no theme names | The engine derives the acrylic fills from the theme's card colour when it is opaque |
| Bottom bar: the extension panels covered the window's buttons | With the play bar moved down, the pages, and the panels placed by them, reach the window's top | The panels start under `Border#Titlebar` |
| Floating panels: the sidebar's More button (`···`) stopped opening its menu, and nothing in the top 64 pixels took a click | `Border#Titlebar` lies over the top of the whole window. Apple's own brush lets clicks through; a colour written by a theme, even transparent, catches them | The layout leaves `.titlebar` alone; `docs/themes/names.md` tells creators to do the same |
| Apple Music ended by WinUI (0xc000027b in `Microsoft.UI.Xaml.dll`) when the window narrowed, with the play bar's buttons folding | The fold decided inside `SizeChanged`, during the layout pass: the widths read were not yet those of the button just hidden, so it folded and unfolded in turn until WinUI stopped the layout cycle | Decide after the pass: `SizeChanged` queues one evaluation on the dispatcher (low priority, one at a time), each step lays out again and queues the next, and no fold is decided in the round where the song's margin just changed |
| Centred bar: no progress line under the buttons, and no times | The line's template repeats its margin inside, on all four sides: an 8-pixel bottom margin left the 6-pixel line no height, and the side margins counted twice. The times' visibility is set by `NowPlayingControl`'s `MouseEntered` and `MouseExited` states, whose setters rank above a local value | `centre-bar` gives the line side margins only, half the room on each, and lifts it with the cell's padding. `visibility: visible` is held by a storyboard of Ampify's begun after Apple's state, and begun again when a later state collapses the element (`HoldVisible`); stopping it gives Apple's value back. It is never a local value: that replaced Apple's binding of the slider's thumb, which stayed hidden for good after a `:hover` |
| Centred bar: Apple Music closed at the first hover of the song (stowed 0x800F1000, "animation target not specified", in a `PointerEntered` handler) | `NowPlayingControl`'s states name the times, and find them by name in its template; moved out of it, they had no target | A moved element hands its template's states over to a stand-in of the same class and name, never shown (`HandStatesOver`), so they hide nothing the theme placed elsewhere; moved back, they drive the element again. The times then stay shown as the pointer leaves the song |
| Apple Music ended by WinUI (0xc000027b, very likely a layout cycle, `AG_E_LAYOUT_CYCLE`) about 2 s after the lyrics pane opened over a song with lyrics, under Glass | `floating-panels` gave `Grid#PaneRoot` of `SplitView#ContentSplitView` `width: auto` and `horizontal-alignment: stretch`. The `SplitView` template binds that pane's width to `OpenPaneLength` and sets its column and alignment in its visual states; without them the pane followed its content and drew stretched to the left, while Apple's lyrics size their lines from the pane | The engine keeps a `SplitView`'s pane to its template for every theme (`src/core/theme/pane.cpp`): its alignment and grid column are refused, and `width: auto` becomes a fixed width, the open pane length less the horizontal margins |
| The play bar's "More" button cut at the bar's edge | A bar too narrow for its columns gives the toggles a slot narrower than they are and clips them; their gap to the transport still looked wide | The spare room also counts the toggles' slot (`LayoutInformation::GetLayoutSlot`) less their width |

Names from WinUI templates (`Grid#RootGrid`, `Grid#LayoutRoot`, `Border#Root`, `ContentPresenter#ContentPresenter`) appear
dozens of times. Always target them under a named parent.

## Animations

| Problem | Cause | Fix |
|---|---|---|
| An element with a theme `transform` refused its `@keyframes` rotation; most artworks refused an animated scale | `UIElement.Scale`, `Rotation`, `Translation` and `CenterPoint` cannot be used with `RenderTransform` or `RenderTransformOrigin` on the same element: `StartAnimation` throws. Apple Music moves many artworks itself. `Opacity` is not part of the rule | The animation is refused with a line in the log, and `format.md` tells creators to put the transform in the stops |
| `overflow: hidden` clipped an artwork but was refused on cards ("has no property Clip") | `ElementCompositionPreview.GetElementVisual` throws on an element that uses `Translation`, `Scale` or `Rotation`, which Apple Music animates; and once taken, the hand-out visual makes those properties throw for Apple | Never take an element's hand-out visual: the clip is `UIElement.Clip`, a `RectangleGeometry` resized on `SizeChanged` |
| A finite animation without `forwards` stayed on its last frame | A completed composition animation leaves the property at its final value | It runs with `IterationBehavior::Forever` and `StopBehavior::SetToInitialValue`, and a `DispatcherQueueTimer` stops it when its iterations end |
| Rotation around the top left corner | `CenterPoint` is 0,0 by default, and the element may not be laid out yet when the animation starts | An expression animation on `CenterPoint` reads `this.Target.ActualSize` (documented as an expression input), so the centre follows the size |
| Glass: a sidebar entry jumped 6 pixels on hover, and back, from one entry to the next | `NavigationViewItem` refuses the facade's transitions (Apple Music animates it), so `transition: transform` was refused and each hover change applied at once | The transform eases on Ampify's `CompositeTransform` through a Storyboard, from where it is (`WriteStoryTransform`); `opacity` and `background` stay refused there |
| A button that grows on hover grew towards its bottom right corner | XAML scales around the top left corner, CSS around the centre | A `CompositeTransform` whose element has no origin of the theme's turns around its centre (`CentreOn`) |
| Text glyphs moved by `translate` vanished from the playback bar | The buttons clip their content | Not a bug: a theme should translate the button, not its glyph |
| A colour animation on the play button's `Grid#Root` made the log say "1 value applied" every 3 seconds | The theme also gives that element a `background`; the colour animation draws it, so the engine read back an animated brush that was never its value and wrote again at every pass (the write itself is skipped) | The backend keeps the `background` the theme asked for while the animation owns it, and reads that back (`WantBackground`) |
| A colour animation on the sidebar's root did nothing visible | Its children draw an opaque background over it | Not a bug: pick the element that draws the colour, with `ampify devtools pick` |
| The spinning record drifted in a small circle (about 2.5 px) instead of turning on the spot, and was not quite round | Apple gives the artwork, its placeholder and the grid inside a fixed width of 54; a smaller record left them overflowing, a `Stretch` child wider than its slot sitting against the left edge. And the centre of the turn was an expression on the visual's size, which kept the old size | The snippet sizes those three elements to the record's size. `StepLoops` sets `CenterPoint` from `ActualSize` at each step for an element a stepped animation turns, and drops the expression (measured: the hole of the record stayed on the same pixel over twelve frames) |

## Fonts and icons

| Problem | Cause | Fix |
|---|---|---|
| Icons as empty squares | A `TextBlock { font-family }` rule changed the font of glyphs (private-use characters) | The engine never changes the font of a text made of icons |
| "Primary" buttons as symbols | Apple Music's `AccentButtonStyle` draws the text in a symbol font | Use Apple Music's button style and set accent colors on top |
| Part of an SVG icon missing or misplaced | XAML path markup does not go back to the start of a closed subpath: a relative `m` after `z` moves from the last point, not from the start as in SVG | Start each subpath after `z` with an absolute `M` (the kit's icons do) |
| An extension's active play bar button stayed white | A glyph icon (`FontIcon`) takes the button's foreground and ignores the brush key given to `MakeIconElement`; only SVG icons follow it | `TintControl` sets the glyph's own `Foreground` to the accent brush, and leaves it unset when the button is off |
| An extension's button did not go back to white when switched off | `SurfaceTracker` compares a control's signature, which left out `active`: the change was never seen | Every field the backend draws belongs in `Signature` (`src/core/ui_render/surfaces.cpp`) |

## Windows

| Problem | Cause | Fix |
|---|---|---|
| Apple's full-screen player lost its resize border a moment after Ampify set `WS_THICKFRAME` | The FullScreen presenter of its AppWindow puts its own Win32 style back after any change | Give the window an `OverlappedPresenter` with `SetBorderAndTitleBar(true, false)`, on the window's interface thread |
| Above the taskbar, Apple's player had a 3 px light line around the screen | Its window was made with `WS_DLGFRAME`, which `SetBorderAndTitleBar(false, false)` leaves, and the AppWindow puts the bit back after `SetWindowLongPtr` removes it | Place the window so its client area fills the work area, and cut the frame off with `SetWindowRgn` (removed again for the window modes) |
| Apple's shrink button left only the mini player in a corner when Ampify placed the player | `ExitFullScreenButton` turns the player into the mini player, and the main window stays hidden behind it | Collapse that button while Ampify places the player; its close button (`CloseFullScreenButton`) and Escape bring Apple Music back |
| The mini player's title bar stayed white under every theme, and at 125% its cover and buttons were cut at the bottom | The system draws the title bar of a window Ampify makes, outside the XAML tree; and `MoveAndResize` sets the outer size, so the content laid out for the whole window lost the title bar's height | The title bar's colours (`AppWindowTitleBar`, opaque hover and pressed shades) come from the `--mini-*` colours; the window is sized with `ResizeClient` and kept on screen with its frame (`AppWindow.Size` less `ClientSize`) |
| Which new window is Apple's player? | Enumerating the process's windows also finds dialogs and Ampify's own windows | Ask Apple Music by name: `GetAppProperty("FullScreenPlayerWindow")`, like `MainWindow` |

## Build and capture

| Problem | Cause | Fix |
|---|---|---|
| MSVC build failed on a PC | `build-msvc.ps1` took the first `cppwinrt.exe` (SDK 17763, C++/WinRT 1.0) | The newest SDK that has an x64 `cppwinrt.exe` |
| XAML link error | `RoGetActivationFactory` missing | Link `runtimeobject` |
| CI build: `'GetClassNameA': is not a member of ... IAutomationPeer` | `windows.h` came between two WinRT headers: its `GetClassName` macro renamed the method in one and not the other (the CI builds without `UNICODE`) | Include every WinRT header of the file before `windows.h` |
| Screenshot cut off on the right | PowerShell not DPI aware, screen at 125% | `SetProcessDpiAwarenessContext(-4)` before capturing |
| Catppuccin Latte looked the same as Apple Music's light mode | Colors close to Apple Music's own show no difference | Compare side by side, and change shapes too |

## Apple Music's quirks

Finding the window and the UI thread is described in [how-it-works.md](how-it-works.md#the-window-and-the-ui-thread).
A `WH_GETMESSAGE` hook never fired on Windows Server 2025; the DispatcherQueue works.

- Apple Music keeps the first 64 pixels of a page for its playback bar, in more places than the list's padding: the
  page's backdrop (`ContentPresenter#PageBackground`, an artist's video) is laid 64 down, and the lyrics and queue pane
  has a 64-high first row. A layout that moves the panels below the bar must take those 64 back too, or a dark band
  shows. Look for a fixed 64 (`Height=64`, a margin of 64) when something sits lower than its panel's top.
- Apple paints some surfaces in code with a fixed colour, out of any key: the search's top-result tiles (`#FF282828`),
  an album's artist (its blue). Only a rule on the element reaches them (Ampify derives one from the theme's colours).
- The progress bar (`AMP.UI.Controls.ScrubberControl`) is a Slider whose template reads keys of Apple's own, at rest
  too: `ScrubberDecreaseRectFill` / `PointerOver` / `PressedFill` / `Disabled` (played part), `ScrubberTrackRectFill` /
  `PointerOver` / `Pressed` / `Disabled` (the rest), `ScrubberThumbNormal` / `PointerOver` / `Pressed` / `Disabled`.
  The mini player defines them, and the Slider keys, in its own dictionary over the application's, so neither the
  control's `Foreground` nor a theme's keys reached the bar. `HoldThroughStates` writes them in the bar's own resources
  (found in `resources.pri`; checked on 03/11/2026: Glass 4FD1FF, Catppuccin CBA6F7 at rest, hovered and pressed, where
  Apple's white stayed before). The volume slider reads WinUI's Slider keys and followed the accent already.
- WinUI resolves `ToggleButtonBackgroundChecked`, `AccentButtonBackground` and `AccentButtonForeground` from the accent
  once, when its dictionaries load, like the other accent keys: a theme's accent alone left the checked lyrics and queue
  buttons in Windows' blue and white text on a light accent. They are derived from the accent with the rest.
- Apple's lists (`AMP.UI.Controls.ListView`, the Songs table, album track lists) give their rows' selection colours
  in their own style: the `ListViewItemBackgroundSelected*` keys of a theme never reach them. The row's
  `ListViewItemPresenter` draws the selection from its own `SelectedBackground`, `SelectedPointerOverBackground` and
  `SelectedPressedBackground`, which take a local value and keep it (`.list-item { selected-background: ... }`).
  Checked on 02/11/2026: Catppuccin's purple veil was Apple's grey before, the measured colour after.
- Apple's main window closes through `WM_CLOSE` (title bar button, Alt+F4, the taskbar), which WinUI turns into
  `AppWindow.Closing`. A subclass of the main window that answers `WM_CLOSE` itself keeps Apple Music from starting
  to shut down; `ShowWindow(SW_HIDE)` then hides it and the music goes on (`src/win/close_to_tray.cpp`).
- Scrolling the Home page moves in steps with or without Ampify: each wheel notch starts fast and slows down, and after a
  few minutes of browsing the interface thread stalls for about 240 ms at a time while new shelves load (measured on
  02/11/2026, the same in Apple Music alone). Ampify's passes after new rows add 1 to 4 seconds a minute of that thread
  while browsing, logged as "passes after changes in the last minute" when above 200 ms.
- XAML Diagnostics (`xaml.watcher`, `src/win/xaml_watcher.cpp`) works in 1.1792 and made no screen flash in 3,000 frames
  of browsing and scrolling. Apple Music does not expose the hooks that release the handles it gives, so it keeps a
  record of every element added (130,000 in five minutes): browsing rounds took 87 s instead of 55 s by the third, and UI
  Automation calls stalled up to 2.8 s. Off by default for that reason; frames, lists and popups already ask for a pass.
- Menu entries of extensions are added after a real pointer release or key press on the window. A UI Automation Invoke
  on the button does not count: to test them without the mouse, send Enter to the "Actions de la barre latérale"
  button (`MoreButton`) with a posted key, which opens the main menu; a song's menu needs a real right click.
- Apple's volume slider is in decibels and is the model: setting `IVolumeControl.Level` (the slider position, 0 to 1) on
  the main window's `VolumeControl` makes Apple's own binding set the player volume and redraw the slider and glyph. The
  control's DataContext is empty (no `IVolumeViewModel` to reach), and the ABI is written by hand in
  `src/win/player_controls.cpp`. `src/win/win_host.cpp` is compiled without `AMPIFY_WITH_XAML`: code that needs XAML
  there is reached through a function pointer installed from `xaml_host.cpp`, or it silently compiles to nothing.
- The row a context menu is about: the data context of the row (a `DataGridRow` for a right click on the row, a
  `TrackLockup` for its "..." button; both IContentDescriptorDriven and ITitledComponent).
  `IContentDescriptorDriven.get_ContentDescriptor` gives the descriptor with the catalog id (`AdamId`), Apple's
  `CanonicalId` and the kind flags (`src/win/amp_item.cpp`). A song of the user's library had neither id, only the
  "local library identifiers" flag; its `ContainerContentDescriptor` is the playlist (IsPlaylist, IsContainer). The
  page the row sits in is driven by a descriptor too (a playlist), so the walk up the tree accepts only a data context
  titled like the menu ("Play «Name»"): the first try reported every song as a playlist.
- Lyrics: a song timed by line draws each line in a `TextBlock#LineContainer`, one timed by word in a
  `Border#WordByWordHost`; both sit in the line's `Grid#ContentGrid`, which Apple fades by state (`NotCurrent` 0.5,
  `Current` 1). The tree only holds the lines Apple Music has realized (53 in one inspector snapshot).
- First start: an MPEG LA licence (buttons Agree, Decline, Save As) shows before the main window, then, after sign-in, a
  "Welcome to Apple Music" screen (Start Listening). No XAML window exists while they are open. The bench closes them
  through UI Automation.

See also [../../knowledge/apple-music-xaml-tree.md](../../knowledge/apple-music-xaml-tree.md) and
[../../knowledge/amexclusive-notes.md](../../knowledge/amexclusive-notes.md).
