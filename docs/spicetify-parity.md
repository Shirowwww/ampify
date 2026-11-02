# Ampify, Spicetify and Cider

## At a glance

| | Ampify | Spicetify | Cider |
|---|---|---|---|
| What it is | A mod of the official Apple Music app | A mod of the Spotify app | A separate Apple Music player |
| Price | Free, GPL v3 | Free, LGPL | Paid |
| Systems | Windows 10 and 11 | Windows, macOS, Linux | Windows, macOS, Linux |
| Lossless and Dolby Atmos | Yes, Apple's own player | No (Spotify) | No lossless (Apple's web API does not offer it) |
| Themes | 16, CSS on Apple Music's controls, with GIFs and settings | Hundreds, CSS on web pages | Yes, mostly colors |
| Extensions and apps | JavaScript API, React-style UI, Marketplace | JavaScript API, React, Marketplace | Plugins, Marketplace |
| Discord, Last.fm | Extensions in the Marketplace | Community extensions | Built in |
| Full-screen lyrics | Apple's full-screen player, karaoke lyrics | Lyrics Plus | Built in |
| Audio tools | Equalizer, Atmos leveler, loudness meter for extensions | None | Equalizer, audio effects |
| Cast and AirPlay | No | Spotify Connect | Yes |
| Remote | Local HTTP remote with a token, MIDI | No | Mobile remote app |

Cider suits people on several systems who want casting. Ampify keeps Apple's lossless and Atmos playback and goes
further into the app's look and what extensions can do. Sources for Cider: [cider.sh](https://cider.sh/learn-more) and
its [release notes](https://cider.sh/changelogs/client-releases/2.5.0).

## Why Ampify works differently

Spotify on a PC is a web page, so Spicetify rewrites the app's files and injects CSS and JavaScript. Apple Music for
Windows is a native WinUI 3 app with no HTML and no web bundle. Ampify loads its kernel into Apple Music at start,
changes XAML properties of the running app, and runs extensions in its own JavaScript engine. Nothing is written into
the package. Themes therefore target XAML elements, by [stable names](themes/names.md) that Ampify keeps up to date.
How it works in detail: [development/how-it-works.md](development/how-it-works.md). Notes on Spicetify's own internals:
[knowledge/spicetify.md](../knowledge/spicetify.md).

## Feature by feature

Each line is one thing Spicetify does, with where Ampify stands.

| Status | Meaning |
|---|---|
| Done | Works, checked on screen or by tests |
| Partial | Works with the limit written in the row |
| Missing | Not built, with the reason |
| Fragile | Works, but depends on something that may break (named in the row) |

Features that only exist in Spotify (podcasts, DJ, Friend Activity, Canvas, ads) are left out.

## Command line and configuration

| Spicetify | Ampify | Status |
|---|---|---|
| Installer that finds Spotify | `ampify_setup.exe` finds the Apple Music package; nothing is written into it | Done |
| `backup`, `apply`, `restore` | `ampify apply` and `ampify restore` turn the background launcher on and off; `ampify backup` saves settings and elements | Done |
| `auto`, `launch` | `ampify launch` | Done |
| `config` read and write, list items `+id` / `-id` | `ampify config [key] [value]`, `ampify config extensions +id` | Done |
| `apply --extension`, `refresh`, `watch` | `ampify extension enable`, `ampify watch`, `ampify dev`: reload without restarting Apple Music | Done |
| `update`, `upgrade` | `ampify update`, SHA-256 checked | Done |
| `enable-devtools` | `ampify devtools`: the XAML tree of the page on screen in about a second. No element picker | Partial |
| `path`, `config-dir`, `--help`, `--version`, chained verbs | `ampify path`, `ampify config --dir`, chained verbs | Done |
| Restore after a broken Spotify update | The kernel stays off on an untested Apple Music; `ampify versions update` | Done |
| `spotify_launch_flags`, `check_spicetify_update` | `app_launch_flags`, `check_kit_update` | Done |
| `current_theme`, `color_scheme`, `inject_css`, `inject_theme_js`, `replace_colors`, `overwrite_assets` | `theme.active`, `theme.scheme` (also `auto` and `theme.schedule`), `theme.parts.*` | Done |
| `sidebar_config`, `home_config` | `sidebar.json`, `home.json` (hide, reorder); the names are not all read from the real app | Fragile |
| `disable_sentry`, `disable_ui_logging` | No setting: telemetry hosts would have to be answered locally | Missing |
| `xpui.js_find` / `xpui.js_repl` patches | No JavaScript bundle to patch | Missing |
| `expose_apis` | `expose_api` | Done |

## Themes

| Spicetify | Ampify | Status |
|---|---|---|
| `color.ini` with schemes | `colors.ini` read as is; `ampify theme import` brings a Spicetify theme's colours | Done |
| `user.css` | CSS on XAML types, `#names` and Ampify's classes ([themes/names.md](themes/names.md)); Spotify's own CSS does not carry over | Done |
| `:hover`, `:active`, `:focus`, `:not()`, `+`, `~`, `:nth-child()` | Same | Done |
| `transition` | `transform`, `opacity`, `background` | Done |
| `@keyframes`, `animation` | `opacity` and `transform`, on the compositor; artworks Apple Music moves itself refuse a transform (the element around them takes it) | Done |
| Animated colours, backgrounds, sizes | Only `opacity` and `transform` animate | Missing |
| `border-radius`, floating rounded panels | `corner-radius`; `ampify:floating-panels` rounds the sidebar, the pages and the lyrics pane with one `--panel-radius` | Done |
| Scrollbar styling | `.scrollbar`; `ampify:scrollbars` keeps bars inside their panel, clear of the rounded corners, without Windows' arrows | Done |
| `::before`, `::after` images and GIFs | `content: url(...)` | Done |
| Visualizer bars (cat-jam, equalizer GIFs) | `content: bars(n)`: bars that follow the music that plays | Done |
| Blurred artwork background (Hazy, Comfy, Bloom) | `background: artwork` under `acrylic()` panels | Done |
| Colours from the artwork (Dribbblish Dynamic) | `--art-1`, `--art-2`, `--art-3`, `--art-text` | Done |
| An artwork background that moves (Lucid, Kawarp) | Needs a Composition effect | Missing |
| `theme.js` | The theme's `main.js` | Done |
| Theme settings menu (each theme codes its own) | `settings` in the manifest, shown in the Ampify card, read as `var(--setting-*)` | Done |
| Fonts and images of the theme | `font-family: url(...)`, `url(...)` backgrounds | Done |
| Context menus, tooltips, volume bar | `.menu`, `.menu-item`, `.tooltip`, `.flyout`, `.volume-slider` | Done |
| Selected and playing rows | Selected rows (`selected-background`); no "playing" state on rows | Partial |
| Lyrics lines | `.lyrics`, `.lyrics-synced`; single lines are not named | Partial |
| Full-screen player | Apple's player (`.fullscreen-*`) | Done |
| Mini player | Its own window and interface thread: themes do not reach it | Missing |
| Layout changes (bar at the bottom, columns swapped) | `ampify:bottom-bar`, `order`, `grid-*`; Apple's lists resist | Partial |
| `overflow`, `filter`, `mix-blend-mode`, `clip-path`, `position` | No XAML equivalent; `display: none`, alignment and grid placement instead | Missing |
| Names that survive a Spotify update | Classes for 1.1792; a new Apple Music may rename elements | Fragile |

## Snippets

| Spicetify snippet | Ampify | Status |
|---|---|---|
| Rotating or spinning CD cover art | `spinning-artwork`: a round record with grooves and a spindle hole, turning on the compositor | Done |
| Hamsters, Oneko, Duck and other GIFs on the playback bar | `::before` / `::after` images | Done |
| Equalizer in the bar | `display-equalizer`: `bars(10)` in the artwork's colour | Done |
| Rounded or square images, buttons and bars (Be Square, Rounded Images) | `corner-radius`; `square-corners` squares every panel, the bar included | Done |
| Hide an element (play count, buttons, recent searches) | `display: none` on its name | Done |
| Modern scrollbar, hide sidebar scrollbar | `.scrollbar`, `ampify:scrollbars` | Done |
| Hover panels, dynamic sidebar | `:hover` works; a hidden panel keeps its room | Partial |
| Nyan Cat, Kirby, bee progress bars | The image sits on the bar; it does not follow the song's position | Partial |
| Duotone artwork | No colour filter on an image in XAML | Missing |
| Cover Ambience | `cover-ambience`: the display glows in the artwork's colours | Done |

## Extensions (JavaScript API)

| Spicetify | Ampify | Status |
|---|---|---|
| One JavaScript file, wait for the APIs | `main.js` and `manifest.json`, ES modules, `Ampify.ready` | Done |
| `Player`: state, track, events, play, pause, next, previous, seek | `Ampify.player`, through Apple's player and SMTC | Done |
| `Player.setVolume` | Read only (`Ampify.audio.volume()`); a gain on the samples (`audio.setGainDb`) | Partial |
| `Platform.History` | `Ampify.ui.navigate` for Ampify's own pages; Apple Music's pages are not reachable | Partial |
| `Platform.Library`, `Playlists`, `CosmosAsync`, `GraphQL` | Apple reads its library in a private format; no internal API exposed | Missing |
| `Queue`, `removeFromQueue` | `player.queue.list()` (complete once Apple's Up Next panel was opened), `queue.remove` | Partial |
| `addToQueue` | Apple's player ignores what Ampify hands it ([knowledge/apple-music-queue.md](../knowledge/apple-music-queue.md)) | Missing |
| `React`, `ReactDOM`, `ReactComponent`, `ReactHook` | `Ampify.React`, `Ampify.components`, `Ampify.hooks` (no `useTheme`) | Done |
| `Topbar`, `Playbar` buttons | `ui.topbar.addButton`, `ui.playbar.addButton`, `ui.setLabel` to change their text | Done |
| `ContextMenu`, `Menu` | `ui.menu.addItem`: the item's name, no URI, no submenu | Partial |
| `PopupModal`, `Panel`, `AppTitle` | `ui.popup`, `ui.panel`, `ui.title` (Ampify's surfaces, not the window title) | Done |
| `Keyboard`, `LocalStorage`, `Config`, `URI`, `Locale`, `SVGIcons`, `showNotification` | `keyboard.bind`, `storage`, `config`, `uri`, `i18n`, `icons`, `notify` | Done |
| `colorExtractor` | `color.extract`, `color.fromArtwork` | Done |
| `getAudioData` | `audio.analyze`, `onFrame`, `loudness`: in both processes since the interface reads what the audio process shares | Done |
| Settings pages built by each extension | `settings` in the manifest, shown in the Ampify card | Done |
| TypeScript, JSX, bundling | `ampify new`, `dev`, `test`, `build`, `types`; no TypeScript or JSX compiler | Partial |

## Custom apps and Marketplace

| Spicetify | Ampify | Status |
|---|---|---|
| Custom apps with a sidebar entry and pages | `kind: app`, `ui.sidebar.addItem`, `ui.page.register`; no active-state icon | Partial |
| Marketplace in the app: tabs, search, install, update, schemes | The Marketplace page, SHA-256 checked, permissions shown before activation | Done |
| Public catalog | [ampify-catalog](https://github.com/Shirowwww/ampify-catalog), reviewed; a few dozen items against hundreds | Partial |
| Publish (topic and manifest) | `ampify publish` opens the pull request | Done |

## Popular everyday extensions

| Spicetify | Ampify | Status |
|---|---|---|
| Full Screen | The Now Playing button opens Apple's full-screen player, karaoke lyrics included | Done |
| Cover Ambience | Snippet `cover-ambience` | Done |
| Volume Percentage | Extension `volume-percentage`: the volume of Apple Music as a percentage, in the playback bar | Done |
| Lyrics Plus | Apple's synced lyrics and translation, in its full-screen player and lyrics pane | Done |
| Stats | App `listening-stats`: top artists and songs over 7 days, 4 weeks or since the start, listening time, kept on the PC; no account-wide history | Done |
| New Releases | Apple Music's own New page | Done |
| Shuffle+ (true shuffle) | Needs `addToQueue`, which Apple ignores | Missing |
| adblockify, Toggle DJ, "Made for you" shortcut, HomeWhereYouBelong, No Tracking Links | Spotify only: Apple Music has no ads, no DJ, and its share links carry no tracking | Missing |

## Beyond Spicetify

Ampify also has: a 10-band equalizer and an Atmos leveler on the samples, a LUFS meter, Discord Rich Presence and
Last.fm scrobbling as extensions, a local HTTP remote, MIDI, a notification-area icon, settings profiles, themes that
follow Windows or the time, tests of an element without Apple Music (`ampify test`), and an anonymous diagnosis report.

## Cider

Cider is a separate, paid player built on Apple's web API, for Windows, macOS and Linux. It has casting, AirPlay and a
mobile remote; it cannot play lossless. Ampify keeps Apple's own player, so lossless and Dolby Atmos stay, and goes
deeper into how the app looks and what extensions do. Sources: [cider.sh](https://cider.sh/learn-more),
[release notes](https://cider.sh/changelogs/client-releases/2.5.0); for Spicetify, [spicetify.app](https://spicetify.app/docs)
and its [repository](https://github.com/spicetify/cli).
