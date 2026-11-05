# Ampify, Spicetify and Cider

## At a glance

| | Ampify | Spicetify | Cider |
|---|---|---|---|
| What it is | A mod of the official Apple Music app | A mod of the Spotify app | A separate Apple Music player |
| Price | Free, PolyForm Noncommercial | Free, LGPL | Paid |
| Systems | Windows 10 and 11 | Windows, macOS, Linux | Windows, macOS, Linux |
| Lossless and Dolby Atmos | Yes, Apple's own player | No (Spotify) | No lossless (Apple's web API does not offer it) |
| Themes | 11 with 27 schemes, CSS on Apple Music's controls, with GIFs and settings | Hundreds, CSS on web pages | Yes, mostly colors |
| Extensions and apps | JavaScript or TypeScript API, React-style UI, Marketplace | JavaScript API, React, Marketplace | Plugins, Marketplace |
| Discord, Last.fm | Extensions in the Marketplace | Community extensions | Built in |
| Full-screen lyrics | Apple's full-screen player, karaoke lyrics | Lyrics Plus | Built in |
| Audio tools | Apple Music's own equalizer stays; Ampify adds a 10-band equalizer API for extensions (no screen of its own; it reaches Dolby Atmos, which Apple's skips), an Atmos leveler, a loudness meter | None | Equalizer, audio effects |
| Cast and AirPlay | No | Spotify Connect | Yes |
| Remote | Local HTTP remote with a token (this PC only), MIDI keys and knobs | No | Mobile remote app |

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
| `enable-devtools` | `ampify devtools`: the XAML tree of the page on screen within a few seconds; `ampify devtools pick`: put the pointer on an element, and it prints the element under it, a selector and the elements above it | Done |
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
| Animated colours, backgrounds, sizes | `transition` eases plain `background` colours; `@keyframes` also animate a plain `background` colour (stepped 30 times a second, never a gradient or an image); sizes do not animate | Partial |
| `border-radius`, floating rounded panels | `corner-radius`; `ampify:floating-panels` rounds the sidebar, the pages and the lyrics pane with one `--panel-radius` | Done |
| Scrollbar styling | `.scrollbar`; `ampify:scrollbars` keeps bars inside their panel, clear of the rounded corners, without Windows' arrows | Done |
| `::before`, `::after` images and GIFs | `content: url(...)` | Done |
| Visualizer bars (cat-jam, equalizer GIFs) | `content: bars(n)`: bars that follow the music that plays | Done |
| Blurred artwork background (Hazy, Comfy, Bloom) | `background: artwork` under `acrylic()` panels | Done |
| Colours from the artwork (Dribbblish Dynamic) | `--art-1`, `--art-2`, `--art-3`, `--art-text`; `--page-art-*` for the album or playlist on screen | Done |
| Windows 11 look (Fluent, Bloom's Mica) | `--window-material: mica \| mica-alt \| acrylic` on the window; theme `fluent` | Done |
| Liquid glass with refraction | Acrylic panes with a rim of light (theme `glass`); no shader bends what lies behind | Partial |
| An artwork background that moves (Lucid, Kawarp) | `content: artwork cover` with `@keyframes` turns the artwork behind the panes (Glass, setting `moving-artwork`); no shader warp like Kawarp's | Partial |
| `theme.js` | The theme's `main.js` | Done |
| Theme settings menu (each theme codes its own) | `settings` in the manifest, shown in Ampify's settings page, read as `var(--setting-*)` | Done |
| Fonts and images of the theme | `font-family: url(...)`, `url(...)` backgrounds | Done |
| Context menus, tooltips, volume bar | `.menu`, `.menu-item`, `.tooltip`, `.flyout`, `.volume-slider` | Done |
| Selected and playing rows | Selected rows (`selected-background`); the row of the song playing with `:playing` (`.list-item:playing`) | Done |
| Lyrics lines | `.lyrics`, `.lyrics-synced`, `.lyrics-line`, `.lyrics-line-content`, `.lyrics-words`, `.lyrics-background-words`, `.lyrics-instrumental`; the line being sung with `:current` (`.lyrics-line-content:current`, the Lyrics spotlight snippet) | Done |
| Full-screen player | Apple's player (`.fullscreen-*`) | Done |
| Mini player | Ampify's own small window on top, with its own thread: cover, controls, progress, coloured by `--mini-*`. Apple's own mini player is not themed | Partial |
| Layout changes (bar at the bottom, columns swapped) | `ampify:bottom-bar`, `order`, `grid-*`; Apple's lists resist | Partial |
| `overflow`, `clip-path` | `overflow: hidden` and `clip-path: inset(...)` cut an element to a rectangle; no rounded or shaped clips | Done |
| `filter`, `mix-blend-mode`, `position` | No XAML equivalent; `display: none`, alignment and grid placement instead | Missing |
| Names that survive a Spotify update | Classes for 1.1792; a new Apple Music may rename elements | Fragile |

## Snippets

| Spicetify snippet | Ampify | Status |
|---|---|---|
| Rotating or spinning CD cover art | `spinning-artwork`: a round record with grooves and a spindle hole, turning while a song plays and still in pause | Done |
| Hamsters, Oneko, Duck and other GIFs on the playback bar | `::before` / `::after` images | Done |
| Equalizer in the bar | `display-equalizer`: `bars(10)` in the artwork's colour | Done |
| Rounded or square images, buttons and bars (Be Square, Rounded Images) | `corner-radius`; `square-corners` squares every panel, the bar included | Done |
| Hide an element (play count, buttons, recent searches) | `display: none` on its name | Done |
| Modern scrollbar, hide sidebar scrollbar, thin scrollbars | `.scrollbar`, `ampify:scrollbars`; the thin look away from the pointer is already built in, so no separate snippet | Done |
| Hover panels, dynamic sidebar | `:hover` works; a hidden panel keeps its room | Partial |
| Nyan Cat, Kirby, bee progress bars | An image in the column of the bar's hidden thumb rides at the song's position ([recipe](themes/format.md#an-image-that-follows-the-songs-progress)); snippet `progress-mascot` (a comet, a note, a heart) | Done |
| Duotone artwork | No colour filter on an image in XAML | Missing |
| Cover Ambience | `cover-ambience`: the display glows in the artwork's colours | Done |
| Colourful album pages | `album-colors`: an album or a playlist takes the colours of its own artwork | Done |
| Hide the filter bar of playlists | `declutter`, setting "Hide the search bar of albums and playlists" | Done |
| Spotify's playback bar at the bottom, across the window | `bottom-playbar`, over any theme or none; `ampify:bottom-bar` for a theme | Done |
| Thin or thick progress bar | `progress-thickness`: the display's progress line from 1 to 6 pixels, round ends | Done |
| Compact sidebar (Comfy) | `compact-sidebar`: rows of 32 pixels instead of 40, Ampify's entries included (`.sidebar-entry`) | Done |
| Highlight the lyrics line being sung | `lyrics-spotlight`: a tile of the song's colour under the line, through `:current` | Done |
| Hide the explicit badge | `hide-explicit` | Done |
| Centred lyrics | `centred-lyrics`: the lines and the static text sit in the middle of the panel | Partial: the line being sung keeps Apple's own fade, checked by tests only |
| Bigger album artwork | `large-album-artwork`: a size setting, 120 to 360 pixels, on the album or playlist header | Partial: checked by tests only, not on screen |
| Hover-revealed row actions | `hover-row-actions`: favourite, add, download and reaction buttons fade in under the pointer | Partial: checked by tests only, not on screen |
| Hide more shelves on Home and New | Missing: Apple's shelves have no names of their own, so a plain hide would empty the page | Missing |

## Extensions (JavaScript API)

| Spicetify | Ampify | Status |
|---|---|---|
| One JavaScript file, wait for the APIs | `main.js` and `manifest.json`, ES modules, `Ampify.ready` | Done |
| `Player`: state, track, events, play, pause, next, previous, seek | `Ampify.player`, through Apple's player and SMTC | Done |
| `Player.setVolume` | `Ampify.player.setVolume(0 to 1)` sets Apple's player volume once a song has started; from a `ui` extension it moves Apple's own slider (a decibel scale, not a straight 0 to 1). `audio.volume()` reads it, `audio.setGainDb` adds a gain | Done |
| `Player.toggleShuffle`, `setShuffle`, `toggleRepeat`, `setRepeat` | `player.toggleShuffle()`, `setShuffle(on)`, `toggleRepeat()`, `setRepeat("off" \| "all" \| "one")`, `shuffle()`, `repeat()`: Apple's own buttons, checked on screen | Done |
| `Platform.History` | `Ampify.ui.navigate` for Ampify's own pages; `http.open` of a `music.apple.com` link (`uri.build`) opens that album, artist, playlist or song in Apple Music's own window; no back and forward of Apple's history | Done |
| `Platform.Library`, `Playlists`, `CosmosAsync`, `GraphQL` | `library.tracks()` reads the library file with play counts (permission `library.read`); no playlists, no writes, no Cosmos or GraphQL: Apple's internal API is private | Partial |
| `Queue`, `removeFromQueue` | `player.queue.list()` (complete once Apple's Up Next panel was opened), `queue.remove` | Partial |
| `addToQueue` | Apple's player ignores what Ampify hands it ([knowledge/apple-music-queue.md](../knowledge/apple-music-queue.md)) | Missing |
| `React`, `ReactDOM`, `ReactComponent`, `ReactHook` | `Ampify.React`, `Ampify.components`, `Ampify.hooks` (no `useTheme`) | Done |
| `Topbar`, `Playbar` buttons | `ui.topbar.addButton`, `ui.playbar.addButton`, `ui.setLabel` to change their text, `active` and `ui.setActive` for a toggle drawn in the accent colour | Done |
| `ContextMenu`, `Menu` | `ui.menu.addItem`: the item's name and, when Apple's data gives it, its kind and catalog id (no URI, no `shouldAdd` to show an entry for some items only); an entry can hold a submenu (one level, 20 entries) | Partial |
| `PopupModal`, `Panel`, `AppTitle` | `ui.popup`, `ui.panel`, `ui.title` (Ampify's surfaces, not the window title) | Done |
| `Keyboard`, `LocalStorage`, `Config`, `URI`, `Locale`, `SVGIcons`, `showNotification` | `keyboard.bind`, `storage`, `config`, `uri`, `i18n`, `icons`, `notify` | Done |
| `colorExtractor` | `color.extract`, `color.fromArtwork` | Done |
| `getAudioData` | `audio.analyze`, `onFrame`, `loudness`: in both processes since the interface reads what the audio process shares | Done |
| Settings pages built by each extension | `settings` in the manifest, shown in Ampify's settings page | Done |
| TypeScript, JSX, bundling | `ampify new`, `dev`, `test`, `build`, `types`; `build` and `dev` turn `main.ts`, `main.tsx` or `main.jsx` into `main.js` with esbuild (the project's own, `npm install --save-dev esbuild`, or one on the PATH) | Done |

## Custom apps and Marketplace

| Spicetify | Ampify | Status |
|---|---|---|
| Custom apps with a sidebar entry and pages | `kind: app`, `ui.sidebar.addItem` (`active` or `setActive`: the accent colour and Apple's selection bar), `ui.page.register` | Done |
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
| Stats | App `listening-stats`: listening time, plays, top artists, songs and albums over 7 days, 4 weeks or since the start, day by day and hour by hour, kept on the PC; no account-wide history | Done |
| New Releases | App `new-releases`: the recent albums and singles of the artists you play, from Apple's public catalog; Apple Music's own New page too | Done |
| Trash Bin, Auto Skip | Extension `skip-list`: "Never play this" in the menus and a play bar button, the list in the main menu, exported and imported as text; an album or an artist by its kind, a song by its catalog id when Apple gives one; skips live versions, remixes and intros on request | Done |
| Bookmark | Extension `bookmarks`: songs, albums, artists, playlists and moments of a song, opened again from the main menu | Done |
| Clickable song info | The title, artist and album of the playback bar's display open their pages | Done |
| Loopy Loop | Extension `ab-loop`: the play bar button marks the start, then the end of a passage, which loops until a third press or another song | Done |
| Better Local Files | Apple Music already lists the files of the PC under Library, by album, artist and song, with their artwork | Built in |
| Name That Tune | Needs a player for the catalog's previews beside Apple's; Ampify plays sound only through Apple Music | Missing |
| Full Screen settings | Snippet settings can change Apple's full-screen player, and Ampify adds shuffle and repeat around its transport buttons; the player Apple opens by itself in screen mode has neither | Partial |
| Shuffle+ (true shuffle) | Needs `addToQueue`, which Apple ignores | Missing |
| adblockify, Toggle DJ, "Made for you" shortcut, HomeWhereYouBelong, No Tracking Links | Spotify only: Apple Music has no ads, no DJ, and its share links carry no tracking | Missing |

## Beyond Spicetify

Ampify also has: a 10-band equalizer API for extensions on the samples (Apple Music keeps its own equalizer, which it
skips for Dolby Atmos; Ampify's has no screen and was tested with synthetic sound) and an Atmos leveler, a LUFS meter,
Discord Rich Presence and Last.fm scrobbling as extensions, a local HTTP remote (this PC only), MIDI, a notification-area icon, settings profiles, themes that
follow Windows or the time, tests of an element without Apple Music (`ampify test`), and an anonymous diagnosis report.

## Cider

Cider is a separate, paid player built on Apple's web API, for Windows, macOS and Linux. It has casting, AirPlay and a
mobile remote; it cannot play lossless. Ampify keeps Apple's own player, so lossless and Dolby Atmos stay, and goes
deeper into how the app looks and what extensions do. Sources: [cider.sh](https://cider.sh/learn-more),
[release notes](https://cider.sh/changelogs/client-releases/2.5.0); for Spicetify, [spicetify.app](https://spicetify.app/docs)
and its [repository](https://github.com/spicetify/cli).
