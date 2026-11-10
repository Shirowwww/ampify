# Your first theme

You need Ampify installed, Apple Music open, and a terminal in a working folder. The finished themes in
[themes/](../../themes) are good examples to keep open next to this page.

## 1. Create it

```bash
ampify new theme my-theme
```

| File | What it holds |
|---|---|
| `manifest.json` | Name, author, version, default color scheme |
| `colors.ini` | The colors, one section per scheme (`[Night]` and `[Day]`) |
| `user.css` | Shapes and effects: corners, hover, sizes |

The starter already works: a plum background, a coral accent, rounded corners, and artwork that grows on hover.

## 2. See it live

```bash
ampify dev my-theme
```

Leave the terminal open. Each time you save, Apple Music changes within a second. To see the other scheme:
`ampify theme set my-theme --scheme Day`.

## 3. Change the colors

`colors.ini` uses Spicetify's names:

| Key | What it colors |
|---|---|
| `main` | The page background |
| `sidebar` | The sidebar |
| `player` | The playback bar (at the top in Apple Music) |
| `card` | The display of the song playing, in the middle of the bar |
| `text`, `subtext` | Text and secondary text |
| `button` | The accent: buttons and the progress bar |
| `main-elevated`, `highlight` | Buttons and hover |

Values are hexadecimal, with or without `#`: `FF7A85`, `#FF7A85`, or `#80FF7A85` with the transparency first. Apple
Music switches to light or dark mode to match the color of `main`.

### Several schemes: one hue, two modes

A theme with many schemes stays easy to keep when each scheme is one hue in one mode, every scheme has the same
keys, and a comment says what each key paints, once, at the top. People pick a hue and its mode in the Marketplace,
or both modes of a hue with a schedule (`theme.schedule = 07:00=Blue Light,19:00=Blue Dark`).

```ini
; main = the page, sidebar = the sidebar, text / subtext = the two text colours (4.5:1 on main),
; button = the accent, main-elevated = the cards and the hover of a row.

[Blue Dark]
main = 1B1E28
sidebar = 161922
text = E6E9F5
subtext = AEB4CC
button = 8EA8F0
main-elevated = 262A38

[Blue Light]
main = F4F6FB
sidebar = E9EDF5
text = 1F2330
subtext = 4E566B
button = 2F5BD3
main-elevated = E2E7F1
```

Name the pair `<Hue> Dark` and `<Hue> Light`. `theme.scheme = auto` takes only the first scheme whose name has
`Dark` or `Night` in dark mode, and `Light` or `Day` in light mode ([Automatic scheme](format.md#automatic-scheme)):
put the pair people should get first. Check every text against
`main` and against `main-elevated`, in both modes: `ampify theme replay` reports each text under 4.5:1
([offline bench](../development/offline-bench.md)).
## 4. Change the shapes

`user.css` targets Apple Music's zones by stable names written as CSS classes. In it, the colors of `colors.ini` are
`$main` or `var(--spice-main)`.

```css
/* A frosted-glass sidebar. */
.sidebar {
    backdrop: acrylic($sidebar, 0.6);
}

/* A gradient on the page background. */
.page {
    background: linear-gradient(135deg, $main 0%, #2A1F45 100%);
}

/* Playback buttons grow on hover. */
.transport AppBarButton:hover {
    transform: scale(1.15);
}
```

The names you will use most:

| Name | Zone |
|---|---|
| `.sidebar` | The sidebar |
| `.playbar` | The playback bar |
| `.lcd`, `.lcd-artwork-image` | The display of the song playing, and its artwork |
| `.transport AppBarButton`, `.play-button` | The playback buttons, and play or pause |
| `.content`, `.page` | The page area, and a page |
| `.card`, `.card-title` | An album, playlist or station card, and its title |
| `.track-row` | A song in an album or a playlist |

Every name is in [names.md](names.md), and every property in [format.md](format.md). For anything without a name,
`ampify devtools` writes the tree of the page on screen to `logs\inspector.txt`, with a selector for each element.

Two traps:

- A `z-index` below 10 on `.playbar` slides it under the pages.
- A text size on every `TextBlock` also changes page titles. Scope it: `.track-row TextBlock`.

People choose where the playback bar goes once, for every theme. If yours plans it at the bottom, write
`"playbar": "bottom"` in `manifest.json` and lay the bar out under `@setting bar-position = Bottom`
([the bar's place](format.md#the-playback-bars-place)). Rows, toggles and greyed buttons take their states as classes
do: `.list-item:selected`, `.shuffle-button:checked`, `.next-button:disabled`.

## 5. Check it and share it

```bash
ampify theme check my-theme
ampify publish my-theme
```

`ampify theme check` reports each refused rule with its line. `ampify publish` sends the theme to the catalog
([publishing](../extensions/catalog.md)). Replace `your-github-username` in `manifest.json` first.

## A snippet

A snippet is a small `user.css` that people switch on by itself, on top of any theme.

```bash
ampify new snippet my-snippet
ampify dev my-snippet
```

The starter rounds the card artwork (`.card .artwork { corner-radius: 16; }`). A snippet sees the theme's colors and
its rules win over the theme's. People switch it on from the Marketplace or with `ampify snippet enable my-snippet`.

## From Spicetify

```bash
ampify theme import https://github.com/catppuccin/spicetify
ampify theme set catppuccin --scheme mocha
```

The colors carry over as they are. See [spicetify.md](spicetify.md).
