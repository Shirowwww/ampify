# How Spicetify works, and what Ampify takes from it

A reading of the Spicetify CLI source (`spicetify/cli` at `cf2081f`, v2.46.4) and of its ecosystem, to learn from its
design. Paths are relative to that repository. What Ampify already offers in return is in
[docs/spicetify-parity.md](../docs/spicetify-parity.md).

## Install and apply

Spotify's interface is a web page packed in `Apps/xpui.spa` (a zip). Spicetify backs the `.spa` files up, unzips them,
runs about 60 regex patches over the JS, CSS and HTML (`src/preprocess/preprocess.go`), turns 26 hard-coded colours into
`var(--spice-*)`, then replaces `Apps/*.spa` with the patched folders, which Spotify loads as they are. The state is read
from the layout of `Apps/` alone: only `.spa` files means stock, only folders means applied, both means Spotify updated
over a modded install. It refuses to run as administrator, because files written by an elevated process cannot be read
by Spotify running as the user.

| Patch group | Effect |
|---|---|
| css-map | hashed class names become stable names, in CSS and JS alike |
| colours | 26 hex values become `--spice-*` variables, so `color.ini` recolours the whole app |
| `expose_apis` | internal objects are assigned to `Spicetify.*` while the page loads (the extension API) |
| telemetry | about 30 telemetry functions start with `return;` |
| version-gated fixes | per-version hacks guarded by `ParseSpotifyVersion().AtLeast()` |
| compat CSS | about 4 KB of rules Spotify removed are appended back, so old themes keep working |

`Replace` returns silently when nothing matches: a patch that no longer matches gives no warning. This is the main
source of "applied but broken" reports after a Spotify update.

## css-map, the stable names

Ampify's equivalent is its table of stable names (`src/core/theme/builtins.cpp`, [names](../docs/themes/names.md)).

- `css-map.json` is a flat `{hash: name}` object: 3,652 hashes for 1,470 names. Every Spotify version's hash is kept, so
  one file serves old and new builds. Names carry an area prefix (`main-`, `playlist-`, `lyrics-`...).
- It is fetched from GitHub at backup, with a shipped copy as fallback, so name fixes ship without a CLI release.
- Since v2.46, `scripts/update-cssmap.mjs` proposes new pairs from two extracted versions using two signals (CSS-module
  export objects matched by key overlap, and rules with the same selector and body). Every rejected candidate gets a
  reason, any conflict writes nothing, and new names are always chosen by a person.
- Failures seen in issues: one name given to two roles (a divider and a button wrapper, so the divider's rule collapsed
  the buttons), hashes left unmapped, platform-specific builds.

## Colours and load order

`color.ini` sections (`[Base]`, `[Dark]`...) hold 18 keys (`text subtext main main-elevated highlight
highlight-elevated sidebar player card shadow selected-row button button-active button-disabled tab-active notification
notification-error misc`). Each becomes `--spice-<key>` and `--spice-rgb-<key>: r,g,b`.

The page loads `colors.css`, `user.css`, the runtime wrapper (synchronous, so `window.Spicetify` exists first), then
`theme.js`, the extensions in config order and the custom apps. Extensions can run before Spotify has booted: there is
no single readiness promise, only polling and the one-shot `platformLoaded` and `webpackLoaded` events.

## The runtime wrapper and the API

`src/jsHelper/spicetifyWrapper/` (about 3,800 lines) builds the `Spicetify` object:

- a player facade over `Platform.PlayerAPI`, with `songchange` and `onplaypause` found by diffing the state and
  `onprogress` synthesised every 100 ms;
- a copy of Spotify's platform registry (every service whose name ends in `API`);
- webpack's `require` stolen with a fake chunk, then React and its components (Slider, Toggle, menus...) found by
  substrings of their source, with a refusal when two candidates match;
- UI helpers (`Topbar.Button`, `Playbar.Button`, `PopupModal`) that copy Spotify's own classes so they look native;
- `Spicetify.test()`, a self-check that compares the documented members with what exists.

A custom app becomes a fake webpack chunk; the router is patched to add a `/<app>/*` route and a sidebar entry. `watch`
polls files and reloads the page through Chrome DevTools on port 9222.

## What themes and extensions use

From the 18 most used themes and 20 most used extensions and apps:

- every theme has a colour scheme; 80 % of selectors target the main zones (sidebar, play bar, cards, top bar);
- feedback states are everywhere: `:hover` in 17 themes, `:active`/`:focus` in 13, `transition` in 12, `@keyframes`
  in 11; 14 restyle scrollbars, 10 the lyrics;
- big dynamic themes take a background and an accent from the artwork, and build their own settings UI;
- 16 of 20 projects read the playing track, 15 the current page, 10 show notifications, 8 use React, 7 add
  context-menu entries;
- the most starred extensions are full-screen "now playing" views with lyrics.

## Lessons for Ampify

| Lesson | Evidence in Spicetify |
|---|---|
| A stable-names table is versioned data with a regeneration tool | the derivation script came years late |
| One name, one role | the divider and button sharing a name |
| A silent no-match is the main failure | `Replace` is silent |
| A version gate beats best-effort patching | the unmerged allowlist branch |
| One readiness signal | polling everywhere |
| Ship an API self-check | `Spicetify.test()` |
| Live reload with a clear result line | `watch` |
| Keep shims for what the host removed | the compat CSS |
| Isolation is an asset | shared page and `localStorage` in Spicetify |
