# How Spicetify works inside

A reading of the Spicetify CLI source (`spicetify/cli` at `cf2081f`, release v2.46.4, 2026-10-28) and its docs, done
to learn from its design. Nothing of Spicetify was run. Paths are relative to the `spicetify/cli` repository. Where the
docs and the code disagree, this page follows the code.

Companion page: [spicetify-ecosystem.md](spicetify-ecosystem.md) (how people build themes and extensions, and the
Marketplace).

## From install to apply

Spotify's interface is a web page packed in `Apps/xpui.spa` (a zip). Spicetify never touches the executable, except
for two optional byte flips. It:

1. copies the `.spa` files to `Backup/`;
2. unzips them to `Extracted/Raw/`;
3. runs about 60 regex patches over the JS, CSS and HTML (`src/preprocess/preprocess.go`);
4. copies the result to `Extracted/Themed/` and turns 26 hard-coded colours into `var(--spice-*)`;
5. on `apply`, replaces `Apps/*.spa` with the unzipped folders, which Spotify loads as they are, then writes the theme,
   extensions and apps into them.

The state is read from the layout of `Apps/`, with no state file (`src/status/spotify/spotify.go`): only `.spa` files
means stock, only folders means applied, both means Spotify updated over a modded install, neither means broken.
`[Backup] version` and `with` in the config record which Spotify and which CLI made the backup; a mismatch stops
`apply` with "Preprocessed Spotify data is outdated".

Spicetify refuses to run as administrator (`--bypass-admin` overrides): files written by an elevated process cannot be
read by Spotify running as the user, and the window turns black.

## The patches

| Group | Effect | Why |
|---|---|---|
| css-map | hashed class names become stable names, in CSS and JS alike | stable selectors for themes |
| CSS colours | 26 hex values and their alpha forms become `--spice-*` variables | `color.ini` recolours the whole app |
| JS colours | colours set from JS read the variables | same |
| `disable_sentry`, `disable_ui_logging` | about 30 telemetry functions start with `return;` | privacy |
| `expose_apis` | internal objects are assigned to `Spicetify.*` while the page loads (platform, Redux store, URI, Snackbar, Tippy, context menu provider) | the extension API |
| GraphQL | persisted queries are collected by name into `Spicetify.GraphQL.Definitions` | data access |
| version-gated fixes | `ParseSpotifyVersion().AtLeast()` guards per-version hacks | one CLI for many Spotify builds |
| V8 snapshot | since about 1.2.64 the main bundle lives only in `v8_context_snapshot*.bin`; it is cut out to `xpui-modules.js` | the code to patch moved |
| compat CSS | about 4 KB of rules Spotify removed are appended back | old themes keep working |

`Replace` and `ReplaceOnce` return silently when nothing matches (`src/utils/utils.go`). A patch that no longer matches
gives no warning: this is the main source of "applied but broken" reports (issues #3922, #4034, #4040).

## css-map: the stable-names layer

Ampify's equivalent is its table of stable names ([docs/themes/names.md](../docs/themes/names.md)).

- `css-map.json` is a flat `{hash: name}` object: 3,652 hashes for 1,470 names. 797 names have two or more hashes (up
  to 11): every Spotify version's hash is kept, none is removed. One file serves old and new builds.
- Names carry an area prefix: `main-` (2,333 entries), `x-`, `playlist-`, `view-`, `artist-`, `search-`, `lyrics-`.
- Application is blind text replacement over whole files with `strings.NewReplacer`. It is safe only because hashes
  are long random strings and the same replacer runs on CSS and JS.
- At `backup`, the map is fetched from GitHub (`main`, or the latest patch tag of the CLI's minor), with the copy
  shipped beside the executable as fallback. No signature or hash is checked. Map fixes therefore ship without a CLI
  release.
- Maintenance was manual for years (40+ commits since 2025-09, spikes at each Spotify minor). Since v2.46,
  `scripts/update-cssmap.mjs` proposes new pairs from two extracted versions, using two signals:
  - CSS-module export objects matched by key overlap (exact, or at least 75 % of the smaller set with a unique best);
  - CSS rules with the same selector shape and body, present once in each version.
  Each rejected candidate is recorded with a reason (`missing-runtime-reference`, `duplicate-semantic-in-current-version`
  ...). Any conflict exits 1 and writes nothing. Unknown names are refused: humans name new zones.
- An unmerged branch (`feat/classmap-aware-support`) adds a version allowlist and per-version maps in a separate repo,
  and its comment sums up the lesson: "shipping a module with a stale hash is worse than failing loudly".

Failure modes seen in issues: one name given to two roles (#3949: a divider and a button wrapper shared a name, so the
divider's rule collapsed the buttons), hashes left unmapped, platform-specific builds (#3942, macOS), typos and
duplicates in the union.

## Colours

`color.ini` sections (`[Base]`, `[Dark]`...) hold 18 keys with defaults (`src/utils/color.go`): `text subtext main
main-elevated highlight highlight-elevated sidebar player card shadow selected-row button button-active
button-disabled tab-active notification notification-error misc`. Each becomes `--spice-<key>` and
`--spice-rgb-<key>: r,g,b` in `xpui/colors.css`. Values may be hex, `r,g,b`, `${ENV_VAR}` or, on Linux,
`${xrdb:name:fallback}`. An unknown scheme warns and falls back to the first section.

## Load order

1. After `<body>`: `colors.css`, then `user.css`.
2. `helper/spicetifyWrapper.js`, synchronous, so `window.Spicetify` exists before any deferred script.
3. Helpers (`sidebarConfig.js`, `homeConfig.js`, `expFeatures.js`), then an inline `Spicetify.Config = {...}`.
4. Before `</body>`: `theme.js`, then each extension in config order, then the custom apps' `subfiles_extension`.
   `.mjs` files get `type="module"`.

Extensions can run before Spotify has finished booting. There is no single readiness promise: the docs teach a
`setTimeout` polling loop, and the only signals are the one-shot `Spicetify.Events.platformLoaded` and `webpackLoaded`,
which are not even in `globals.d.ts`.

## The runtime wrapper

`src/jsHelper/spicetifyWrapper/` (about 3,800 lines, bundled by esbuild at release):

| Mechanism | Detail |
|---|---|
| Player facade | exists from the start and calls `Platform.PlayerAPI` lazily; an early call throws on an undefined state instead of saying "not ready" |
| Platform copy | polls every 50 ms for `Spicetify._platform`, copies its keys; `getHistory()` becomes `Platform.History`. For Spotify 1.2.38 and later, walks the platform registry and exposes every service whose name ends in `API` |
| Webpack capture | pushes a fake chunk to steal webpack's `require`, calls `require(id)` on every module, finds React by markers in the factory source |
| Component discovery | by substrings of their source (Slider by `progressBarRef`, Toggle by `onSelected` and `type:"checkbox"`) or by export shape; menu items have four fallback marker sets and refuse when two candidates match |
| Late components | Slider, Dropdown, Toggle and the cards live in lazy chunks; a scanner re-scans with backoff until they exist |
| Player events | polls until the player state exists, hooks its `update` event, diffs the track URI (`songchange`) and paused flag (`onplaypause`); `onprogress` is synthesised every 100 ms |
| Self-test | `Spicetify.test()` compares hand-written member lists with what exists and prints `N/M OK`, errors for missing members, warnings for extras |

UI helpers copy Spotify's own classes so their buttons look native (`Topbar.Button` copies the class string of a
history button), and wait in queues until their host element exists. `CosmosAsync` sends non-Spotify URLs through a CORS
proxy (`cors-proxy.spicetify.app` by default).

## Custom apps

A custom app becomes a fake webpack chunk (`spicetify-routes-<app>.js`) pushed onto the bundler's chunk array. The router
is patched (three regex generations) to add a lazy route `/<app>/*`, and two spots get `Spicetify._renderNavLinks` for the
sidebar entry, built from Spotify's own navigation components. The manifest gives `name` (a string or a per-locale
object), `icon`, `active-icon` (inline SVG), `subfiles`, `subfiles_extension` and `assets` globs. An app runs only when its
route is visited; startup code goes in `subfiles_extension`. When the router patch fails, Spicetify prints a mismatch
message but `apply` still reports success.

## Home, sidebar, flags and `[Patch]`

- `home_config` and `sidebar_config` inject small scripts that reorder or hide sections, stored in `localStorage`.
- `experimental_features` wraps Spotify's feature-flag resolver with an override popup.
- `[Patch]` lets the user add `<file>_find_<n>` regexes with `<file>_repl_<n>` or `_repl_all_<n>`; a pattern not found
  is only a warning.

## Live reload and devtools

- `watch -s|-e|-a|-l` polls files at a fixed interval and re-copies them into `Apps/xpui`. To reload the page it talks to
  Chrome DevTools on port 9222 (`Runtime.evaluate("window.location.reload()")`); if nothing listens, it restarts Spotify
  with `--remote-debugging-port=9222`.
- `enable-devtools` flips two bytes after the `app-developer` marker in `offline.bnk` and aborts if the marker moved.

## The public API

`globals.d.ts` is 2,409 lines. Members and how they are built:

| Member | Built how | Ampify |
|---|---|---|
| `Player` (35 members: play, pause, seek, skipBack/Forward, volume, mute, shuffle, repeat, heart, playUri, progress, events) | thin wrapper on `PlayerAPI` plus Spicetify's own emitter | `Ampify.player`, partial; volume is deliberately absent |
| `Platform` (History, PlayerAPI, LibraryAPI, PlaylistAPI, ClipboardAPI, LocalStorageAPI, PlatformData, Session, ProductStateAPI...) | registry copy | `Ampify.ui.navigate` for Ampify pages only; the rest is planned or has no equivalent |
| `CosmosAsync`, `GraphQL` | wrappers on Spotify's backends | planned (`services.call`) or none |
| `URI`, `Keyboard`, `LocalStorage`, `Config`, `Locale`, `SVGIcons` | patch or Spicetify code | done |
| `React`, `ReactDOM`, `ReactComponent.*` (menus, cards, sliders, toggles, dialogs, router) | found by heuristics | `Ampify.React` and `Ampify.components` (a smaller set) |
| `ReactQuery`, `ReactFlipToolkit`, `ReactJSX`, `ReactDOMServer`, `Tippy`, `classnames`, `Mousetrap` | found by heuristics | not offered |
| `Snackbar`, `showNotification` | Spotify's snackbar | `Ampify.notify` |
| `Topbar.Button`, `Playbar.Button`, `Playbar.Widget` | Spicetify DOM code | done (`Widget` missing) |
| `ContextMenu`, `ContextMenuV2`, `Menu` | patch plus registry rendered as Spotify menu items | planned |
| `PopupModal` | Spicetify's own element | done |
| `AppTitle` | product-state override | partial |
| `colorExtractor`, `extractColorPreset`, `Color` | Spotify backends and GraphQL | partial (`Ampify.color.extract`) |
| `getAudioData` | Spotify's audio-analysis backend | done locally (`Ampify.audio.analyze`) |
| `Events`, `test()` | Spicetify code | `Ampify.ready`; no self-test yet |

`Panel` and `getFontStyle` are documented but absent from the code at this commit.

## Config and commands

`config-xpui.ini` sections: `[Setting]` (paths, `current_theme`, `color_scheme`, the four `inject_*`/`replace_colors`/
`overwrite_assets` switches, `spotify_launch_flags`, `check_spicetify_update`, `always_enable_devtools`),
`[Preprocesses]`, `[AdditionalOptions]` (`extensions`, `custom_apps` as `|` lists, `sidebar_config`, `home_config`,
`experimental_features`), `[Backup]`, `[Patch]`. The parser re-adds missing keys with defaults, and switches off a theme
part whose file is missing.

Commands chain left to right (`spicetify restore backup apply`). Beyond the ones Ampify mirrors: `refresh [-e|-a]`
(re-copy without a full apply), `color <key> <value>` (edit the live `color.ini`), `clear`, `restart`,
`spotify-updates block|unblock` (byte-patches the executable), `update` (self-update, then re-apply: "spicetify
disappeared after Spotify updated, we'll attempt to fix it"). Every state guard prints the exact command to run.

## What breaks after a Spotify update

Every Spotify minor (1.2.86, 1.3.0, 1.3.3 to 1.3.6) needed a CLI release. Classes of breakage from the issues since
2026-09: blank or black window (a patch produced invalid JS), silent partial application, bundler change (webpack to
Rspack renamed the chunk globals), extension UI hooks found by regex, css-map holes, other patchers editing the same
bundle. The user's remedy is always `spicetify restore backup apply`. A Rust rewrite ("v3") is in progress, with a
module store inside the client and self-repair after updates.

## Lessons for Ampify

| # | Lesson | Evidence | Action for Ampify |
|---|---|---|---|
| 1 | A stable-names table is a versioned asset with a regeneration tool | the derivation script came late; per-version maps on a branch | keep `core/theme/builtins.cpp` as the source; add a tool that proposes names for a new Apple Music version from two trees, with rejected candidates and reasons |
| 2 | Derive from two independent signals, require uniqueness, fail on conflict, never invent names | `update-cssmap.mjs` | for XAML: `x:Name` set under a stable ancestor, type path, resource key, automation id |
| 3 | One name, one role, checked by a tool | #3949 | flag a class that matches elements of different types in one tree |
| 4 | Silent no-match is the main failure: count and report resolutions | `Replace` is silent | per theme and version, "matches 41 of 44 rules", in the log, `ampify doctor` and the Marketplace |
| 5 | A version gate beats best-effort patching | the allowlist branch | Ampify has one; add per-version levels (full, names only, off) |
| 6 | Data fetched apart from the binary allows cheap fixes, with a local floor | remote css-map | fetch the names table like `versions.json`, with a SHA-256 check |
| 7 | One readiness promise plus per-subsystem signals | polling everywhere, #3936 | keep `Ampify.ready`; add a typed "not ready" error naming the subsystem |
| 8 | Ship an API self-check | `Spicetify.test()` | `ampify doctor --api`, runnable in CI against the fake host |
| 9 | Discovery of internals needs tiers and a uniqueness guard | `findMenuItem` | resolve Apple's services by name, then by shape, and refuse when several match |
| 10 | Thin compatibility adapters, warn once on deprecated use | `ContextMenu` over V2 | `warnOnce` for deprecated members |
| 11 | Isolation and scoped storage are an asset | shared realm and `localStorage` in Spicetify | document per-element QuickJS and scoped `Ampify.storage` |
| 12 | Live reload: debounced, with a clear result line, never a spin wait | `watch.go` | "reloaded in N ms / failed: reason" |
| 13 | One command heals the state after the host updates | `update`, `auto` | at launch after an Apple Music update, one line with what still works |
| 14 | Keep shims for what the host removed | the 4 KB compat CSS | aliases in the names table, with a "deprecated name" warning |
| 15 | Generate docs and typings from one list | documented members that do not exist | one API list feeding `api.md`, `ampify.d.ts` and the self-check |
