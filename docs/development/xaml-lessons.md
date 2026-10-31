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
| Apple Music crashed (0xc000027b) at the theme change after an inspector pass | Enumerating the application's dictionaries realizes deferred resources, and the next refresh fails on them | No enumeration: the inspector asks `HasKey` for a fixed list of Fluent colour keys (`core/theme/colour_keys.cpp`) |
| A button lost the theme's colours under the pointer and while pressed (Greenroom's play button turned black) | The button's visual states set Fluent resources, which win over local values | The colours are also written into the control's own resources under `{Type}{Property}PointerOver` and `Pressed` keys |
| The window froze for about half a second every 3 seconds with Greenroom | Each write of the play button's background put new state keys, which asked for a refresh of the whole window's resources (400 to 800 ms on the interface thread); the refresh reset that background, which the next pass wrote again | State keys are written only when their colour changes. The log gives the duration and the cause of every refresh |

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
| A text size on every `TextBlock` overwrote page titles | Too broad a selector | Target under a named parent |

Names from WinUI templates (`Grid#RootGrid`, `Grid#LayoutRoot`, `Border#Root`, `ContentPresenter#ContentPresenter`) appear
dozens of times. Always target them under a named parent.

## Fonts and icons

| Problem | Cause | Fix |
|---|---|---|
| Icons as empty squares | A `TextBlock { font-family }` rule changed the font of glyphs (private-use characters) | The engine never changes the font of a text made of icons |
| "Primary" buttons as symbols | Apple Music's `AccentButtonStyle` draws the text in a symbol font | Use Apple Music's button style and set accent colors on top |

## Build and capture

| Problem | Cause | Fix |
|---|---|---|
| MSVC build failed on a PC | `build-msvc.ps1` took the first `cppwinrt.exe` (SDK 17763, C++/WinRT 1.0) | The newest SDK that has an x64 `cppwinrt.exe` |
| XAML link error | `RoGetActivationFactory` missing | Link `runtimeobject` |
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
