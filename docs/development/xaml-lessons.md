# XAML lessons

What went wrong and what fixed it while Ampify learned to draw inside Apple Music. Read this before touching `win/`.

General rules learned:

1. **In a process you do not control, hold everything you create with a strong reference.** Release it on the UI thread.
2. **Never trust the log without looking at a screenshot.** Several times the log said "mounted" or "applied" while
   nothing was visible.
3. **Compare before and after side by side.** Translating colors that are close to Apple Music's own shows nothing. A
   real theme also changes shapes.

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
| Large text rewrote every text size every 3 seconds | WinUI keeps sizes in single precision: 16.8 reads back as 16.799999237060547, and the text comparison saw a new value on each pass | Numbers are compared as WinUI keeps them (`SameValue`) |
| The "Open the Marketplace" button of the Settings card showed symbols instead of its text | Apple Music's `AccentButtonStyle` sets its `FontFamily` to WebDings, and its template draws the label with it | The plain button style of the page with Ampify's accent template (`PrimaryButtonTemplate`), as kit buttons do |
| Greenroom reported a refused `Foreground` on `FontIcon#SubItemChevron` | The hidden chevron of an `AppBarButton`'s template is listed by the tree walk but cannot be reached afterwards (never realized) | A value for an element gone since the snapshot is skipped, not refused |
| After a live switch of theme, the Marketplace page kept the accent of the theme before | Ampify's pages and panels are popups without a parent: switching the mode of the window's content does not reach them | After the window, each open popup reads its theme again alone (`ReadPopupsAgain`) |
| Extensions had no way into Apple Music's menus | Apple Music builds them from its own lists (`ExtensibleMenuFlyout`), and nothing tells when one opens | After a click or a key, the open `MenuFlyoutPresenter` is looked for: its `ItemsSource` is the menu's own items, where Ampify's entries go while it is open (with the style of Apple Music's entries) and leave when its popup closes. A menu is an item's when one entry quotes a name ("Play «Name»"); the main menu is the one opened from `Grid#PaneHeaderGrid` |
| The album artist stayed Apple blue under every theme, although the inspector showed the theme's colour on its `TextBlock` | The name is a `Hyperlink` inside the `TextBlock`, its runs keep their own colour, and Apple Music fills the link after the theme's first pass | A text colour also goes to the inlines of a text that holds a link, and is read back from its last run, so a later pass sees the difference and writes again |
| A theme without colours (Hide chrome) turned a dark Apple Music light | The app mode was set from the scheme name, light when there was none | The mode follows the theme's `dark` flag, then its scheme name, and is left alone when neither says anything (`AppModeOf`) |
| Glass: a button under the pointer showed Apple Music's red under black text | The theme gave `AccentFillColorDefaultBrush` only; the pointer-over and pressed states use `AccentFillColorSecondaryBrush` and `...TertiaryBrush` | Both are worked out from the accent at 90% and 80%, as Fluent does, unless the theme names them |
| Settings pages stayed grey (#1C1C1C) and their toggles and sliders Apple blue under every theme | Each settings page is a class of its own (`PlaybackSettingsPage`...) whose background Apple sets in code; WinUI's `ToggleSwitchFillOn`, `SliderTrackValueFill`... are set from the accent once, when its dictionaries load | The settings pages are `.page` too, and the control keys are worked out from the theme's accent (`DefineAccentControls`) |

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
| Floating panels: the back arrow sat across the sidebar panel's rounded corner, half on the window | Apple pins `Button#NavigationViewBackButton` to the window's corner (margin 4,0,0,0); the panel starts one gap in | `floating-panels` moves `.back-button` one gap down and in, on the line of the app's name |
| Bottom bar: the song's artwork flush against the window's left and bottom edges, its placeholder stretched to the bar's height | The display filled its column from the window's edge | `bottom-bar` gives `.now-playing` a gap on the left and 4 above and below: the 54-pixel artwork is square again |
| Apple's logo over the song's title after leaving a theme that hid it | Apple binds `Image#Apple`'s visibility to what plays, a local value: the engine recorded "Visible" before the theme and wrote it back when the theme went | Hide it by `opacity: 0`, which Apple leaves alone, so the engine clears it instead of restoring a stale value |
| Extension panels stayed in Fluent's grey under every theme | Their frame reads `AcrylicInAppFillColorDefaultBrush`, which no theme names | The engine derives the acrylic fills from the theme's card colour when it is opaque |
| Bottom bar: the extension panels covered the window's buttons | With the play bar moved down, the pages, and the panels placed by them, reach the window's top | The panels start under `Border#Titlebar` |
| Floating panels: the sidebar's More button (`···`) stopped opening its menu, and nothing in the top 64 pixels took a click | `Border#Titlebar` lies over the top of the whole window. Apple's own brush lets clicks through; a colour written by a theme, even transparent, catches them | The layout leaves `.titlebar` alone; `docs/themes/names.md` tells creators to do the same |

Names from WinUI templates (`Grid#RootGrid`, `Grid#LayoutRoot`, `Border#Root`, `ContentPresenter#ContentPresenter`) appear
dozens of times. Always target them under a named parent.

## Animations

| Problem | Cause | Fix |
|---|---|---|
| An element with a theme `transform` refused its `@keyframes` rotation; most artworks refused an animated scale | `UIElement.Scale`, `Rotation`, `Translation` and `CenterPoint` cannot be used with `RenderTransform` or `RenderTransformOrigin` on the same element: `StartAnimation` throws. Apple Music moves many artworks itself. `Opacity` is not part of the rule | The animation is refused with a line in the log, and `format.md` tells creators to put the transform in the stops |
| A finite animation without `forwards` stayed on its last frame | A completed composition animation leaves the property at its final value | It runs with `IterationBehavior::Forever` and `StopBehavior::SetToInitialValue`, and a `DispatcherQueueTimer` stops it when its iterations end |
| Rotation around the top left corner | `CenterPoint` is 0,0 by default, and the element may not be laid out yet when the animation starts | An expression animation on `CenterPoint` reads `this.Target.ActualSize` (documented as an expression input), so the centre follows the size |
| Text glyphs moved by `translate` vanished from the playback bar | The buttons clip their content | Not a bug: a theme should translate the button, not its glyph |

## Fonts and icons

| Problem | Cause | Fix |
|---|---|---|
| Icons as empty squares | A `TextBlock { font-family }` rule changed the font of glyphs (private-use characters) | The engine never changes the font of a text made of icons |
| "Primary" buttons as symbols | Apple Music's `AccentButtonStyle` draws the text in a symbol font | Use Apple Music's button style and set accent colors on top |

## Windows

| Problem | Cause | Fix |
|---|---|---|
| Apple's full-screen player lost its resize border a moment after Ampify set `WS_THICKFRAME` | The FullScreen presenter of its AppWindow puts its own Win32 style back after any change | Give the window an `OverlappedPresenter` with `SetBorderAndTitleBar(true, false)`, on the window's interface thread |
| Which new window is Apple's player? | Enumerating the process's windows also finds dialogs and Ampify's own windows | Ask Apple Music by name: `GetAppProperty("FullScreenPlayerWindow")`, like `MainWindow` |

## Build and capture

| Problem | Cause | Fix |
|---|---|---|
| MSVC build failed on a PC | `build-msvc.ps1` took the first `cppwinrt.exe` (SDK 17763, C++/WinRT 1.0) | The newest SDK that has an x64 `cppwinrt.exe` |
| XAML link error | `RoGetActivationFactory` missing | Link `runtimeobject` |
| CI build: `'GetClassNameA': is not a member of ... IAutomationPeer` | `windows.h` came between two WinRT headers: its `GetClassName` macro renamed the method in one and not the other (the CI builds without `UNICODE`) | Include every WinRT header of the file before `windows.h` |
| Screenshot cut off on the right | PowerShell not DPI aware, screen at 125% | `SetProcessDpiAwarenessContext(-4)` before capturing |
| Catppuccin Latte looked the same as Apple Music's light mode | Colors close to Apple Music's own show no difference | Compare side by side, and change shapes too |

## Finding the window and the UI thread

- `Window::Current()` returns null in a desktop WinUI 3 app. Get the window from the `AMP.Utils.dll` export
  `?GetAppProperty@SharedUtils@@YA?AUIInspectable@Foundation@Windows@winrt@@AEBUhstring@5@@Z` with `"MainWindow"` or
  `"MiniPlayerWindow"`, then `try_as<Window>()`. Before the window exists the call fails with `0x8000000B`: retry.
- Use `window.DispatcherQueue().TryEnqueue(...)`. A `WH_GETMESSAGE` hook never fired on Windows Server 2025.
- Recognize pages by class name (`PlaybackSettingsPage`), not by position.
- First start: an MPEG LA licence (buttons Agree, Decline, Save As) shows before the main window, then, after sign-in, a
  "Welcome to Apple Music" screen (Start Listening). No XAML window exists while they are open. The bench closes them
  through UI Automation.

See also [../../knowledge/apple-music-xaml-tree.md](../../knowledge/apple-music-xaml-tree.md) and
[../../knowledge/amexclusive-notes.md](../../knowledge/amexclusive-notes.md).
