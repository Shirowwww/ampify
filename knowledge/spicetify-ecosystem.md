# How people build for Spicetify

A study of the Spicetify ecosystem on 2026-10-31: the Marketplace, the official themes, the most popular community
themes, extensions and apps, and the creator tooling. Read-only clones, nothing executed. The counts come from scripts
run over the clones (18 themes, 786 JS/TS files in 20 projects). Companion page:
[spicetify-internals.md](spicetify-internals.md).

## Size of the ecosystem

GitHub topics on 2026-10-31: `spicetify-extensions` 489 repositories, `spicetify-themes` 193, `spicetify-apps` 74,
`spicetify-snippets` 7. Most starred: the CLI 25.3k, the official themes 6.1k, Beautiful Lyrics 2.4k, Marketplace
1.6k, Comfy 802, Spicy Lyrics 694, Bloom 682, harbassan's apps (Stats) 674, Catppuccin 605, Lucid 496, Hazy 461.

## The Marketplace

A custom app (5,100 lines of TypeScript, 20 languages) plus a small extension that re-applies what was installed at every
Spotify start.

**Discovery.** GitHub search by topic, sorted by stars or by last push (GitHub has no "created" sort, so "newest" means
recently pushed). For each repository it fetches `manifest.json` from `raw.githubusercontent.com`, six at a time in a
worker, cached in `sessionStorage`. Unauthenticated GitHub search allows about 10 requests a minute: "too many requests"
is the first failure users meet. Snippets come from one `snippets.json` in the Marketplace repository, edited by pull
request.

**The manifest authors write** (validated loosely, unknown fields allowed):

| Field | For | Notes |
|---|---|---|
| `name`, `description` | all | required |
| `preview` | all | image path or URL, often a GIF |
| `readme` | all | rendered inside the app |
| `main` | extension | `.js` or `.mjs` |
| `usercss`, `schemes` | theme | CSS and `color.ini` |
| `include` | theme | extension URLs loaded with the theme |
| `branch`, `authors`, `tags` | all | at most 4 tags shown |

A repository may publish an array of manifests: Comfy and Lucid publish one entry per range of Spotify versions
("Legacy, compatible with 1.2.45 to 1.2.60"), each on its own branch. There is no version number, licence, permission,
host range, dependency or changelog field; an item's "version" is the date of its last push.

**Install and load.** Only a metadata record is stored (IndexedDB, formerly `localStorage`); the code is never stored.
At every start, extensions are loaded as `<script>` from jsDelivr (`cdn.jsdelivr.net/gh/<user>/<repo>@<branch>/...`,
with `.com` tried when `.net` is blocked), and theme CSS is fetched and injected as a `<style>`. Code therefore comes live
from a mutable branch, without hash or review. Each change asks for a reload of Spotify, because scripts cannot be
unloaded. Apps are not installed: the card links to the repository.

**Updates, removal, trust.** Updates are checked only when the Installed tab opens, by comparing the push date; the record
is re-saved silently. The blacklist (`blacklist.json`, exact URLs and globs) hides listings but does not stop an installed
item from loading. Popularity is GitHub stars only. The most discussed open issue is "Malicious extension prevention".

**What users see.** Tabs (Extensions, Themes, Snippets, Apps, Installed), cards with preview, authors, tags, stars and
last update, search, seven sort modes, a colour-scheme dropdown for the active theme, a settings dialog (show stars and
tags, hide installed, cycle schemes, colours from the artwork, backup and restore), an in-app snippet editor and a live
`color.ini` editor.

### Against Ampify's catalog

| Capability | Spicetify Marketplace | Ampify |
|---|---|---|
| Source of truth | decentralised topic search | one reviewed `catalog.json` |
| Integrity | none | SHA-256, path checks |
| Permissions | none | shown before install and before enabling |
| Versioning | push date | semver, `kitApi`, `appVersions` |
| Reload after a change | required | not needed |
| Apps | link out | installable |
| Preview image, README, tags, popularity, sort modes | yes | missing |
| Revoking a bad item | listings only | missing |
| In-app snippet editor, live scheme editor, colours from artwork | yes | missing (CLI only for snippets) |

## Themes

**Layout.** A folder with `color.ini` and `user.css` (required), `theme.js` and `assets/` (optional). Big themes are
written in SCSS and ship compiled CSS. The four biggest community themes (Hazy, Comfy, Lucid, Bloom) ship a one-line
`user.css` that imports the real CSS from a CDN, and a `theme.js` that loads a remote script: users get fixes without
reinstalling.

**Size.** The median official theme is about 500 lines of CSS. Whole redesigns run 1,300 to 3,100 lines (Lucid 1,322,
Comfy 2,053, Bloom 2,724, Hazy 3,106).

**Colour keys used** (out of the clones): `main`, `text`, `subtext` in almost every scheme; then `notification`,
`button`, `button-active`, `sidebar`, `player`, `shadow`, `card`, `tab-active`, `misc`, `selected-row`; `play-button` and
`accent` are common non-standard additions. In CSS, `--spice-text`, `--spice-button`, `--spice-main` and `--spice-subtext`
appear in 12 to 14 of the themes, and the `--spice-rgb-*` forms in 12.

### What themes target

1,184 distinct classes, 4,259 occurrences, grouped by area. "Themes" is how many of the 18 touch the area.

| Rank | Area | Themes | Most used selectors |
|---|---|---|---|
| 1 | Top bar, search, history buttons | 18 | `main-topBar-background`, `Root__top-container`, `Root__globalNav` |
| 2 | Sidebar and library | 18 | `Root__nav-bar`, `main-yourLibraryX-entryPoints` |
| 3 | Page header, banner, action bar | 18 | `main-actionBarBackground-background` (the only selector in all 18), `main-entityHeader-backgroundColor` |
| 4 | Transport buttons | 17 | `main-playPauseButton-button`, `main-playButton-PlayButton` |
| 5 | Queue and right panel | 16 | `Root__right-sidebar` |
| 6 | Buttons, toggles, inputs, chips | 16 | `x-toggle-indicatorWrapper`, `main-addButton-button` |
| 7 | Cards, grids, home | 17 | `main-home-homeHeader`, `main-card-card`, `main-cardImage-imageWrapper` |
| 8 | Now-playing widget and cover | 17 | `main-nowPlayingBar-nowPlayingBar`, `main-coverSlotCollapsed-container` |
| 9 | Progress bar and times | 17 | `playback-bar__progress-time-elapsed`, `progress-bar` |
| 10 | Track list rows | 16 | `main-trackList-trackListRow`, `main-trackList-playingIcon` |
| 11 | Dialogs | 12 | `GenericModal` |
| 12 | Context menus and dropdowns | 15 | `main-contextMenu-menuItemButton` |
| 13 | Lyrics | 10 | `lyrics-lyrics-background`, `lyrics-lyricsContent-lyric` |
| 14 | Main view layout | 12 | `Root__main-view` |
| 15 | Scrollbars | 14 | `os-scrollbar-handle` |

Themes concentrate on about 90 selectors, so covering these areas and their states matters more than matching all of
Spotify's 1,470 names.

**Where Ampify's names stand** ([docs/themes/names.md](../docs/themes/names.md)): areas 1 to 4, 7 and 8 are covered.
Partial: states of rows (selected, playing), the parts of the progress bar (track, fill, thumb), the action bar and the
header background, queue rows, generic buttons and toggles, lyrics lines. Missing: context menus, scrollbars.

### CSS the themes use, against Ampify's dialect

| Feature | Themes (of 18) | Ampify |
|---|---|---|
| `var()`, `!important`, margin and padding, `:hover`, `transform`, borders, `:nth-child`, gradients, `>`, opacity, fonts, `z-index` | 12 to 18 | done |
| `border-radius` | 18 | done |
| width and height with min and max | 17 | done |
| `:active`, `:focus` | 13 | missing |
| `:not()` | 12 | missing |
| `transition` | 12 | done for transform, opacity, background |
| `@keyframes`, `animation` | 11 | missing |
| `calc()` | 10 | missing |
| `overflow` | 13 | missing |
| attribute selectors `[x=y]` | 14 | none (no attributes in XAML; names cover the need) |
| scrollbar styling | 14 | missing |
| `::before`, `::after`, `position` | 15 to 17 | no equivalent; grid placement and alignment instead |
| `backdrop-filter` | 7 | `backdrop: acrylic()` |

### What `theme.js` does

- It puts the artwork in the background: Hazy and Bloom draw it on a canvas with brightness and saturation, Lucid
  animates it with WebGL.
- It takes an accent from the cover, with hand-written "too dark / too close to white" checks or Spotify's extracted
  colours.
- It builds a settings panel. Every popular theme has its own (Comfy, Lucid, Hazy), which makes it the largest
  duplicated effort in the ecosystem. Lucid's settings schema (sections, groups, then dropdown, input, toggle, button,
  colour or font controls, each with a visibility condition) is the best design seen.
- It injects elements: Comfy's banner follows the page and the song, Lucid adds a "next song" card.
- It sets CSS variables from the app's state: zoom level, crossfade time, layout values.
- It waits for the app to draw: `waitForElement` is copied in Bloom, Dribbblish and Hazy.
- It compares `Platform.version` to handle several Spotify versions.

## Extensions and apps

**Tooling.** 9 of 17 code repositories build with spicetify-creator, whose README now says it is deprecated ("use
esbuild, Bun, Rollup or webpack/rspack with your own config"); its successor package is archived. Others use their own
esbuild script, or a single plain JS file with a `// NAME: // AUTHOR: // DESCRIPTION:` header. Sizes range from about 300
lines (cat-jam) to 30,000 (Spicy Lyrics).

**APIs actually called** (projects out of 20):

| API | Projects | Ampify |
|---|---|---|
| `Platform` (History 15, PlayerAPI, LibraryAPI, ClipboardAPI, version) | 17 | own pages only; the rest planned |
| `Player` (data 12, events 12, progress 7, seek) | 16 | partial |
| `CosmosAsync` | 12 | planned |
| `ReactComponent` | 10 | done (smaller set) |
| `showNotification`, `PopupModal` | 10 | done |
| `Locale`, `React`, `ReactDOM`, `Config` | 8 to 9 | done |
| `GraphQL` | 7 | no equivalent |
| `Menu`, `ContextMenu` | 7 and 4 | planned, not displayed |
| `URI`, `SVGIcons`, `LocalStorage` | 6 | done |
| `Topbar`, `Queue`, `Keyboard` | 4 to 5 | done or partial |

Most extensions need the playing track and its events, the navigation history, a React UI with dialogs and
notifications, menus, and a data source beyond the playing song. Many extensions also scrape the page: `querySelector` in 153 files,
`MutationObserver` in 44, polling timers in 149.

**Small extensions people love**: hide podcasts (menu toggle and hiding), cat-jam (a GIF synced to the beat), oneko (a
cat that follows the pointer), trashbin (skip disliked songs), loopyLoop (A-B loop), bookmark, full app display. Most
need only the player, a menu entry, storage, or a free overlay layer.

**Settings UI**: a helper library exists (`spcr-settings`) but few use it; big projects build their own.

**Surviving Spotify updates**: the core's css-map, version gates in code, separate forks per host range, polling and
mutation observers, `data-testid` selectors. 7 % of the CLI's closed issues mention "update"; "not working" 6 %, "broken"
3 %.

## Creator experience

| Step | Theme | Extension or app |
|---|---|---|
| Start | copy a folder from the official repository | `npx create-spicetify-app`, or one JS file |
| Find what to style | Chromium DevTools, then look up hashed names in `css-map.json` | same |
| Run | `spicetify config current_theme X`, `spicetify apply` | build, `spicetify config extensions x.js`, `apply` |
| Iterate | `spicetify watch -s` | `npm run watch` plus `spicetify watch -le` |
| Debug | `enable-devtools` (patches a Spotify file) | same |
| Publish | topic plus `manifest.json` | topic plus manifest, built file committed |

Pain points: a deprecated bundler with no documented replacement, thin docs (nothing on `--spice-*` or DevTools in the
themes page), no theme scaffolding, hashed names behind a hand-kept map, invalid manifests silently skipped, no way to
test without Spotify, devtools behind a binary patch.

What Ampify already does better: `ampify new`, `validate`, `test` (fake host with permission checks), `build`
(reproducible zip that prints its catalog entry), `dev` (reload with last-good fallback), `theme check` (refused rules
with line numbers), `types`, `doctor`, permissions with consent, `appVersions`, staying off on an untested Apple Music.

## What creators need, ranked

| # | Capability | Evidence | Ampify |
|---|---|---|---|
| 1 | Colour scheme file and variables | 18 of 18 themes | done |
| 2 | Style the main zones by stable name | areas 1 to 10, 80 % of selectors | done for zones, partial for states and sub-parts |
| 3 | Read the playing track and follow changes | 16 of 20 projects | partial |
| 4 | Artwork background and accent | all big dynamic themes | static done; accent from artwork and animation missing |
| 5 | Feedback states: hover, pressed, focus, transitions, keyframes | `:hover` 17, `:active`/`:focus` 13, `transition` 12, `@keyframes` 11 | hover and transitions done; the rest missing |
| 6 | Navigation: current page, page change, go to a page | 15 of 20 | own pages only |
| 7 | Custom UI: pages, panels, dialogs | `React` in 8, `PopupModal` in 10 | done |
| 8 | Notifications | 10 of 20 | done |
| 9 | Persistent settings | 177 files | done |
| 10 | Ready-made settings UI | every big theme builds one | missing |
| 11 | Context-menu entries | 7 projects | planned |
| 12 | Buttons in the play bar or top bar | 5 projects | done |
| 13 | Library and playlist data | 3 to 5 projects | planned |
| 14 | Data beyond the playing song | 12 projects | planned |
| 15 | Full-screen now playing with lyrics | the most starred extensions | an Ampify view exists; Apple has its own (see apple-music-native-views.md) |
| 16 | Style lyrics lines and background | 10 of 18 themes | missing |
| 17 | A free overlay layer | oneko, cat-jam | missing |
| 18 | Scrollbars and generic controls | 14 of 18 themes | missing |
| 19 | Marketplace metadata: preview, tags, README | every card | missing |
| 20 | Survive host updates | 177 "update" issues | partial: stable names and version gate |
| 21 | Live reload | `watch` | done |
| 22 | TypeScript and JSX with a bundler | 9 of 17 repositories | partial (typings only) |
| 23 | Keyboard shortcuts | 4 projects | done |
| 24 | Audio data for visuals | cat-jam, visualizer | done |
| 25 | Revoke a broken or malicious item | top Marketplace issue | missing |

## Ideas worth taking further

1. Declarative settings in the manifest (toggle, select, slider, colour, font, text), rendered natively by Ampify, stored
   per element, exposed to CSS as variables and to scripts.
2. A pick-to-name inspector: point at an element, see its stable name and the rules that style it.
3. Previews generated by Ampify from the theme itself, so cards never show a stale image.
4. Names as data per Apple Music version, published with `versions.json`, so a new Apple Music needs a data update, not
   a kernel build.
5. A ready CI action for authors that validates, checks names and builds, then prints the catalog entry.
6. Revocation that reaches installed items (a hashed `revoked.json`).
7. A beta channel for authors beside the reviewed default.
8. Theme scaffolding with one commented rule per area.
9. A port assistant for Spotify themes that maps their classes to Ampify names.
10. Shared helpers for themes: set a variable, follow the page, colours from the artwork, an overlay layer.
11. Updates with a badge, a changelog and one-click rollback.
12. "Try this theme for 30 seconds", cheap because Ampify applies without a restart.
