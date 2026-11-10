# Spicetify themes in Apple Music

A Spicetify `color.ini` works in Ampify as it is: put it in a theme folder and its colors dress Apple Music.
`ampify theme import` brings in a whole Spicetify theme in one command.

Apple Music for Windows is a native app (WinUI 3) with no HTML, so the `user.css` of a Spicetify theme, written for
Spotify's classes, cannot apply. Ampify keeps it aside for whoever wants to rewrite it.

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

The command finds the folder that holds `color.ini` or `user.css` (if there are several, it lists them and asks for the
right address). The new theme has `color.ini` as it is, `spicetify-user.css` and `spicetify-theme.js` kept aside and
never run, an almost empty `user.css` for your own rules, a `manifest.json` with the first scheme as default, and the
`LICENSE` and `README`. `--yes` replaces a theme with the same id. Catppuccin and Sleek import cleanly.

## What each key becomes

| `color.ini` key | Apple Music elements | Fluent resource replaced |
|---|---|---|
| `main` | The page area, `Grid#PageRoot`, `NavigationView#NavView` | `SolidBackgroundFillColorBaseBrush`, `ApplicationPageBackgroundThemeBrush` |
| `sidebar` | `Grid#PaneRoot` (the sidebar) | |
| `player` (or `sidebar`) | `BackDrop`, behind the playback bar | |
| `card` (or `main-elevated`) | The display of the song playing | `CardBackgroundFillColorDefaultBrush` |
| `text` | Sidebar text, song title | `TextFillColorPrimaryBrush` |
| `subtext` | Artist, elapsed time, duration | `TextFillColorSecondaryBrush` |
| `playback-bar` (or `button`) | The progress bar of the display | |
| `button`, `button-active`, `button-disabled` | | `AccentFillColorDefaultBrush`, `...SecondaryBrush`, `...DisabledBrush` |
| `main-elevated` | | `ControlFillColorDefaultBrush` |
| `highlight` | | `SubtleFillColorSecondaryBrush` |
| `selected-row` | | `SubtleFillColorTertiaryBrush`, and selected rows and cards |
| `notification`, `notification-error` | | `SystemFillColorAttentionBrush`, `SystemFillColorCriticalBrush` |

- Spicetify 1 keys (`main_fg`, `main_bg`, `sidebar_and_player_bg`...) are read under their current names.
- Every key, known or not, is usable in `user.css` as `$key` or `var(--spice-key)`.
- Apple Music's light or dark mode follows the color of `main`, whatever the scheme's name.
- A rule in your `user.css` always wins over the translation.
- PascalCase keys (`TextFillColorPrimaryBrush`) replace the resource of that name. Both styles mix in one file.
- Colors alone change little, since Apple Music's defaults are close to many palettes. A theme that feels different
  changes shapes too ([format.md](format.md)).

## Rewriting the rules

Spotify's selectors never match Apple Music, but most of the CSS inside the rules does once the selector names an
Apple Music element ([names.md](names.md)):

- `display: none` hides an element, and `display: flex` (or any layout mode) shows it again.
- `border-radius`, `opacity`, `margin`, `padding`, `width`, `height`, `transform`, `transition`, and `@keyframes` on
  `opacity` and `transform`.
- Text properties: `font-family`, `font-size`, `font-weight`, `font-style`, `letter-spacing`, `line-height`,
  `text-align`, `text-overflow: ellipsis`, `white-space: nowrap`.
- Selectors `>`, `+`, `~`, `:first-child`, `:last-child`, `:only-child`, `:nth-child(n)`, `:nth-last-child(n)`,
  `:not(...)`, `:hover`, `:focus`, `:active`.

`backdrop-filter` becomes `backdrop: acrylic(tint, opacity)`, and `justify-content`, `align-items` and `gap` become
alignment or margins on the children. `overflow: hidden` and `clip-path: inset(...)` cut an element to a rectangle
(rounded or shaped clips do not work). `filter` takes `blur()` only. `pointer-events`, `cursor`, `mix-blend-mode`
and `text-transform` have no equivalent. Colors written `${xrdb:...}` (pywal) are refused. `theme.js` does not run:
an Ampify theme can have a `main.js` that uses the `Ampify` API instead.
