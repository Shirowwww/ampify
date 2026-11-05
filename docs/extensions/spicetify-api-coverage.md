---
title: Spicetify API coverage
description: Each part of Spicetify's extension and theme API, and what Ampify offers in its place.
---

# Spicetify API coverage

This page is for people who know Spicetify and want to know, part by part, what Ampify gives them. It lists Spicetify's
API by area and gives each row a status. It does not repeat the feature table of [spicetify-parity.md](../spicetify-parity.md),
which covers the whole product (themes, command line, Marketplace). Read that page for the big picture.

Ampify's own reference is [api.md](api.md). Its types are in `ampify types`, and [examples/](../../examples/) shows each area.

## How to read the status

| Status | Meaning |
|---|---|
| Same | Same name and same job, or close enough to port the code with small changes |
| Partial | Works, with a limit written in the row |
| Different | Another way to reach the same goal |
| Missing | Not built yet, and possible on Apple Music |
| Impossible | Needs Apple Music internals that do not exist or are private |

Spicetify's names are from its [documentation](https://spicetify.app/docs) and its
[`globals.d.ts`](https://github.com/spicetify/cli/blob/main/globals.d.ts). Where Spicetify's own name is a Spotify
detail, the row says so.

## Player

| Spicetify | Ampify | Status | Note |
|---|---|---|---|
| `Player.play`, `pause`, `togglePlay`, `next`, `back`, `previous` | `player.play()`, `pause()`, `toggle()`, `next()`, `previous()` | Same | Needs `player.control` |
| `Player.seek(ms)` | `player.seek(ms)` | Same | |
| `Player.setVolume(0 to 1)` | `player.setVolume(v)` | Partial | Apple's slider is in decibels: 0.5 is not the middle. See [api.md](api.md#53-player-stable) |
| `Player.getVolume()` | `audio.volume()` | Partial | Known once a song has started |
| `Player.toggleShuffle`, `setShuffle`, `toggleRepeat`, `setRepeat` | `player.toggleShuffle()`, `setShuffle()`, `toggleRepeat()`, `setRepeat()` | Same | Apple's own buttons |
| `Player.getShuffle`, `getRepeat` | `player.shuffle()`, `player.repeat()` | Same | |
| `Player.data` (track, position, duration, context) | `player.state()` | Partial | No playback context (playlist, album URI) |
| `Player.getProgress`, `getDuration`, `isPlaying` | `player.state().positionMs`, `durationMs`, `playing` | Same | |
| `Player.addEventListener("songchange")` | `player.on("songchange", fn)` or `"trackchange"` | Same | Spicetify's name is accepted too |
| `Player.addEventListener("onplaypause")` | `player.on("onplaypause", fn)` or `"statechange"` | Partial | Also fires when the length becomes known or the position jumps, not only on play and pause |
| `Player.addEventListener("onprogress")` | Poll `player.state()` or use `onFrame` for audio | Different | No progress event yet |
| `Player.removeEventListener` | `player.off(event, fn)` | Same | |
| `Player.origin`, `Player.getQueue` | `player.queue.list()` | Partial | Complete only once Apple's "Up Next" panel was opened |
| `Player.addToQueue`, `removeFromQueue` | `player.queue.remove(id)`; `queue.add` answers `false` | Missing | Apple's player ignores queued songs ([apple-music-queue](../../knowledge/apple-music-queue.md)) |
| `Player.playUri`, `playUri` with context | Not available | Missing | Apple's internal play API is not public |
| Lyrics (Lyrics Plus) | `player.lyrics()`, `Ampify.lyrics.current()` | Same | Apple's own lyrics, with word times when Apple gives them |
| Cover art of the track | `player.artwork()`, `track.artworkColor` | Same | |

## Platform

| Spicetify | Ampify | Status | Note |
|---|---|---|---|
| `Platform.History` (listen, push, goBack) | `ui.navigate(path)` for Ampify's pages | Partial | Apple Music's pages cannot be reached |
| `Platform.Library` (likes, folders, albums, artists) | `library.tracks()` (read only, permission `library.read`) | Partial | Tracks with play counts from Apple's library file. No likes, no writes |
| `Platform.PlaylistAPI` | Not available | Missing | Apple's playlists are behind Apple's private services |
| `Platform.UserAPI` (user, country, product) | `Ampify.locale`, `Ampify.i18n.language` | Different | No account data: Ampify keeps it out on purpose |
| `Platform.PlayerAPI` | Not available | Impossible | Spotify's own player object does not exist in Apple Music |
| `CosmosAsync` (REST calls to Spotify) | `Ampify.http` to a declared host | Different | Apple's internal web calls are private, so Ampify only offers plain https calls, declared in the manifest |
| `GraphQL` | Not available | Impossible | Apple Music has no public GraphQL |

## Interface

| Spicetify | Ampify | Status | Note |
|---|---|---|---|
| `Topbar.Button` | `ui.topbar.addButton({ label, icon, onClick, active })` | Same | Apple has no separate top bar: buttons go next to the lyrics and queue buttons |
| `Playbar.Button`, `Playbar.Widget` | `ui.playbar.addButton(...)` | Partial | Buttons only, no free widget |
| `ContextMenu.Item`, `SubMenu` | `ui.menu.addItem("context", { ... })` and `items: [...]` | Partial | No `shouldAdd` and no URI: the item's `kind` and ids come from Apple's data when it has them |
| `Menu` (app menu) | `ui.menu.addItem("app", ...)` | Same | |
| `PopupModal.display`, `hide` | `ui.popup.open(view)` | Same | |
| `Panel` (side panel) | `ui.panel.register(id, view)` | Same | |
| `AppTitle` | `ui.title.set(text)` | Same | Title of Ampify's panel, page or popup, not the window title |
| Sidebar entries (custom apps) | `ui.sidebar.addItem({ ... })` | Same | Icon is a Segoe Fluent glyph, not a Spotify icon |
| Custom apps, pages | `kind: "app"`, `ui.page.register(path, view)` | Same | |
| `Keyboard`, `Mousetrap.bind` | `keyboard.bind(shortcut, fn)` | Partial | No `unbind` yet, no sequences |
| `Locale` | `Ampify.i18n.t`, `i18n.define` | Same | Eight languages for Ampify's own texts |
| `SVGIcons` | `Ampify.icons.get(name)`, `icons.names()` | Same | |
| `showNotification` | `Ampify.notify({ title, text })` | Same | Permission `notifications` |
| `colorExtractor` | `color.extract(...)`, `color.fromArtwork(url)` | Same | |
| `React`, `ReactDOM`, `ReactComponent` | `Ampify.React`, `Ampify.ReactDOM`, `Ampify.components` | Partial | React-style API on Ampify's own node tree; no React internals and no class components |
| `ReactHook.useTheme`, other React hooks | `Ampify.hooks.usePlayer()`, `useState`, `useEffect`, ... | Partial | No `useTheme` |
| Spotify's own React components (`Button`, `Toggle`...) | `Ampify.components.Button`, `Toggle`, `Slider`, `Dropdown`... | Different | Ampify's own, drawn with WinUI controls |
| `Config` (extension settings) | `Ampify.config.get/set` and the manifest `settings` | Same | The manifest declares the settings page |
| `LocalStorage` | `Ampify.storage.get/set/remove` (permission `storage`) | Same | One JSON file per element |
| `Ampify.link` (app and extension talking) | `Ampify.link.post/on/connected` | Different | Spicetify's `subfiles_extension` idea; messages are JSON |
| `URI` (parse and build links) | `Ampify.uri.parse`, `Ampify.uri.build` | Partial | Apple's links: `music:`, `itms:`, `music.apple.com` |

## Themes, snippets and files

| Spicetify | Ampify | Status | Note |
|---|---|---|---|
| `color.ini` with schemes | `colors.ini` as it is | Same | [themes/format.md](../themes/format.md) |
| `user.css` | `user.css` on XAML names | Partial | Spotify's class names do not carry over: see [themes/names.md](../themes/names.md) |
| Snippets (CSS in the Spicetify settings) | `kind: "snippet"` | Same | Switched on alone |
| `theme.js` | `main.js` of the theme | Same | |
| `ampify` CLI scaffold | `ampify new`, then the kind: theme, snippet, extension or app | Same | Starters that already run |

## Impossible, and why

- **React and Spotify internals** (`Platform` objects, Cosmos, GraphQL, the router): Apple Music is a WinUI app.
  Its screens are not a web page with a JavaScript bundle, and the calls behind them are private.
- **Editing Apple Music's own code**: Ampify never changes the package. It does not hook the DRM or FairPlay.
- **Spotify-only pieces**: ads, DJ, Canvas, Friend Activity, podcasts. They have no Apple Music equivalent.

## Top 10 gaps, by value

Ranked by what people would miss most, with the status they see today.

1. **Queue add (`addToQueue`)**: missing. Needed for "true shuffle" style extensions. Apple's player ignores it.
2. **Playlists (`PlaylistAPI`)**: missing. Needs Apple's private services.
3. **Mini player**: missing. Its own window, so themes do not reach it.
4. **Theme filters and blends** (`filter`, `mix-blend-mode`): missing. XAML has no equivalent.
5. **Duotone or tinted artwork**: missing. No colour filter on an image in XAML.
6. **Progress event (`onprogress`)**: different. Extensions must poll `player.state()`.
7. **Keyboard unbind and sequences**: partial. `keyboard.bind` only.
8. **Context menu conditions (`shouldAdd`) and URIs**: partial. Apple's menu data is used only where it gives it.
9. **Library writes (likes, folders)**: missing. `library.write` is reserved.
10. **`useTheme` and other React hooks**: partial. Themes are applied by the theme engine, not by a hook.

Gaps 1, 2, 9 depend on Apple's private services. Gap 6 and 7 are the cheapest to add next.

## Changes made with this page

- `player.off(event, fn)` removes a listener. Before, an extension could not stop listening.
- `player.on` accepts Spicetify's `songchange` and `onplaypause` names.
- `http.open(url)` is now in the TypeScript types (it was in the reference but missing from the types).
- A manifest typo now gets a suggestion: `unknown permission: storag (did you mean "storage"?)`.
