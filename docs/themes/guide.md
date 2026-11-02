# Make a theme in five minutes

You need Ampify installed, Apple Music open, and a terminal open in a working folder. The themes of the Marketplace
are in [themes/](../../themes): open one next to this guide to see a finished example.

## 1. The starting theme

```bash
ampify new theme my-theme
```

The `my-theme` folder holds three files:

| File | What it does |
|---|---|
| `manifest.json` | The name, author, version and default color scheme |
| `colors.ini` | The colors, one scheme per section (`[Night]` and `[Day]`) |
| `user.css` | Shapes and effects: rounded corners, hover, sizes |

The theme is already complete: a plum background, a coral accent, rounded corners, and artwork that grows on hover.

## 2. See it in Apple Music

```bash
ampify dev my-theme
```

`ampify dev` copies the theme into the Ampify folder, selects it, and reloads it at every save. Keep the terminal open.
Each time you save a file, Apple Music changes within a second, with no restart.

For the other scheme: `ampify theme set my-theme --scheme Day`.

## 3. Change the colors

Open `colors.ini`. The names are Spicetify's:

| Key | What it colors in Apple Music |
|---|---|
| `main` | The page background |
| `sidebar` | The sidebar |
| `player` | The playback bar (at the top in Apple Music) |
| `card` | The now playing display (LCD) in the middle of the bar |
| `text`, `subtext` | Text and secondary text |
| `button` | The accent: buttons and the progress bar |
| `main-elevated`, `highlight` | Buttons and hover |

Write a value in hexadecimal, with or without `#`: `FF7A85`, `#FF7A85`, or `#80FF7A85` with the transparency first. The
light or dark mode of Apple Music follows the color of `main`.

## 4. Change the shapes

`user.css` targets Apple Music elements with stable names written as CSS classes: `.sidebar`, `.playbar`, `.card`.
Ampify keeps the path behind each name up to date when Apple Music changes, so a theme written with them keeps working.
The colors of `colors.ini` are written `$main` or `var(--spice-main)` there.

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

The most useful names:

| Name | Element |
|---|---|
| `.sidebar` | The sidebar |
| `.playbar` | The playback bar |
| `.frost` | The frosted layer behind the playback bar and the page headers |
| `.lcd` | The now playing display |
| `.lcd-artwork-image` | The artwork in the bar |
| `.transport AppBarButton` | Shuffle, previous, play, next, repeat |
| `.play-button` | Play or pause |
| `.content` | The page area |
| `.page` | A page (Home, Library...) |
| `.card`, `.card-title` | A square card (album, playlist, station) and its title |
| `.track-row` | A song in an album or a playlist |
| `.app-name` | The "Apple Music" name at the top left |

All the names are in [names.md](names.md). Under a name, the usual XAML types still work (`.playbar TextBlock`), and so
do the raw paths of the tree (`Grid#PaneRoot`) for the few elements without a name: the full tree is in
[knowledge/apple-music-xaml-tree.md](../../knowledge/apple-music-xaml-tree.md), and `ampify devtools` writes the tree of
your own Apple Music version to `logs\inspector.txt` in the Ampify folder.

Two pitfalls:

- Do not set a `z-index` below 10 on `.playbar`. It would slide under the pages.
- A text size set on every `TextBlock` also overrides page titles: scope it, as in `.track-row TextBlock`.

All properties (colors, gradients, images, acrylic, margins, corners, fonts, transforms, shadows, hover, grid placement)
are in [format.md](format.md).

## 5. Check it and share it

```bash
ampify theme check my-theme
ampify build my-theme
```

`ampify theme check` reads the theme the way the kernel does and reports each refused rule with its line. `ampify build`
checks the manifest, writes the `.zip`, and prints the entry to add to the catalog (see
[the catalog](../extensions/catalog.md)).

## A snippet

A snippet is a small `user.css` that the user switches on alone, on top of any theme, as in Spicetify's Marketplace.

```bash
ampify new snippet my-snippet
ampify dev my-snippet
```

The starter rounds the artwork of the cards (`.card .artwork { corner-radius: 16; }`). It sees the theme's colors and
comes after it. Users switch it with `ampify snippet enable my-snippet` or from the Marketplace.

## Reuse a Spicetify theme

```bash
ampify theme import https://github.com/catppuccin/spicetify
ampify theme set catppuccin --scheme mocha
```

The `color.ini` works as it is. Details: [spicetify.md](spicetify.md).
