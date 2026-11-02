# Ampify and Spicetify

Ampify aims to offer what Spicetify offers for Spotify: themes, extensions, custom apps, a marketplace and a command
line. This page compares them feature by feature and says what is done and what is not.

Sources for Spicetify: [commands](https://spicetify.app/docs/cli/commands),
[configuration](https://spicetify.app/docs/customization/config-file),
[themes](https://spicetify.app/docs/advanced-usage/themes),
[extensions](https://spicetify.app/docs/development/extensions),
[custom apps](https://spicetify.app/docs/development/custom-apps) and the [repository](https://github.com/spicetify/cli).

## The main difference

Spotify on PC is a web page. Spicetify rewrites the app's files and injects CSS and JavaScript into the page. Apple Music
for Windows is a native WinUI 3 app. There is no HTML, no CSS and no web bundle to patch. Ampify does not write into
Apple Music's package. It injects its kernel at start and acts inside the process. Five mechanisms cover almost
everything:

1. **Injection and kernel.** The package is started suspended, then the kernel loads into `AppleMusic.exe` and
   `AMPLibraryAgent.exe`.
2. **A JavaScript engine in the kernel.** QuickJS, one instance per element, global object `Ampify`.
3. **The XAML tree in the process.** The kernel shares the UI process. It reads the app's resources, walks the visual
   tree from the window, adds elements and listens to events. All changes go through the UI thread.
4. **C++/WinRT projections of Apple's winmd files.** The metadata of Apple Music's own components is in the package
   (`knowledge/winmd/`), so Ampify can call the app's internal services by name, as the app does itself.
5. **Sound after decoding.** The kernel hooks the audio output (WASAPI and Windows spatial audio) to read and change
   samples. The player volume is never written.

For playback state and commands, Ampify also uses Windows' public SMTC API.

The cost: CSS selectors do not exist. Elements are targeted by XAML type and name, and names can change with an Apple
Music update. The inspector (`ampify devtools`) lists the real names for your version.

## Status legend

| Status | Meaning |
|---|---|
| Done | Works. It may still wait for a check on a real PC, as noted |
| Partial | Works with a limit written in the row |
| Outside 1.0 | Not in version 1.0, for the reason in the row ([extensions/api.md](extensions/api.md), section 11) |
| Different | Done another way, or no equivalent exists |

## 1. Installation and command line

| Spicetify | Ampify | Status |
|---|---|---|
| Installer, finds Spotify | `ampify_setup.exe` finds the Apple Music package and copies four files. It does not copy the app | Done |
| `backup` and `apply` (patch the files) | `ampify launch` starts the app with the kernel injected. No package file is written. `ampify backup` saves settings and elements | Different |
| `restore` | `ampify restore` starts the app with no kernel. Nothing needs putting back. To uninstall, use Settings > Apps | Done |
| `auto` | `ampify launch` | Done |
| `config` (read, write) | `ampify config [key] [value]` on `settings.txt`, value checked | Done |
| `config <list> <item>` and `<item>-` | `ampify extension enable <id>` and `disable <id>`. `ampify config extensions +id` and `-id` also work | Done |
| `apply --extension <file>` | `ampify extension enable <id>` loads just that element, with no restart of the app | Done |
| `watch` | `ampify watch` and `ampify dev <folder>` reload an element or a theme at each save | Done |
| `refresh`, `watch` (reload what changed) | Extensions reload by themselves (stop, then start). Themes remove and apply again. No Apple Music restart | Done |
| `update` (Spicetify updates itself, then applies again) | `ampify update` | Done |
| `enable-devtools` | `ampify devtools` writes the XAML tree to `logs\inspector.txt` and `inspector.json`. There is no WebView2 debug port | Partial |
| `upgrade` | `ampify update` reads the latest GitHub release, checks the published SHA-256 and asks for confirmation | Done |
| `path`, `config-dir` | `ampify path [name]` and `ampify config --dir` | Done |
| Restore after a broken update | The kernel turns itself off on an unknown Apple Music version. `ampify versions update` fetches the list of tested versions | Done |
| `--help`, `--version`, chained verbs (`backup apply enable-devtools`) | `ampify` accepts several verbs on a line, for example `ampify backup apply launch` | Done |
| Legacy versions | `ampify install-kit <version>` installs a past release, SHA-256 checked | Done |

## 2. Configuration keys

| Spicetify key | Ampify | Status |
|---|---|---|
| `spotify_path` | None: Apple Music is a Store package that Windows finds by its identity, so there is no path to set. `ampify path app` prints where it is | Different |
| `prefs_path` | None: `ampify path prefs` prints the usual place of the preferences file | Different |
| `spotify_launch_flags` | `app_launch_flags`, arguments added when Apple Music starts (Apple Music may ignore them) | Done |
| `check_spicetify_update` | `check_kit_update`, a check at `ampify` start, never an automatic download | Done |
| `disable_sentry`, `disable_ui_logging` | None: no setting exists | Outside 1.0 |
| `remove_rtl_rule` | Not needed: there are no right-to-left CSS rules to remove. A theme can set `flow-direction` | Different |
| `expose_apis` | `expose_api`. At `0`, no JavaScript element starts | Done |
| `current_theme` | `theme.active` | Done |
| `color_scheme` | `theme.scheme`, which also accepts `auto`, plus `theme.schedule` | Done |
| `extensions`, `custom_apps` (lists) | One key per element: `extensions.<id>.enabled`, `apps.<id>.enabled` | Different |
| `inject_css` | `theme.parts.resources`: the rules become overrides of XAML values | Done |
| `inject_theme_js` | `theme.parts.script`: the theme's `main.js` runs or not | Done |
| `replace_colors` | `theme.parts.colors` | Done |
| `overwrite_assets` | `theme.parts.assets`: images and fonts of the theme folder | Done |
| `sidebar_config`, `home_config` | `sidebar.json` and `home.json` (hide and reorder). The real element names still have to be read from the app | Partial |
| `xpui.js_find_*`, `xpui.js_repl_*` (patches) | No equivalent. There is no JavaScript bundle to patch. A table of byte patterns per tested version is a last-resort idea, not built | Different |

## 3. Extensions (JavaScript API)

| Spicetify | Ampify | Status |
|---|---|---|
| A single JavaScript file | `main.js` in a folder with `manifest.json`, loaded by the kernel. Several files through ES modules | Done |
| Creator (TypeScript, JSX, bundling) | `ampify new`, `dev`, `test`, `build` and `types` (`ampify.d.ts`). There is no TypeScript or JSX compiler | Partial |
| Wait for the APIs (`setTimeout` loop) | `Ampify.ready`, a promise resolved when the element has started | Done |
| `Player`: state, track, events | `Ampify.player.state()` and `on(...)`. Read through Apple's player (`AMP.Services.Player`) in `AppleMusic.exe`, activated from Apple's own DLL in 1.1792; SMTC elsewhere | Done |
| `Player`: play, pause, next, previous, seek | SMTC commands, then media keys. `seek` goes through SMTC only | Partial |
| `Player`: volume | `Ampify.audio.setGainDb`: an internal gain on samples. The slider is never written, so there is no `setVolume` | Different |
| `Platform.History` (navigation) | `Ampify.ui.navigate(path)` for app pages. No navigation to Apple Music's own pages | Partial |
| `Platform.Library`, `Platform.Playlists` | Apple Music reads its library through its library agent, with fetch requests in a private format. The permissions `library.read` and `library.write` are reserved | Outside 1.0 |
| `CosmosAsync` | An allow-list of read calls to `AMP.Services` (`services.read`, reserved). It first needs a layer that calls those methods by name with checked argument types | Outside 1.0 |
| `GraphQL` | No equivalent. Apple exposes none | Different |
| `URI` | `Ampify.uri` parses and builds `music:`, `musics:`, `itms:` and `https://music.apple.com` links | Done |
| `React`, `ReactDOM` | `Ampify.React` and `Ampify.ReactDOM`: a small engine with React's shape (hooks, fragments, props) that renders to native controls | Done |
| `ReactComponent` | `Ampify.components`: Button, Toggle, Slider, TextField, Dropdown, Card, List, Text, Tooltip. Spotify's internal components are not reusable | Done |
| `ReactHook` | `Ampify.hooks`: the React hooks plus `usePlayer()`. No `useTheme()` yet | Partial |
| `TippyProps` | `tooltip`, `tooltipPlacement` and `tooltipDelay` on every primitive and component | Done |
| `Topbar` | `Ampify.ui.topbar.addButton`. The button goes in the playback bar's toggle button row, since Apple Music has no separate top bar | Done |
| `Playbar` | `Ampify.ui.playbar.addButton` | Done |
| `ContextMenu`, `Menu` | `Ampify.ui.menu.addItem`: entries in Apple Music's main menu (`"app"`) and in the menu of a song, an album or a playlist (`"context"`), whose `onClick` receives the item's name, from a click or from the keyboard (menu key, arrows, Enter). Spicetify gives the items' URIs, a `shouldAdd` filter and submenus; Apple Music's menus give no identifier for their item | Partial |
| `PopupModal` | `Ampify.ui.popup.open`, a native dialog | Done |
| `Panel` (documented by Spicetify, absent from its code) | `Ampify.ui.panel.register`, a 380 px column on the right | Done |
| `AppTitle` | `Ampify.ui.title.set`: the title of the element's panel, page and popup (checked on an app page). Apple Music's window title stays its own | Partial |
| `Keyboard` | `Ampify.keyboard.bind` (global shortcuts through `RegisterHotKey`) | Done |
| `LocalStorage` | `Ampify.storage`, one JSON file per element | Done |
| `Config` | `Ampify.config.get` and `set`, limited to the element's keys | Done |
| Settings pages that each extension builds (or `spcr-settings`) | `settings` in the manifest: toggles, choices, texts and numbers shown in the Ampify card of Apple Music's settings, with no code ([extensions/manifest.md](extensions/manifest.md)) | Better |
| `Queue` | `Ampify.player.queue.list()`: Apple's queue section by section (history, current, added, next, autoplay), with each song's catalog id. Apple Music builds it the first time its "Up Next" panel opens in a session; before that, the current track only, with `complete: false` and the reason | Partial |
| `removeFromQueue` | `Ampify.player.queue.remove(id)` removes an entry still to come; it leaves Apple's panel at once | Done |
| `addToQueue` | `queue.add` answers `false`: Apple's player ignores the songs Ampify hands it ([knowledge/apple-music-queue.md](../knowledge/apple-music-queue.md) says what was tried) | Outside 1.0 |
| `colorExtractor` | `Ampify.color.extract(pixels, width, height)` on pixels the script holds, and `Ampify.color.fromArtwork(url)` on an artwork of Apple Music, such as `track.artwork` | Done |
| `getAudioData` | `Ampify.audio.analyze()`, `onFrame` and `loudness()`: level, spectrum, tempo, LUFS, computed in memory | Done |
| `showNotification` | `Ampify.notify({ title, text })`, a Windows notification | Done |
| `getFontStyle` (documented by Spicetify, absent from its code) | `Ampify.fonts.style(name)` and `Ampify.fonts.roles()`: the font roles of the applied theme, the Windows 11 ramp for what it does not set | Done |
| `SVGIcons` | `Ampify.icons.get(name)` | Done |
| `Locale` | `Ampify.locale` and `Ampify.i18n` | Done |

## 4. Themes

| Spicetify | Ampify | Status |
|---|---|---|
| `color.ini` (sections, named colors) | `color.ini` or `colors.ini`, read as it is and translated to Apple Music elements and Fluent brushes ([themes/spicetify.md](themes/spicetify.md)) | Done |
| `user.css` (free rules) | `user.css`: a CSS subset with XAML type and `x:Name` selectors, `:hover`, `:active`, `:focus`, `:root` variables, gradients, acrylic, transforms, shadows, transitions, `@keyframes` animations, grid placement ([themes/format.md](themes/format.md)). Spotify's own `user.css` cannot apply, since the HTML classes it targets do not exist | Different |
| `theme.js` | The theme's `main.js`, run as an extension while the theme is active | Done |
| Color schemes (`[Base]`, `[Dark]`) | Sections of `colors.ini`, `theme.scheme`, `auto` (follows Windows) and `theme.schedule` (follows the time) | Done |
| Theme parts | `theme.parts.*` | Done |
| Images and fonts | Files in the theme folder, `url(...)` in `background` and `font-family` | Done |
| Import a Spicetify theme | `ampify theme import <folder\|zip\|address>`. Colors carry over, layout rules do not | Partial |
| Official theme repository | The catalog in this repository | Partial |
| Reload a changed theme | `ampify dev`, `ampify watch`, `theme.live_reload` | Done |
| Blurred artwork background | `background: artwork` under acrylic panels (the `artwork` theme) | Done |
| Context menus, tooltips, the volume bar (`.main-contextMenu-menu`, `.main-popupMenu-*`, `.volume-bar`) | `.menu`, `.menu-item`, `.tooltip`, `.flyout`, `.volume-slider`: the popups take the theme the first time they open, and the controls keep its colours under the pointer and while pressed ([themes/names.md](themes/names.md)) | Done |

## 5. Custom apps

| Spicetify | Ampify | Status |
|---|---|---|
| `CustomApps/<name>/` | `apps/<id>/` with `manifest.json` (`kind: app`), `main.js` and an `.svg` icon | Done |
| `render()` in React | A global `render()` in an app, or `Ampify.ReactDOM.render` to `page:/<path>` | Done |
| Sidebar entry, normal and active icon | The app adds its entry with `Ampify.ui.sidebar.addItem` and a Segoe Fluent Icons glyph, as the app starter does. The manifest's `icon` does not make the entry by itself, and there is no active-state icon | Partial |
| Routing and sub-pages | `Ampify.ui.navigate(path)` and the paths the app registers | Partial |
| Page in HTML | No. Ampify renders to native controls and uses no WebView | Different |
| `subfiles_extension` | An app and an extension with the same id are linked and talk through `Ampify.link` | Done |
| `subfiles` (several files) | ES modules with relative `import` inside the element's folder | Done |

## 6. Snippets and marketplace

| Spicetify | Ampify | Status |
|---|---|---|
| Snippets (small CSS you switch on) | `kind: snippet`: a `resources.xaml` and an optional `user.css`, `ampify snippet enable <id>` | Done |
| Marketplace tab in the app | The Marketplace app: a page inside Apple Music that lists the catalog and installs, updates, removes, and picks themes | Done |
| Install and update | `ampify install <id>` and `ampify update <id>`: SHA-256 checked, permissions shown before activation | Done |
| Public catalog | [ampify-catalog](https://github.com/Shirowwww/ampify-catalog), schema, validator and human review ([extensions/catalog.md](extensions/catalog.md)). The catalog has few entries so far | Partial |

## 7. Developer tools

| Spicetify | Ampify | Status |
|---|---|---|
| Developer console | `ampify logs --follow` shows the kernel log and `Ampify.log` | Done |
| React DevTools | `ampify inspect` prints the XAML tree written by the inspector. There is no live tree | Partial |
| Creator (project, tests, bundling) | `ampify new <kind>`, `ampify build` (validation, zip, SHA-256) and `ampify test` | Partial |
| Hot reload while developing | `ampify dev <folder>` | Done |
| Element tests | `ampify test` runs `main.js` in QuickJS with a fake host. It needs no Apple Music | Done |
| Editor types | `ampify types` writes `ampify.d.ts` | Done |

## 8. Features with no direct equivalent

### Player volume (`Player.setVolume`)

Any change of gain is internal. It never shows on Apple Music's volume bar.

- The gain goes through `Ampify.audio.setGainDb`, applied to the decoded samples. It is relative: it adds to the volume
  the user chose.
- The kernel reads the slider position (`Ampify.audio.volume()`) and never writes it. `AVCFPlayerSetVolume` is never
  called.
- A "fake volume" would show on the slider, so it is excluded.

Status: relative gain done. An absolute `setVolume` that follows the slider is not built.

### `CosmosAsync` (Spotify's internal API)

The path is `Ampify.services.call(...)`, which calls the services the app already uses, through the `AMP.Services`
projections. Calls are read-only, on an allow-list of methods published with Ampify. It opens no new connection to Apple.
Apple's public MusicKit API needs a developer token (an Apple Developer account): an option for later. Status: outside
1.0, since the layer that calls `AMP.Services` methods by name with checked argument types does not exist yet.

### `GraphQL`

Apple exposes no known equivalent. Data goes through the services above. There is no GraphQL translation.

### `[Patch]` section (patches of the JavaScript bundle)

There is no bundle. The package contains V8, JavaScriptCoreV8 and the Jet engine for store pages. Whether any JavaScript
there can be changed at run time has not been studied. The first stage, overriding XAML resources at run time, covers
the common case (themes). A last-resort table of byte patterns per tested version, never a fixed address, would follow the
rule "resolve by name".

### Telemetry preprocessor (`disable_sentry`, `disable_ui_logging`)

No setting exists. The kernel would answer locally the requests sent to the telemetry hosts found by traffic capture.
License and DRM hosts would never be on that list. Status: outside 1.0.

## 9. Beyond Spicetify

| Feature | Ampify | Status |
|---|---|---|
| Atmos leveler | Built-in extension: a gain on the samples that brings Atmos to the stereo level. Passes the automated tests, with a listening check on a PC pending | Done |
| Equalizer | `Ampify.audio.eq`: 10 bands, presets, applied to the samples in native code | Done |
| LUFS meter | `Ampify.audio.loudness()`. The standalone tool is in `reference/loudness-meter/` | Partial |
| Lyrics | `Ampify.player.lyrics()` and `Ampify.lyrics.current()`: Apple Music's own lyrics, synced by line and by word when Apple times them, with a `lyricschange` event | Done |
| Discord Rich Presence | `Ampify.discord`, through the local pipe, permission `ipc.discord` | Done |
| Last.fm and ListenBrainz scrobbling | `Ampify.http` to the declared hosts. Example: `examples/scrobbler` | Done |
| Media keys and global shortcuts | SMTC and `RegisterHotKey` | Done |
| Always-on-top mini player | Apple Music has its own, with a full-screen player and karaoke lyrics ([knowledge/apple-music-native-views.md](../knowledge/apple-music-native-views.md)). Themes do not reach its window yet | Different |
| Notification-area icon | `Ampify.tray`, permission `ui.tray`, setting `tray.enabled` | Done |
| Local remote | HTTP on `127.0.0.1` with a token, for phones and Stream Deck | Done |
| MIDI controller | `Ampify.midi` and `midi.map`, no light feedback | Done |
| "Now playing" for OBS | `Ampify.fs.writeText` to a text file | API done, no example |
| Sleep timer | Example: `examples/sleep-timer` | Done |
| Listening stats | `Ampify.storage` and `Ampify.player.on` | API done, no example |
| M3U8 playlists import and export | Needs the library, outside 1.0 | Outside 1.0 |
| Settings profiles | `ampify profile use <name>` | Done |
| Theme that follows the system or the time | `theme.scheme = auto`, `theme.schedule` | Done |
| Accessibility | A high contrast theme in the catalog | Partial |
| Diagnosis | `ampify doctor` | Done |
| Export and import of settings | `ampify export <file.zip>` and `ampify import` | Done |
| Interface texts in several languages | `Ampify.i18n` | Done |
| Visualizer | `Ampify.audio.onFrame` feeds it | Partial |

## 10. What the most used Spicetify themes and extensions need

The gaps found before 1.0, among what people do most with Spicetify, are closed: a full-screen now playing view with synchronized lyrics,
transitions on hover, named targets for album and playlist pages, and the player read on Apple Music 1.1792. What
remains is in the rows marked "Outside 1.0" above.

## 11. How each feature is checked

- Pure logic (JavaScript, validator, measurement, settings): tests under Node and Linux, without the app.
- Windows mechanisms (injection, the XAML tree, SMTC, notifications): the Windows bench in CI, and a test pass on a PC.
- Internal services (winmd projections): a read-only probe calls each method and logs the result.
- Sound (hooks, Atmos): measurements on real songs on a PC, with in-memory capture only.
- Anything that needs the real app signed in cannot be checked in CI without an Apple account. See
  [development/bench-and-ci.md](development/bench-and-ci.md).
