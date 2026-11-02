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

## Command line and settings

| Spicetify | Ampify |
|---|---|
| `backup`, `apply` | `ampify backup`, `ampify apply` (turns on the background launcher; nothing to patch) |
| `restore` | `ampify restore` |
| `auto` | `ampify launch` |
| `watch`, `refresh` | `ampify watch`, `ampify dev <folder>` |
| `config`, `config extensions <id>` | `ampify config`, `ampify extension enable <id>` |
| `path`, `config-dir` | `ampify path`, `ampify config --dir` |
| `upgrade`, `update` | `ampify update` |
| `enable-devtools` | `ampify devtools` (the XAML tree of the page on screen) |
| Chained verbs | `ampify backup apply launch` |
| Legacy versions | `ampify install-kit <version>` |
| `spotify_launch_flags`, `check_spicetify_update`, `expose_apis` | `app_launch_flags`, `check_kit_update`, `expose_api` |
| `current_theme`, `color_scheme` | `theme.active`, `theme.scheme` (also `auto` and `theme.schedule`) |
| `inject_css`, `inject_theme_js`, `replace_colors`, `overwrite_assets` | `theme.parts.resources`, `.script`, `.colors`, `.assets` |
| `sidebar_config`, `home_config` | `sidebar.json`, `home.json` |
| `spotify_path`, `prefs_path` | Not needed: `ampify path app` and `ampify path prefs` print them |
| `disable_sentry`, `disable_ui_logging`, `xpui.js` patches | No equivalent |

## Themes

| Spicetify | Ampify |
|---|---|
| `color.ini` | Works as it is ([details](themes/spicetify.md)) |
| `user.css` | `user.css` with XAML selectors and stable names; Spotify's own rules do not apply ([format](themes/format.md)) |
| `theme.js` | The theme's `main.js`, with the `Ampify` API |
| Theme options | `settings` in the manifest, shown in the Ampify card |
| Snippets | `kind: snippet` |
| Import a theme | `ampify theme import`: colors carry over, layout rules do not |

## Extensions and apps

| Spicetify | Ampify |
|---|---|
| `Player` | `Ampify.player` (state, events, commands, lyrics, queue) |
| `Player.setVolume` | `Ampify.audio.setGainDb`, a gain on the samples (there is no `setVolume`) |
| `Platform.History` | `Ampify.ui.navigate` for app pages only |
| `Platform.Library`, `Playlists`, `CosmosAsync`, `GraphQL` | Not available |
| `React`, `ReactDOM`, `ReactComponent`, `ReactHook` | `Ampify.React`, `Ampify.ReactDOM`, `Ampify.components`, `Ampify.hooks` |
| `Topbar`, `Playbar` | `Ampify.ui.topbar`, `Ampify.ui.playbar` (both in the playback bar) |
| `ContextMenu`, `Menu` | `Ampify.ui.menu.addItem`: the item's name only, no URIs or submenus |
| `PopupModal`, `Panel` | `Ampify.ui.popup`, `Ampify.ui.panel` |
| `Keyboard`, `LocalStorage`, `Config` | `Ampify.keyboard`, `Ampify.storage`, `Ampify.config` |
| `Queue`, `addToQueue`, `removeFromQueue` | `Ampify.player.queue.list` and `remove`; `add` does not work yet |
| `colorExtractor`, `getAudioData`, `showNotification` | `Ampify.color`, `Ampify.audio`, `Ampify.notify` |
| `Locale`, `SVGIcons`, `getFontStyle` | `Ampify.i18n`, `Ampify.icons`, `Ampify.fonts` |
| `CustomApps/<name>` | `apps/<id>` with a global `render()` |
| `subfiles`, `subfiles_extension` | ES modules, `Ampify.link` |
| Creator | `ampify new`, `dev`, `test`, `build`, `types`, `publish` |

## Only in Ampify

The Atmos leveler, a 10-band equalizer, a loudness meter, Apple's synced lyrics through the API, Discord presence,
a notification-area icon, a local HTTP remote and MIDI control, settings profiles, export and import, and a theme that
follows Windows' mode or the clock.
