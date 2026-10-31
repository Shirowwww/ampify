# Make a theme in five minutes

You need Apple Music started with Ampify (`ampify launch`) and a terminal open in a working folder.

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

`user.css` targets Apple Music elements by type and name, like CSS: `Grid#PaneRoot` is the grid named `PaneRoot`. The
colors of `colors.ini` are written `$main` or `var(--spice-main)` there.

```css
/* A frosted-glass sidebar. */
Grid#PaneRoot {
    backdrop: acrylic($sidebar, 0.6);
}

/* A gradient on the page background. */
ContentPage {
    background: linear-gradient(135deg, $main 0%, #2A1F45 100%);
}

/* Playback buttons grow on hover. */
TransportControl#TransportControl AppBarButton:hover {
    transform: scale(1.15);
}
```

The most useful elements:

| Selector | Element |
|---|---|
| `Grid#PaneRoot` | The sidebar |
| `Grid#TransportBar` | The playback bar |
| `BackDrop` | The background of the playback bar |
| `NowPlayingControl#LCD > Grid#LCD` | The now playing display |
| `ArtworkImage#ActualArtwork` | The artwork in the bar |
| `TransportControl#TransportControl AppBarButton` | Previous, play, next |
| `SplitView#ContentSplitView > Grid > Grid#ContentRoot` | The page area |
| `ContentPage` | A page (Home, Library...) |
| `TextBlock#AppName` | The "Apple Music" name at the top left |

The full tree is in [knowledge/apple-music-xaml-tree.md](../../knowledge/apple-music-xaml-tree.md). To see the tree of
your own Apple Music version, run `ampify devtools`, then read `logs\inspector.txt` in the Ampify folder.

Three pitfalls:

- A name like `Grid#ContentRoot`, `Border#Root` or `Grid#RootGrid` appears dozens of times in WinUI templates. Always
  target it under a named parent. Otherwise the rule also hits the playback bar or the buttons.
- Do not set a `z-index` below 10 on `Grid#TransportBar`. It would slide under the pages.
- A text size set on every `TextBlock` also overrides page titles.

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

## Reuse a Spicetify theme

```bash
ampify theme import https://github.com/catppuccin/spicetify
ampify theme set catppuccin --scheme mocha
```

The `color.ini` works as it is. Details: [spicetify.md](spicetify.md).
