# Themes: format and rules

A theme is a folder with at least one of the files below. The format follows section 6.1 of
[the API reference](../extensions/api.md), with a layer of rules on top (`user.css`).

```
my-theme/
├── manifest.json     id, name, version, default scheme, settings (optional)
├── colors.ini        colors per scheme (optional)
├── resources.xaml    XAML resource dictionary (optional)
├── preview.png       picture for the Marketplace card, named by "preview" in the manifest (optional, 2 MB at most)
├── user.css          rules for specific elements (optional)
├── images/           images referenced by url(...)
├── fonts/            fonts referenced by url(...) in font-family
└── templates/        control templates referenced by url(...)
```

The folder name is the theme id. An installed theme lives in `%USERPROFILE%\Ampify\themes\`.

Around themes, the Ampify folder also holds:

```
Ampify/
├── settings.txt        settings (theme.active, theme.scheme, theme.schedule, themes.<id>.<key>, snippets.<id>.enabled...)
├── themes/<id>/        one folder per theme
├── snippets/<id>/      one folder per snippet (see "Snippets")
├── resource-keys.txt   keys allowed in resources.xaml
├── sidebar.json        sidebar: hidden entries and order (optional)
├── home.json           home page: hidden sections and order (optional)
└── logs/               AppleMusic.log, inspector.txt, inspector.json
```

To start, read [guide.md](guide.md) (a theme in five minutes). To reuse a Spicetify theme, read
[spicetify.md](spicetify.md).

## colors.ini

One section per scheme, for example `[Base]` and `[Dark]`. The file can also be named `color.ini`, as in Spicetify.
A lowercase key (`main`, `sidebar`, `text`, `button`...) is a Spicetify color. Ampify translates it to Apple Music
elements (table in [spicetify.md](spicetify.md)), and it is written `$main` or `var(--spice-main)` in `user.css`.
A PascalCase key is the name of an app brush, and it replaces that brush. The value is a hexadecimal color, with or
without `#`: `FA2D48`, `#FA2D48` or `#80FA2D48` (alpha first).

```ini
[Base]
AccentFillColorDefaultBrush = 0A84FF

[Dark]
AccentFillColorDefaultBrush = 0A84FF
SolidBackgroundFillColorBase = 1C1C1E
```

- The scheme named by `defaultScheme` in `manifest.json` is the default, or the first scheme when the manifest names
  none. The setting `theme.scheme` picks another. It can also follow the Windows mode or the time of day: see
  "Automatic scheme".
- The same names work as named colors in `user.css`: `background: $SolidBackgroundFillColorBase;`.
- A name that does not exist in the app is reported in the log ("missing resource") and does nothing else.
- A scheme that gives the accent (`AccentFillColorDefaultBrush`, or Spicetify's `button`) also gives its hover and
  pressed colors, as Fluent does: `AccentFillColorSecondaryBrush` is the accent at 90%, `AccentFillColorTertiaryBrush`
  at 80%. Without them, a button under the pointer showed Apple Music's own accent.
- The accent also reaches WinUI's toggles, sliders, check boxes, radio buttons, progress bars and links (the Settings
  pages are full of them): `ToggleSwitchFillOn`, `SliderTrackValueFill`, `CheckBoxCheckBackgroundFillChecked`,
  `HyperlinkButtonForeground` (from `AccentTextFillColorPrimaryBrush`) and their hover, pressed and disabled keys. WinUI
  sets these from the accent once, when its dictionaries load, so the accent alone left them in Apple Music's blue.
- On an accent where white text reads under 3:1 (a light cyan, Spotify's green, a yellow), the text on it
  (`TextOnAccentFillColorPrimaryBrush` and `...SecondaryBrush`) turns black, as in Fluent's dark mode.
- A key the scheme names itself is never replaced, and neither is one that `resources.xaml` defines (nor what would be
  worked out from an accent it defines).
- An album's artist, which Apple Music paints in its own blue in code, takes the theme's link colour
  (`AccentTextFillColorPrimaryBrush`, else the accent), unless `user.css` colours `.album-artist` itself.
- The tiles of a search's top results, which Apple Music paints grey in code, take the card colour
  (`CardBackgroundFillColorDefaultBrush`), unless `user.css` sets a background on `.search-top-result` itself.
- A theme that rounds the song's display (`.lcd { corner-radius: ... }`) has its progress line stop before the right
  corner, which cut its end off, unless `user.css` sets a margin on `.scrubber` itself.

## resources.xaml

A XAML resource dictionary. Each `x:Key` must be in the list of allowed keys. A single key outside the list refuses the
whole theme, and the log says which one.

The list comes from the inspector: the file `resource-keys.txt` in `%USERPROFILE%\Ampify\` holds one key per line.
Without this file the check is off, and the log says so.

## user.css

A subset of CSS. A rule links selectors to declarations.

```css
/* comment */
Grid#NowPlayingBar {
    background: #1C1C1E;
}

Grid#Bar > TextBlock {
    color: $TextFillColorPrimaryBrush;
    font-size: 18px;
}

#AdvertisingBanner {
    visibility: collapsed !important;
}
```

### Selectors

| Syntax | Target |
|---|---|
| `Grid` | Every `Grid` (short type name) |
| `Microsoft.UI.Xaml.Controls.Grid` | The same type, by full name |
| `#Bar` | The element whose `x:Name` is `Bar` |
| `*` | Every element |
| `A B` | A `B` anywhere inside an `A` |
| `A > B` | A `B` whose direct parent is an `A` |
| `A:hover` | An `A`, while the pointer is over it (see "Hover, press and focus") |
| `A:active` | An `A`, while it is pressed |
| `A:focus`, `A:focus-within` | An `A`, while the keyboard focus is on it or on one of its parts |
| `A:nth-child(2)` | An `A` that is the second child of its parent (counting from 1) |
| `A:first-child`, `A:last-child` | An `A` that is the first, or the last, child of its parent |
| `.sidebar`, `.card`... | A stable name for a zone of Apple Music (see [names.md](names.md)) |

You can combine them: `Grid#Bar > TextBlock`, `Grid#TransportBar Button:hover`,
`Grid#TransportBar > Grid:nth-child(2)`. The structural pseudo-classes reach the many elements Apple Music leaves
unnamed; they count every child of the parent, whatever its type. `:hover`, `:active` and `:focus` go only on the last
element of the selector, one of the three per selector (`Button:active:nth-child(2)` is fine). Attributes (`[x=y]`) and
other pseudo-classes (`:checked`...) are not supported. A rule that contains one is ignored, with a line in the log.

### Priority

When several rules target the same element and property, the most specific wins: a `#name` beats a type, and a type
beats `*`. With equal specificity, the rule written last wins. `!important` beats everything else.

Rules come from several sources, applied in this order: the theme, then each active snippet (in alphabetical order of
their ids), then `sidebar.json` and `home.json`. A later source beats the earlier ones, whatever the specificity of
their selectors. Only `!important` goes ahead: an `!important` rule of the theme beats a normal rule of a snippet. The
full ranking is therefore `!important`, then source, then specificity, then place in the file.

### Variables (`:root` and `var()`)

A `:root` block defines variables. Their names start with `--`. A value reads them with `var(--name)`, or
`var(--name, fallback value)` if the variable may be missing.

```css
:root {
    --background: #1C1C1E;
    --gap: 8px;
    --gaps: var(--gap) 16px;
}

Page {
    background: var(--background);
    padding: var(--gaps);
}
```

- Names are case-insensitive.
- A `:root` block placed lower in the file counts too. Inside `:root`, a variable can use the ones defined before it.
- A missing variable without a fallback makes the declaration ignored, with a line in the problems list.
- In `:root`, only variables are read. A variable written outside `:root` is ignored.
- A snippet sees the variables of the active theme.

`calc()` computes a number from others, once the variables are in: `+`, `-`, `*`, `/` and brackets, on numbers with
or without a unit (`px`, `ms`...). Two lengths can be added or subtracted, not multiplied, and a length cannot be
divided by another.

```css
:root {
    --gap: 12;
}

.content {
    margin: var(--gap) var(--gap) var(--gap) calc(var(--gap) * 2);
}
```

### Theme settings (`--setting-<key>` and `@setting`)

A theme can let people adjust it without editing its files, as Spicetify's Comfy does: it lists settings in
`manifest.json`, with the same `settings` field as an extension ([manifest.md](../extensions/manifest.md#settings)).
The "Ampify" card, in Apple Music's Settings > Playback, shows the settings of the active theme under the extensions.
A change is written to `settings.txt` as `themes.<id>.<key>`, and the theme is applied again at once.

```json
"settings": [
  {"key": "panel-radius", "label": "Panel corners", "type": "number", "min": 0, "max": 24, "default": 12},
  {"key": "hide-app-name", "label": "Hide the Music title", "type": "toggle", "default": false},
  {"key": "play-button", "label": "Play button", "type": "choice", "options": ["White", "Green"], "default": "White"}
]
```

`user.css` reads them in two ways.

Each setting is a variable, `--setting-<key>`, with the value chosen in the card, or the `default` when none is chosen
or when the stored value does not fit (a number out of bounds, a choice that is not an option). A toggle gives `true` or
`false`, a number gives a plain number such as `12`, which a length takes as pixels.

```css
:root {
    --panel-radius: var(--setting-panel-radius);
}
```

A `@setting` block holds rules that apply only while a setting is on, or has a given value:

```css
@setting hide-app-name {
    .app-name { visibility: collapsed; }
}

@setting play-button = Green {
    .play-button > Grid#Root { background: $AccentFillColorDefaultBrush; }
}
```

- `@setting <key> { ... }` applies while the setting is `true`, which is meant for a toggle.
- `@setting <key> = <value> { ... }` applies while the setting has that value, compared without case. The value may
  be quoted: `@setting accent = "Deep pink"`. `@setting hide-app-name = false` applies while the toggle is off.
- The block holds rules, `:root` blocks (variables that change with the setting), `@media` blocks and `@import`. A
  `@setting` block may sit inside `@media`, and the reverse. `@setting` inside `@setting`, and `@keyframes` inside
  `@setting`, are not supported.
- A key the manifest does not declare makes the block ignored, with a line in the problems list.
- A snippet sees the settings of the active theme, as it sees its variables.
- The default values must give the theme's look before settings existed: someone who never opens the card sees no
  change.

When a setting goes off, the values its rules wrote come back to what was under them, as when a rule is deleted
during live editing.

### Properties

| CSS | XAML | Accepted values |
|---|---|---|
| `background` | `Background` | Color, `$name`, `transparent`, `url(images/file.png) [cover\|contain\|fill]`, `artwork`, `linear-gradient(...)` (see "Gradients and images") |
| `backdrop` | `Background` | `acrylic(tint, opacity)`: an acrylic background (see "Acrylic") |
| `color` or `foreground` | `Foreground` | Same as `background` |
| `border-color` | `BorderBrush` | Same as `background` |
| `selected-background` | `SelectedBackground` | A row of a list (`.list-item`) when selected; same values as `background` |
| `selected-hover-background` | `SelectedPointerOverBackground` | The same row, selected and under the pointer |
| `selected-active-background` | `SelectedPressedBackground` | The same row, selected and pressed |
| `border-width` | `BorderThickness` | 1 to 4 lengths, none negative, CSS order: top right bottom left |
| `corner-radius` or `border-radius` | `CornerRadius` | 1 to 4 lengths, none negative, CSS order: top-left, top-right, bottom-right, bottom-left |
| `font-family` | `FontFamily` | A font name, quoted or not, or `url(fonts/file.ttf#Name)` (see "Theme fonts") |
| `font-size` | `FontSize` | Number, `px` optional; or relative to the element's own size, `120%` or `1.2em`, so titles stay above body text |
| `font-weight` | `FontWeight` | `normal`, `bold`, or a number from 1 to 1000 |
| `margin` | `Margin` | 1 to 4 lengths, negative ones allowed |
| `padding` | `Padding` | 1 to 4 lengths, none negative |
| `width`, `height` | `Width`, `Height` | A length or `auto` |
| `min-width`, `min-height` | `MinWidth`, `MinHeight` | A length, or `auto` for no minimum |
| `max-width`, `max-height` | `MaxWidth`, `MaxHeight` | A length, or `none` for no maximum |
| `compact-pane-length` | `CompactPaneLength` | On `.navigation`: the width of the sidebar folded into icons (48 in Apple Music) |
| `opacity` | `Opacity` | A number from 0 to 1 |
| `visibility` | `Visibility` | `visible` or `collapsed` (`hidden` and `none` mean `collapsed`) |
| `template` | `Template` | `url(templates/file.xaml)`, a full `ControlTemplate`. Not applied to a control whose padding or border is negative (Apple Music's search box buttons): WinUI would stop the app |
| `flow-direction` or `direction` | `FlowDirection` | `ltr` (left to right) or `rtl` (right to left); `left-to-right` and `right-to-left` also work |
| `order` | Position (Ampify only) | An integer from 0: the place of the element among its parent's children |
| `horizontal-alignment` | `HorizontalAlignment` | `left`, `center`, `right` or `stretch` |
| `vertical-alignment` | `VerticalAlignment` | `top`, `center`, `bottom` or `stretch` |
| `grid-row`, `grid-column` | `Grid.Row`, `Grid.Column` | An integer from 0 |
| `grid-row-span`, `grid-column-span` | `Grid.RowSpan`, `Grid.ColumnSpan` | An integer from 1 |
| `z-index` | `Canvas.ZIndex` | An integer, possibly negative: the highest element is drawn in front of its neighbors |
| `transform` | `RenderTransform` | `scale(x[, y])`, `rotate(Ndeg)`, `translate(x[, y])`, `none` (see "Transforms") |
| `transform-origin` | `RenderTransformOrigin` | One or two values: percentages, or `left`, `center`, `right`, `top`, `bottom` |
| `box-shadow` | `Shadow` and `Translation` | A depth from 0 to 100 pixels, or `none` (see "Shadows") |
| `transition` | `OpacityTransition`, `BackgroundTransition`, `ScaleTransition`... | `<property> <duration>, ...` for `transform`, `opacity`, `background` or `all`, or `none` (see "Transitions") |
| `animation` | Composition animations of `Opacity`, `Scale`, `Rotation`, `Translation` | `<name> <duration> [easing] [delay] [count] [direction] [fill], ...` running a `@keyframes` block, or `none` (see "Animations") |
| `font-role` | `FontFamily`, `FontWeight`, `FontSize` | A font role, for example `title` (see "Font roles") |

A property that does not exist on an element is refused without harm. The log reports it once. Like all others, these
properties return to their original value when the theme is removed.

### Gradients and images

`linear-gradient` follows CSS: an optional direction, then at least two colors, each with an optional position.

```css
ContentPage {
    background: linear-gradient(135deg, #0E1424 0%, $AccentFillColorDefaultBrush 60%, #2A1F45);
}

Grid#Bar {
    background: linear-gradient(to right, #CC182238, transparent);
}
```

- The direction is an angle in degrees (`0deg` up, `90deg` right, clockwise) or `to top`, `to right`,
  `to bottom right`... Without a direction, the gradient goes down.
- A color is written as elsewhere: `#hex`, `$name` or `transparent`. A position is a percentage from 0 to 100. A missing
  position is placed as in CSS: the first at 0%, the last at 100%, the others evenly spaced.
- The gradient becomes a `LinearGradientBrush` whose points are relative to the element. Its line passes through the
  corners of a square, so on a very elongated element a slanted angle does not land exactly on the corners.
- `color` and `border-color` also accept a gradient.

An image is given with `url(...)`, followed by an optional fit:

| Fit | Effect | XAML |
|---|---|---|
| `cover` | Fills the element and keeps proportions, cropping the image if needed | `Stretch=UniformToFill` |
| `contain` | The whole image, keeping proportions | `Stretch=Uniform` |
| `fill` (or nothing) | Stretches the image to the element's size | `Stretch=Fill` |

The path starts at the theme folder. A path that leaves it (`..`, an absolute path, a drive letter) is refused.

### The artwork of the playing song

`background: artwork` sets the artwork of the song that is playing, as Apple Music shows it in its playback bar. It
changes with the song (within 3 seconds). The fits are those of images (`cover` by default, `contain`, `fill`). Placed
on the window background, under acrylic panels, it gives the blurred background of the Hazy or Comfy Spicetify themes:

```css
Grid#PageRoot { background: artwork cover; }
NavigationView#NavView, ContentPage, Border#Titlebar,
SplitView#ContentSplitView > Grid > Grid#ContentRoot > Border > Grid { background: transparent; }
Grid#PaneRoot { backdrop: acrylic(#0B0B10, 0.62); corner-radius: 18px; margin: 10 6 10 10; }
```

Full example: `themes/artwork`. With no song loaded, the background stays empty.

### The colours of the artwork

Ampify reads the colours of that artwork and gives them as variables, so a theme can follow each song:

| Variable | Value |
|---|---|
| `--art-1` | the most common colour |
| `--art-2` | the most vibrant one, for accents |
| `--art-3` | a dark vibrant one, for backgrounds and glass |
| `--art-text` | `#000000` or `#ffffff`, whichever reads best on `--art-1` |
| `--art-1-hex`, `--art-2-hex`, `--art-3-hex` | the same colours without `#`, to put an alpha in front |

```css
.playbar { backdrop: acrylic(var(--art-3, #0B0B10), 0.7); }
.lcd { background: #33var(--art-2-hex, FFFFFF); }
```

Before the first artwork is read the variables do not exist: give each `var()` a fallback. A theme or snippet whose
`user.css` names `--art-` is applied again when the song changes (within 3 seconds); the others are not. Example:
the `art-tint` setting of `themes/artwork`.

### Layout

The placement properties (`vertical-alignment`, `horizontal-alignment`, `grid-row`, `grid-column`, `margin`,
`z-index`) move Apple Music's zones. One line puts the playback bar at the bottom, like in Spotify:

```css
Grid#TransportBar, BackDrop { vertical-alignment: bottom; }
```

For a whole layout with aligned edges, start from a ready-made one (see "Ready-made layouts" below).

`horizontal-alignment` and `vertical-alignment` place an element in the space its parent gives it. `grid-row`,
`grid-column` and their `-span` variants move a child of a `Grid` to another cell. They are read only on an element whose
direct parent is a `Grid`, and refused elsewhere. `z-index` puts an element in front of or behind its neighbors.

```css
Grid#TransportBar > StackPanel#NowPlayingToggleButtons {
    grid-column: 0;
    horizontal-alignment: left;
}
```

The cells and the number of rows and columns come from the app. The inspector shows `Grid.Row` and `Grid.Column` of
each element. A cell number that does not exist puts the element in the last cell.

### Ready-made layouts

Moving Apple Music's zones by hand takes many rules that must agree with each other, or the edges stop lining up: a
sidebar a few pixels shorter than the pages next to it, a bar that overlaps a panel. Ampify ships layouts tuned once,
which a theme brings in with `@import`:

```css
:root {
    --gap: 10;
    --panel-background: #121212;
}

@import "ampify:floating-panels";
@import "ampify:bottom-bar";

/* The theme's own rules come after, so they win over the layouts. */
.play-button { background: #1ED760; }
```

`@import url(ampify:bottom-bar);` works too. The rules of a layout take the place of the `@import` in the file, so a
rule written after it overrides it at equal specificity. The log names them after their layout
(`ampify:bottom-bar line 12`). Other `@import` statements (a file, a URL) are not supported and are ignored.

| Layout | What it does |
|---|---|
| `ampify:floating-panels` | The sidebar and the pages become two rounded panels on the window's background, with even gaps and aligned bottom edges. The frosted layer goes, and the page tops shrink to the gap. |
| `ampify:bottom-bar` | The playback bar moves to the bottom, across the whole window: the song on the left, its title next to its artwork, the buttons in the middle, volume and toggles at the right end. Both panels stop above it. Import it after `floating-panels`. |

Each layout reads variables that the theme may set in its `:root`, with these defaults:

| Variable | Default | Meaning |
|---|---|---|
| `--window-background` | `#0B0B0D` | The window, around the panels |
| `--panel-background` | `#17171A` | Both panels |
| `--panel-radius` | `10px` | Their corners |
| `--band-background` | `#1E1E22` | `floating-panels`: the band under an album's or a playlist's tracks, which Apple paints grey |
| `--gap` | `8` | The space around the panels and between them |
| `--content-top` | `64` | `floating-panels`: where the pages start, below the playback bar at the top |
| `--compact-pane` | `48` | `floating-panels`: the width of the sidebar folded into icons, as in Apple Music. Wider shows the start of the texts Apple Music hides there |
| `--page-top` | `8`, or `40` with `bottom-bar` | The space above the first line of a page. With the bar at the bottom, it leaves room for the window's buttons. |
| `--bar-height` | `72` | `bottom-bar`: the height of the bar |
| `--bar-room` | `80` | `bottom-bar`: the space the panels leave at the bottom, the bar and a gap |
| `--bar-reach` | `calc(var(--gap) - 290)` | `bottom-bar`: how far left the bar reaches past the page column: the open sidebar's 290 pixels less a gap, so the song's artwork lines up with the sidebar |
| `--bar-reach-folded` | `calc(var(--gap) - var(--compact-pane))` | `bottom-bar`: the same when Apple Music folds the sidebar, at 1324 pixels wide and less |

### Window size (`@media`)

Rules inside `@media` apply only while the window has that size, in the pixels Windows counts for the app (the
inspector writes the current size on its first line). `min-width`, `max-width`, `min-height` and `max-height` are read,
joined by `and`; bounds are included. When the window crosses a bound, the theme follows at once, and a value that no
rule gives any more goes back to Apple Music's.

```css
@media (max-width: 1324px) {
    .lcd {
        visibility: collapsed;
    }
}
```

Apple Music folds its sidebar at 1324 pixels wide and less. Variables cannot be set inside `@media`, and an `@media`
cannot hold another.

### Acrylic

`backdrop: acrylic(tint, opacity)` sets an acrylic background (WinUI 3 `AcrylicBrush`): a blur of what is behind the
element, tinted. The tint opacity goes from 0 to 1.

```css
Grid#PaneRoot {
    backdrop: acrylic($LayerFillColorDefaultBrush, 0.55);
}
```

- `backdrop` and `background` write the same property. The rule that wins (see "Priority") gives the background.
- This is in-app acrylic: it blurs the elements of the window, not the desktop.
- When Windows turns effects off (transparency disabled, battery saver), only the tint shows.

### Transforms

`transform` rotates, scales or moves an element without changing the space it takes: its neighbors do not move.

```css
TransportControl#TransportControl AppBarButton {
    transform-origin: center;
}

TransportControl#TransportControl AppBarButton:hover {
    transform: scale(1.15) rotate(-3deg);
}
```

- Functions: `scale(x)` or `scale(x, y)`, `scaleX`, `scaleY`, `rotate(Ndeg)`, `translate(x, y)` (pixels), `translateX`,
  `translateY`. `none` resets the element.
- They become one `CompositeTransform`, which always applies scale, then rotation, then translation, whatever the
  written order. Two `scale` values multiply. Two `rotate` or two `translate` values add up.
- `transform-origin` gives the fixed point, relative to the element: `center` (or `50% 50%`), `left top`, `right`...
  Without it, XAML starts from the top left corner. Pixels are not accepted.

### Shadows

`box-shadow` takes a single value: the depth, in pixels. Ampify puts a `ThemeShadow` on the element and moves it by this
depth (`Translation`, Z axis): the higher the element, the larger and softer the shadow.

```css
SplitView#ContentSplitView > Grid > Grid#ContentRoot {
    box-shadow: 24px;
}
```

`Grid#ContentRoot` alone would also hit the inner `ContentRoot` of the `NavigationView` (which holds the playback bar)
and that of the playback buttons. A name taken from WinUI templates is always targeted under a named parent.

- `none` or `0` removes the shadow. The full CSS form (`0 4px 8px #000`) is not accepted: color, offset and blur come from
  Windows.
- A `ThemeShadow` is drawn only on what is behind the element in the window. Depending on the WinUI version, it may not
  show outside a popup. Check it on the real app.
- The element's old Z value returns with its original shadow when the theme is removed.

### Hover, press and focus

A rule whose selector ends with `:hover` applies while the pointer is over the element, `:active` while the element is
pressed, and `:focus` while the keyboard focus is on the element or on one of its parts:

```css
Frame#ContentFrame Button {
    transform-origin: center;
}

Frame#ContentFrame Button:hover {
    transform: scale(1.05);
    box-shadow: 16px;
}

Frame#ContentFrame Button:active {
    transform: scale(0.95);
}

.search-area:focus {
    background: #3A1F5C;
}
```

- Each state has its own cascade, with the same priority rules between its rules. While the state holds, its values beat
  those of normal rules, whatever the specificity. When several hold at once, the press beats the focus, and the focus
  beats the hover.
- When a state ends, the element takes back exactly what it had: the value of a state that still holds, the theme's
  value, or the original one.
- A press starts when a pointer button goes down on the element, even on a button that handles the click itself. It ends
  when the button is let go, when the pointer is taken away, or when the pointer leaves the element.
- `:focus-within` is the same as `:focus`: the state holds while the focus is anywhere inside the element, so moving
  from the search box's text to its own buttons keeps it.
- Ampify follows only the events of the states an element uses, once per element: `PointerEntered` and `PointerExited`
  for `:hover`, the pointer being pressed and let go for `:active`, `GotFocus` and `LostFocus` for `:focus`. Removing
  the theme, or a state rule that disappears, stops that and restores the values, even if the state still holds.
- `order` is not possible under a state: the declaration is ignored and reported.
- The change is immediate, unless the element has a `transition` (next section).
- A control that keeps the pointer's entry for itself does not react to `:hover`.

Apple Music's buttons change their own background, text colour and border under the pointer and while pressed. A
`background`, `color` or `border-color` given to a button, or to the root of its template (`.play-button > Grid#Root`),
stays in those two states: Ampify writes the same brush to the keys the button's states read
(`AppBarButtonBackgroundPointerOver`, `AppBarButtonBackgroundPressed`, `ButtonBackgroundPointerOver`...), in that
button's own resources only. A `:hover` colour goes to both states, and an `:active` colour to the pressed one only.
Checked, selected or "repeat on" states keep Apple Music's colours, since they say something. The inspector shows what
each element's states change (`States=` below). A theme gives the selected rows of song lists its own colour through
`colors.ini`: `ListViewItemBackgroundSelected`, `ListViewItemBackgroundSelectedPointerOver` and
`ListViewItemBackgroundSelectedPressed` (the `GridViewItem...` keys for grids of cards).
A theme that names none of them gets Spicetify's `selected-row` there when it gives one, or else its accent at 20,
27 and 33% opacity. Apple Music's own lists never read those keys: Ampify hands them to every row through
`.list-item { selected-background: ...; selected-hover-background: ...; selected-active-background: ...; }`, which a
theme's `user.css` can set itself.

The same goes for the entries of a menu, which also stay lit while their submenu is open, and for a slider: `color`
fills the part left of the thumb, `background` the rest of the track, and its `Thumb` keeps its `background` while
dragged. A `:hover` colour on such a control is what its states show under the pointer: `.menu-item:hover
{ background: #3E3E3E; }` lights each entry of a menu.

### Flyouts, menus and tooltips

Apple Music opens its flyouts (the volume's), its menus and its tooltips in popups, outside the window's tree. Each
popup is a root of its own, `Popup`: a selector reaches what is inside it, and never what opened it.

```css
.menu,
.flyout,
.tooltip {
    background: #282828;
}

.menu-item:hover {
    background: #3E3E3E;
}
```

A popup takes the theme the first time it opens, within a quarter of a second (at once when a click or a key opened
it), then keeps it while it is closed. It follows the light or dark mode of the element that opened it. See
[names.md](names.md) for `.flyout`, `.menu`, `.menu-item`, `.menu-separator`, `.tooltip` and the volume slider.

### Transitions

`transition` animates the changes of an element instead of jumping to the new value: a `:hover`, a theme being applied,
a scheme change.

```css
HoverGrid#SquareLockupContent {
    transition: transform 180ms ease-out, opacity 150ms;
}

HoverGrid#SquareLockupContent:hover {
    transform: scale(1.04);
}
```

- `transform`, `opacity` and `background` can be animated; `all` means the three. Durations are in `ms` or `s`, from 0
  to 5 s. Easing words (`ease`, `linear`, `ease-in-out`...) and delays are accepted and ignored: Ampify uses XAML's own
  quick ease-out.
- Ampify uses XAML's implicit transitions, which run on the compositor thread: they stay smooth even while Apple Music
  is busy. An animated `transform` is shown through the element's `Scale`, `Rotation` and `Translation` around its
  `transform-origin`, instead of `RenderTransform`.
- `background` animates between plain colors only (a gradient, an image or acrylic changes at once).
- Removing the theme gives the element back the transitions it had before.

### Animations

`@keyframes` describes a movement and `animation` runs it, as in CSS:

```css
.lcd-artwork-image {
    animation: spin 20s linear infinite;
}

.play-button:hover {
    animation: breathe 1.2s ease-in-out infinite alternate;
}

@keyframes spin {
    to { transform: rotate(360deg); }
}

@keyframes breathe {
    to { transform: scale(1.08); opacity: .8; }
}
```

- A stop is `from`, `to` or a percentage; several can share a block (`0%, 100% { ... }`). Stops animate `opacity` and
  `transform` (`scale`, `rotate`, `translate`), with `var()` and `calc()` as anywhere else. Any other property is
  reported and skipped: Apple Music's compositor animates these two on its own thread, so an animation stays smooth
  while the app loads a page.
- A property missing from the first or the last stop starts or ends at the element's own value. The parts of a
  transform that stay at `none` in every stop are left as they are.
- `animation` takes, in any order: the name of the block, a duration, an easing (`linear`, `ease`, `ease-in`,
  `ease-out`, `ease-in-out` or `cubic-bezier(x1, y1, x2, y2)`), a delay (the second time), a count (a whole number or
  `infinite`), a direction (`normal`, `reverse`, `alternate`, `alternate-reverse`) and a fill mode (`none`,
  `forwards`, `backwards`, `both`). Up to 4 animations go in one value, separated by commas. `none` stops them.
- Durations and delays go up to 120 s. `steps()`, negative delays and `paused` are refused, with the reason in the
  problems list.
- Scale and rotation turn around the element's `transform-origin`, the centre unless the theme sets one. The centre
  follows the element when its size changes.
- An animation starts when its rule first applies to the element, and under `:hover`, `:active` or `:focus` each time
  the state begins. A finite animation ends on the element's own values, or stays on its last frame with `forwards`.
- `@keyframes` inside `@media` are not read. A snippet can run the `@keyframes` of the active theme.
- Opacity animates on any element. Some elements refuse an animated transform: the ones Apple Music moves itself (many
  artworks), and the ones the theme also gives a `transform`. The log says so once per element; put that transform in
  the stops instead.
- With "Animation effects" turned off in Windows (Settings > Accessibility > Visual effects), animations do not run.
  The setting is read when an animation starts.
- Removing the theme stops every animation and gives each element its values back.

### Theme fonts

`font-family: url(fonts/file.ttf#Family name)` uses a font shipped in the theme folder. The name after `#` is the family
name in the file (visible in the Windows font preview).

```css
:root {
    --font-title: url(fonts/Inter-Bold.ttf#Inter);
}

TextBlock {
    font-family: url("fonts/Inter-Regular.ttf#Inter");
}
```

- Accepted files: `.ttf`, `.otf` and `.ttc`. The path starts at the theme folder, as for images.
- Ampify gives XAML the address of the file (`file:///C:/.../fonts/file.ttf#Name`), as for images. An `ms-appx:///`
  address would point to Apple Music's package, not to the theme.
- With `theme.parts.assets = false`, the fonts of the folder are refused like images.
- Font roles (`--font-<role>`) accept this form too.
- Ampify never changes the font of a text made of icons (private-use glyphs), whatever the rule says. Changing it
  shows empty squares.

### Right-to-left languages

Spicetify has a setting `remove_rtl_rule`, because Spotify's CSS contains rules for Arabic and Hebrew. Apple Music has
no such rules to remove. Instead, `flow-direction` flips an element and everything inside it:

```css
Page {
    flow-direction: rtl;
}
```

`ltr` restores the usual direction. As with any value, the original direction returns when the theme is removed.

### Element order

`order` moves an element among its parent's children: `0` puts it first. Elements without `order` keep their order,
after those that have one. This is not a XAML property: Ampify moves the element in the list of its parent (for example
the entries of a `NavigationView`). A parent that cannot reorder refuses, without harm. The moves are undone, in reverse
order, when the theme is removed. `sidebar.json` uses this.

## Font roles

A font role names the font of a kind of text: title, body text, caption... It is the equivalent of `getFontStyle` in
Spicetify. A theme sets a role in `:root` with three variables, all optional:

```css
:root {
    --font-title: "Segoe UI Variable Display";
    --font-title-weight: bold;
    --font-title-size: 30px;
    --font-body: Inter;
}

TextBlock#PageTitle {
    font-role: title;          /* family, weight and size of the role, at once */
}

TextBlock {
    font-family: var(--font-body);
}
```

- `--font-<role>`: the family, like `font-family`.
- `--font-<role>-weight`: the weight, like `font-weight` (`normal`, `bold` or a number).
- `--font-<role>-size`: the size in pixels, like `font-size` (a role refuses a relative size such as `120%`).

What the theme does not set keeps the Windows 11 value:

| Role | Family | Weight | Size |
|---|---|---|---|
| `caption` | Segoe UI Variable Small | 400 | 12 |
| `body` | Segoe UI Variable Text | 400 | 14 |
| `body-strong` | Segoe UI Variable Text | 600 | 14 |
| `subtitle` | Segoe UI Variable Display | 600 | 20 |
| `title` | Segoe UI Variable Display | 600 | 28 |
| `title-large` | Segoe UI Variable Display | 600 | 40 |
| `display` | Segoe UI Variable Display | 600 | 68 |

A theme can also create its own roles (`--font-lyrics: ...`).

Extensions read the roles of the applied theme (theme and snippets together) with `Ampify.fonts.style(name)` and
`Ampify.fonts.roles()`: `{"title":{"family":"...","weight":700,"size":30,"fromTheme":true},...}`. See
[the extension API](../extensions/api.md), section 5.6.

## Snippets

A snippet is a small piece of style that you switch on alone, as in Spicetify. It goes in
`%USERPROFILE%\Ampify\snippets\<id>\`:

```
square-corners/
├── manifest.json     "kind": "snippet", and "resources": "resources.xaml" when there is one
├── resources.xaml    resource dictionary (optional)
└── user.css          rules, as in a theme (optional)
```

A snippet needs at least one of the two files. Most are a `user.css` alone, as `ampify new snippet` writes.

- Switch it on with `ampify snippet enable <id>`, which writes `snippets.<id>.enabled = 1` in `settings.txt`.
  `ampify snippet disable <id>` switches it off.
- It applies on top of the active theme, or alone if there is no theme. Its rules beat the theme's (see "Priority").
  Several snippets apply in alphabetical order of their ids.
- Its rules see the colors (`$name`), the variables and the font roles of the active theme. A snippet has no
  `colors.ini`: if it has one, it is ignored and reported.
- Its `resources.xaml` follows the same list of allowed keys as themes. A key outside the list refuses the snippet, not
  the theme. The snippet's dictionary is merged after the theme's, so it wins.
- Switching a snippet off puts back what was under it (the theme, or the original value), with no restart.
- A snippet's problems go to the log with its name.
- The theme parts (`theme.parts.*`) do not affect snippets: each snippet is switched on alone.

Example: [`themes/snippets/square-corners/`](../../themes/snippets/square-corners/). `ampify theme check <folder>` also
checks a snippet.

## Automatic scheme

The scheme of `colors.ini` can follow the Windows mode (light or dark), or the time of day.

| Setting | Effect |
|---|---|
| `theme.scheme = Dark` | That scheme |
| `theme.scheme =` (empty) | The theme's `defaultScheme`, or the first scheme of `colors.ini` |
| `theme.scheme = auto` | Follows the Windows mode |
| `theme.schedule = 07:00=Light,19:00=Dark` | Follows the local time; takes priority over `theme.scheme` |

**`auto`.** In dark mode, Ampify takes the first scheme named `Dark` or `Night` (or whose name contains it). In light
mode, it takes `Light` or `Day` (or a name that contains `light`), otherwise the first scheme that is not dark. A theme
with `[Base]` and `[Dark]` therefore works as it is. Without a dark scheme, the first scheme stays, and the log says so.

**`theme.schedule`.** A list of `HH:MM=Scheme` entries separated by commas. The current scheme is the one of the last
time that has passed. Before the first time of the day, the last scheme of the day before continues. A scheme can be
`auto`: `07:00=auto,22:00=Night` follows Windows by day and forces `Night` in the evening. A badly written entry is
ignored and reported.

The switch happens by itself: the kernel re-reads the mode and the time every 3 seconds and reloads the theme when the
scheme changes. `ampify theme set` keeps `theme.schedule`.

## Sidebar and home (`sidebar.json`, `home.json`)

The equivalent of `sidebar_config` and `home_config` in Spicetify: hide entries and change their order. Both files go in
the Ampify folder, next to `settings.txt`. They apply with or without a theme.

```json
{
  "version": 1,
  "reorder": true,
  "items": [
    {"name": "RadioItem", "hidden": false},
    {"name": "ListenNowItem", "hidden": false},
    {"name": "BrowseItem", "hidden": true}
  ]
}
```

- `name` is the `x:Name` of the element. In `sidebar.json`, the rule targets `NavigationViewItem#<name>`. In
  `home.json`, it targets `#<name>`, whatever the type.
- `hidden: true` gives `visibility: collapsed`.
- With `"reorder": true`, the place in the list gives `order`: the first entry comes first. Without `reorder` (or with
  `false`), only hiding applies and the app's order stays. Set `reorder` only if the list is complete.
- These rules come after the theme and the snippets. Problems go to the log (prefix "Layout").

**The real names still have to be read from the app.** The names in the example are made up. Run the inspector
(`theme.inspect = 1`) and read the `NavigationViewItem` entries in `logs\inspector.txt` or `logs\inspector.json`.

**Generator.** `src/core/theme/layout.hpp` provides what is needed to write these files: `NamesMatching(tree,
NavigationViewItem)` lists the entries of the real tree, `GenerateLayout(existing file, names)` keeps the order and state
of entries already listed and adds new ones at the end, visible, and `WriteLayout` writes the text. After an inspection,
the kernel creates `sidebar.json` if it does not exist yet: the full list, nothing hidden, the current order. Then you
only need to set entries to `"hidden": true` or move them in the list.

## Example of a full redesign: Glass

[`themes/glass/`](../../themes/glass/) changes most of the interface: a gradient background, an acrylic
sidebar and playback bar, content in a large rounded card with a shadow, larger text, and buttons that grow on hover.
Its selectors use the names read on Apple Music 1.1792. Those that only target a generic type are marked provisional at
the top of its `user.css`.

## Check a theme

On any PC, without Apple Music:

```
ampify theme check C:\path\to\my-theme [--scheme Dark]
```

The command prints the schemes, each rule kept with its line, then the problems. It exits with code 0 only if the theme is
usable and has no problem.

`ampify theme names <inspector.json>...` checks the stable names against trees saved by the inspector
(see [names.md](names.md)).

## Theme parts

Three settings in `settings.txt` switch off a part of a theme without deleting it. All default to `true`.

| Setting | Effect when `false` |
|---|---|
| `theme.parts.colors` | `colors.ini` writes no color resource. The colors stay usable in `user.css` with `$name`. |
| `theme.parts.resources` | `resources.xaml` and the rules of `user.css` are read, but nothing is applied. |
| `theme.parts.assets` | The images and fonts of the folder are refused. |

A part that is off is still read: problems are reported as usual. A fourth setting, `theme.parts.script`, switches the
theme's optional `main.js` on or off.

## Commands

In a terminal:

```
ampify theme list
ampify theme set <id|none> [--scheme name]
ampify watch <theme-folder> [--scheme name] [--polls N]
```

- `theme list`: each theme of the `themes` folder with its state, usable or refused.
- `theme set`: selects the theme in the settings. A refused theme is not selected, and the settings stay unchanged
  (exit code 1). `none` restores the original colors. If Apple Music is open, the change applies within 3 seconds.
- `watch`: checks the folder at start, then checks again at each file change. Handy for writing a theme while seeing the
  rules that are kept. `--polls N` stops after N checks (useful for tests).

The Ampify folder is `%USERPROFILE%\Ampify`. The option `--kit-dir <folder>` replaces it. Exit code 2 means wrong usage
(missing argument, theme not found).

## Log

In `%USERPROFILE%\Ampify\logs\AppleMusic.log`, the theme engine writes these kinds of lines (the exact wording can
change between versions):

- Theme loaded, with the scheme and the reason for the choice (for example the Windows dark mode).
- Applied: `Grid#Bar Background = #FF1C1C1E (user.css line 3)`, one line per value written. For a snippet, the file is
  `snippets/<id>/user.css`. For the layout, it is `sidebar.json` or `home.json` (the line is then the place in the list).
- Snippet loaded, snippet problems, layout problems: the state and the problems of each source.
- Refused: the element or the value was refused. One line per cause.
- Theme removed, with the number of values restored.

## Inspector

With `theme.inspect = 1`, the kernel writes the interface tree on its next pass, then sets the setting back to 0:

- `logs\inspector.txt`: for reading. The colour resources, then one element per line, with a selector and its values.
  `States=` lists what an element's visual states change, for example
  `CommonStates: PointerOver (ContentPresenter.Background, ContentPresenter.Foreground), Pressed (...)`. WinUI creates
  the states of a control the first time they are used, so a button never hovered may show none.
- The resources listed are the colours and brushes Apple Music and WinUI name (about 870 in Apple Music 1.1792), each
  looked up by its key. The application's dictionaries are not walked: walking them made Apple Music crash at the next
  theme change.
- `logs\inspector.json`: the same tree for tools (function `InspectTreeJson` in `src/core/theme/inspector.hpp`). The command
  `ampify inspect --tree` prints the text version.

```json
{
  "version": 1,
  "truncated": false,
  "resources": [{"key": "AccentFillColorDefaultBrush", "value": "#FF0A84FF"}],
  "nodes": [
    {"index": 0, "id": "1", "type": "Page", "fullType": "", "name": "", "parent": -1, "depth": 0,
     "selector": "Page", "properties": {"Background": "#FF000000"}},
    {"index": 1, "id": "2", "type": "Grid", "fullType": "", "name": "Bar", "parent": 0, "depth": 1,
     "selector": "Grid#Bar", "properties": {"Background": "#FF0000FF"}}
  ]
}
```

`nodes` lists parents before their children. `parent` is the index of the parent in `nodes` (-1 for a root: the
window's content, then each popup seen open). `id` is a string. Templates and objects have the value "present". After
20000 elements the list stops and `truncated` is `true`.

## Windows side

The engine is portable (`src/core/theme/`) and tested on Linux. The Windows service (`src/win/theme_service.cpp`) calls it:

- On its worker thread, a `ThemeWorker` (`src/core/theme/worker.hpp`). At each pass: `SetSystemDark(dark)` with the value
  `AppsUseLightTheme` of `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize` (0 = dark), then
  `Tick(settings, minute)` with the local time in minutes since midnight. The result is the request to send to the UI
  thread. Its generation changes only when a reload is needed.
- On the UI thread, `ThemeSession::Tick` applies the request. After an inspection, the service also writes
  `inspector.json` and creates `sidebar.json` if it is missing.

The XAML backend (`src/win/xaml_backend.cpp`) reads and writes `FlowDirection` (`LeftToRight` or `RightToLeft`) and the
`Order` position (moving the element in `MenuItems` or `Children` of its parent).

For gradients, acrylic, transforms and shadows, the backend creates an object (brush, transform, shadow) and remembers
the value that produced it. As long as the element keeps that object, it reads back as that value, and the next pass
writes nothing. Hover goes through `HoverTracker` (`src/core/theme/hover.hpp`), the same code the tests use. Under the
pointer, the engine reads and writes the "underneath" value without undoing the hover.

## Known limits

- Element names come from the app. They can change with each Apple Music update. Read them with the inspector (setting
  `theme.inspect`) before writing rules.
- A resource read as `StaticResource` keeps its original value until the element is recreated.
- Images and fonts must be in the theme folder.
- Apple Music animates a few elements itself (the play button of the playback bar): a `transition` on them is refused
  with a line in the log, and their `transform` or colour changes apply at once.
- `box-shadow` only sets the depth, and the `ThemeShadow` can stay invisible on some elements. A slanted gradient is exact on a square element.
- The names in `sidebar.json` and `home.json` have not all been read from the real app, and Apple Music's sidebar may
  not be a `NavigationView`. If it is not, `sidebar.json` finds nothing and has no other effect.
- `auto` and `theme.schedule` are re-read every 3 seconds, so the scheme change is not instant. New pages and rows that
  appear while scrolling get the theme within a fraction of a second: Ampify follows page navigation, the lists of the
  page and the popups that open.
