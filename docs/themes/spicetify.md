# Spicetify themes in Apple Music

A Spicetify `color.ini` works as it is in Ampify. Put it in a theme folder and its colors dress Apple Music.
`ampify theme import` brings in a whole Spicetify theme with one command.

## How Spicetify works, and what changes here

Spotify on PC is a web page (Chromium). Spicetify edits the app's files to inject the theme's CSS and JavaScript. A
Spicetify theme is:

- `color.ini`: color schemes (`[Base]`, `[Nord]`, `[mocha]`...), each with the same keys (`main`, `sidebar`, `text`...).
  Spicetify turns them into CSS variables `--spice-main`, `--spice-sidebar`...
- `user.css`: CSS rules that target Spotify's HTML classes (`.main-view-container`, `.Root__nav-bar`...).
- Sometimes `theme.js`: JavaScript that runs in the page.

Apple Music for Windows is not a web page. It is a native app (XAML, WinUI 3), with no HTML and no `.main-...` classes.
Ampify therefore reads `color.ini` and translates each key to real Apple Music elements. The `user.css` of a Spicetify
theme cannot apply. It is kept aside, as `spicetify-user.css`, for whoever wants to rewrite it. `theme.js` is kept too
(`spicetify-theme.js`) and never run.

## What each key becomes

| `color.ini` key | Apple Music elements | Fluent brush replaced |
|---|---|---|
| `main` | `ContentPage`, the page area, `Grid#PageRoot`, `NavigationView#NavView` | `SolidBackgroundFillColorBaseBrush`, `ApplicationPageBackgroundThemeBrush` |
| `sidebar` | `Grid#PaneRoot` (sidebar) | |
| `player` (or `sidebar`) | `BackDrop` (background of the playback bar) | |
| `card` (or `main-elevated`) | `NowPlayingControl#LCD > Grid#LCD` (the now playing display) | `CardBackgroundFillColorDefaultBrush` |
| `text` | The sidebar text, the song title | `TextFillColorPrimaryBrush` |
| `subtext` | The artist, elapsed time and duration | `TextFillColorSecondaryBrush` |
| `playback-bar` (or `button`) | The progress bar of the display | |
| `button` | | `AccentFillColorDefaultBrush` |
| `button-active` | | `AccentFillColorSecondaryBrush` |
| `button-disabled` | | `AccentFillColorDisabledBrush` |
| `main-elevated` | | `ControlFillColorDefaultBrush` |
| `highlight` | | `SubtleFillColorSecondaryBrush` |
| `selected-row` | | `SubtleFillColorTertiaryBrush`, and the selected rows and cards (`ListViewItemBackgroundSelected...`, `GridViewItemBackgroundSelected...`) |
| `notification`, `notification-error` | | `SystemFillColorAttentionBrush`, `SystemFillColorCriticalBrush` |

- Spicetify 1 keys (`main_fg`, `main_bg`, `sidebar_and_player_bg`, `slider_bg`...) are read under their current name.
- Any key in the file, known or not (`accent`, `rose`...), becomes a color of the theme: `$accent` or
  `var(--spice-accent)` in `user.css`.
- Page text follows `TextFillColorPrimaryBrush`, not a rule: a rule would also color the captions placed on artwork.
- The light or dark mode of Apple Music follows the color of `main` (light or dark), whatever the scheme name.
- A rule in the theme's `user.css` always beats the `color.ini` translation, even a less specific one.
- A key in PascalCase (`TextFillColorPrimaryBrush`) keeps its usual meaning: it replaces the resource of the same name.
  You can mix both styles in one file.
- Changing colors alone does not change the look of Apple Music much, since the defaults are close to Fluent colors.
  A theme that feels different also changes shapes (see [format.md](format.md)). Catppuccin Latte, for example, looks very
  much like Apple Music's own light mode.

## Import a theme

```bash
ampify theme import <folder | archive.zip | address> [id]
```

| Source | Example |
|---|---|
| A folder | `ampify theme import C:\Downloads\Sleek` |
| An archive | `ampify theme import Sleek.zip` |
| A GitHub repository | `ampify theme import https://github.com/catppuccin/spicetify` |
| A folder in a repository | `ampify theme import https://github.com/spicetify/spicetify-themes/tree/master/Sleek` |

The command looks for the folder that holds `color.ini` or `user.css`. If there are several, it lists them and asks for
the address of the right one. The theme arrives in the Ampify folder with:

- `color.ini`, as it is;
- `spicetify-user.css` and `spicetify-theme.js`, kept aside and never applied;
- `user.css`, almost empty, for your own rules;
- `manifest.json`, with the first scheme as the default;
- `LICENSE` and `README`, copied.

A folder that already has an Ampify theme `manifest.json` is copied as it is. `--yes` replaces a theme with the same id.
An archive goes through the same checks as `ampify install` (paths, symbolic links).

Tried on Apple Music 1.1792: Catppuccin (`mocha` dark, `latte` light) and Sleek (19 schemes, including `Dracula` and
`Nord`).

## What ports when you rewrite the rules

Spotify's selectors never match Apple Music, but most of the CSS inside the rules does, once the selector names an
Apple Music element ([format.md](format.md)):

- `display: none` hides the element, as in the many snippets that hide a part of the interface; `display: flex` and the
  other layout modes show it.
- `border-radius`, `opacity`, `margin`, `padding`, `width`, `height`, `transform`, `transition` and `@keyframes`
  animations of `opacity` and `transform`.
- The text properties: `font-family`, `font-size` (pixels, `%` or `em`), `font-weight`, `font-style`, `letter-spacing`,
  `line-height`, `text-align`, `text-overflow: ellipsis` and `white-space: nowrap`.
- The selectors `A > B`, `A + B`, `A ~ B`, `:first-child`, `:last-child`, `:only-child`, `:nth-child(n)`,
  `:nth-last-child(n)`, `:not(...)`, `:hover`, `:focus` and `:active`.
- `var(--spice-<key>)` for any color of `color.ini`.

A property with no counterpart is ignored, and the theme's problems say what to write instead: `backdrop-filter` becomes
`backdrop: acrylic(tint, opacity)`, `justify-content` and `align-items` become `horizontal-alignment` and
`vertical-alignment` on the children, `gap` becomes a `margin` on the children.

## What does not carry over

- Spotify's selectors: rewrite them with Apple Music's names ([guide.md](guide.md), [format.md](format.md)).
- `filter`, `pointer-events`, `cursor`, `overflow`, `mix-blend-mode`, `clip-path` and `text-transform`: Apple Music's XAML
  has nothing to map them to.
- Colors written `${xrdb:...}` (pywal) are refused, with the line in the theme's problems.
- `theme.js`: Ampify only runs the `main.js` of an Ampify theme, with Ampify's own API (`Ampify`), not Spotify's.
