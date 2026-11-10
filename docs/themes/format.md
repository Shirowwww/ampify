# Theme format

The reference for themes and snippets. To start, read [Your first theme](guide.md).

```
my-theme/
├── manifest.json     id, name, version, default scheme, settings (see the manifest page)
├── colors.ini        colors per scheme (optional)
├── user.css          rules for Apple Music's elements (optional)
├── resources.xaml    XAML resource dictionary (optional)
├── preview.png       Marketplace picture, named by "preview" in the manifest (optional, 2 MB at most)
├── main.js           a script, run like an extension while the theme is active (optional)
├── images/  fonts/  templates/
```

The folder name is the theme id. Installed themes live in `%USERPROFILE%\Ampify\themes\`. The manifest fields are in
[manifest.md](../extensions/manifest.md).

## colors.ini

One section per scheme, such as `[Base]` and `[Dark]`. The file may also be named `color.ini`, as in Spicetify.

```ini
[Dark]
main = 1C1C1E
button = FA2D48
AccentFillColorDefaultBrush = 0A84FF
```

- A lowercase key (`main`, `sidebar`, `text`, `button`...) is a Spicetify color, translated to Apple Music's elements
  ([table](spicetify.md#what-each-key-becomes)). Any key, known or not, is usable in `user.css` as `$key` or
  `var(--spice-key)`.
- A PascalCase key is the name of an app resource (a Fluent brush or color) and replaces it. It is also usable as
  `$Name` in `user.css`. A name the app does not have is logged as "missing resource".
- `$Name` for a Fluent key the theme does not set (or in a snippet with no theme) is Apple Music's own colour for that
  key, read when the value is written: `$AccentFillColorDefaultBrush` is the accent on screen, with or without a theme.
  It works for a plain colour (`background`, `color`, `border-color`, `::before`), not inside `linear-gradient(...)`
  or `acrylic(...)`, which need a colour of the theme or a hexadecimal value.
- Values are hexadecimal, with or without `#`: `FA2D48`, `#FA2D48`, or `#80FA2D48` (alpha first).
- The default scheme is the manifest's `defaultScheme`, or the first section. The setting `theme.scheme` picks
  another (see [Automatic scheme](#automatic-scheme)).

From the accent (`AccentFillColorDefaultBrush` or Spicetify's `button`), Ampify works out what WinUI would otherwise
leave in Apple Music's blue: the hover and pressed accents (90% and 80%), toggles, sliders, check boxes, links, checked
buttons, and black text on a light accent. It also paints the played part of the progress bar in the accent (the volume
slider already follows it), gives the theme's link color to an album's artist, the card color
to the search's top results, and the accent at low opacity to selected rows. A key the scheme or `resources.xaml` sets
itself is never replaced, and a rule in `user.css` on those elements wins.

## resources.xaml

A XAML resource dictionary. Each `x:Key` must be one of the colour keys Apple Music and WinUI hold (the list
`ampify inspect` reads), or, when `%USERPROFILE%\Ampify\resource-keys.txt` exists, one of the keys listed in it (one
key per line). A single key outside the list refuses the whole theme, and the log names it.

## user.css

A subset of CSS. Selectors name Apple Music's XAML elements, and declarations map to XAML properties.

```css
/* comment */
Grid#NowPlayingBar {
    background: #1C1C1E;
}

.playbar > TextBlock {
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
| `Grid` | Every `Grid` (short type name). `Microsoft.UI.Xaml.Controls.Grid` works too |
| `#Bar` | The element whose `x:Name` is `Bar` |
| `.sidebar`, `.card`... | A stable name for a zone of Apple Music ([names.md](names.md)) |
| `*` | Every element |
| `A B`, `A > B` | A `B` inside an `A`; a `B` whose direct parent is an `A` |
| `A + B`, `A ~ B` | A `B` right after an `A`; a `B` anywhere after an `A` (same parent) |
| `A:hover`, `A:active`, `A:focus`, `A:focus-within` | While the pointer is over it, while pressed, while it or a part of it has the keyboard focus |
| `A:current` | While Apple shows it as the current one: the lyrics line being sung, and the parts of that line (see [The line being sung](#the-line-being-sung)) |
| `A:nth-child(2)`, `A:nth-last-child(2)` | The second child of its parent, from the start or from the end (counting from 1) |
| `A:first-child`, `A:last-child`, `A:only-child` | Position among its parent's children |
| `A:playing` | The list row of the song playing (the one that shows Apple's moving bars), and the elements between that row and the bars: `.list-item:playing` is the row (see [The row playing](#the-row-playing)) |
| `A:checked` | A toggle, check box, radio button or switch that is on (a settings switch, Apple's shuffle); also a button Apple shows on with a lit dot under it (repeat, `.repeat-button:checked`) |
| `A:selected` | A selected list row (`.list-item:selected`) or sidebar entry: the row's presenter, as `.list-item` names it, is selected with its item. A row draws its selection over its background, so `background` on a selected row goes to its selection colours (`selected-background` and the two others) |
| `A:disabled` | A control that takes no input |
| `A:not(B)` | An `A` that does not match `B`, where `B` is one part: a type, a `#name` and the pseudo-classes above |
| `A:has(B)`, `A:has(> B)` | An `A` with an element inside that matches the selector `B` (with `>`, a child of `A`), as in CSS: `StackPanel:has(TileControl)` |
| `A::before`, `A::after` | A decorative image put into `A` (see [Decorative images](#decorative-images)) |

- One state (`:hover`, `:active`, `:focus` or `:current`) per selector, on any part: on an earlier part, it is the
  state of that ancestor (see [A state on an ancestor](#a-state-on-an-ancestor)).
- `:not()` takes no state, no `::before`/`::after`, no combinator and no class.
- `:has()` takes no state and no `::before`/`::after`; it counts what its selector counts, and goes inside `:not()`.
- `:playing`, `:checked`, `:selected` and `:disabled` go on any part and count as pseudo-classes. A toggle or a
  selection that changes restyles at once; a control that turns disabled is restyled at the next pass (a few seconds at
  most). Ampify reads these states only while the theme has a rule that uses one.
- Not supported: attributes (`[x=y]`), `An+B` forms (`odd`, `2n+1`) and other pseudo-classes (`:visited`...). A rule
  that uses one is ignored, with a line in the log.
- Names from WinUI templates (`Grid#RootGrid`, `Border#Root`, `Grid#ContentRoot`) appear many times: always put them
  under a named parent.

### Priority

The ranking is `!important`, then source, then specificity, then place in the file. Sources apply in this order: the
theme, each active snippet (alphabetical), then `sidebar.json` and `home.json`; a later source wins whatever its
specificity. Within a source, `#name` beats a type, a type beats `*`, and the last rule wins a tie. A built-in name
weighs what the path it stands for weighs, without the covers `.artwork` leaves out: `.lcd-artwork-image` beats
`.artwork` wherever each is written.

### Variables and `calc()`

```css
:root {
    --background: #1C1C1E;
    --gap: 12;
}

.content {
    background: var(--background);
    margin: var(--gap) var(--gap) var(--gap) calc(var(--gap) * 2);
}
```

- Variables are defined in `:root` blocks only, names are case-insensitive, and a variable can use the ones before it.
  They are worked out once, when the theme loads: a `:root` inside `@media` is ignored. For a value that changes with
  the window's width, write the rule itself inside `@media`. A `:root` inside `@setting` works, the setting being known
  at load.
- `var(--name, fallback)` gives a fallback. A missing variable without one makes the declaration ignored.
- `calc()` takes `+`, `-`, `*`, `/` and brackets. Lengths can be added or subtracted, not multiplied together. Inside it,
  `min(a, b)` and `max(a, b)` give the smaller or larger of lengths of one unit.
- A snippet sees the variables of the active theme.

### Theme settings

A theme can declare settings in its manifest, with the same `settings` field as an extension
([manifest.md](../extensions/manifest.md#settings)). People change them in Ampify's settings page,
and the theme applies again at once. The `group` and `description` of a setting group them under a heading and
explain them in one line; the page also resets them, and exports or imports them as a file.

```json
"settings": [
  {"key": "panel-radius", "label": "Panel corners", "type": "number", "min": 0, "max": 24, "default": 12},
  {"key": "hide-app-name", "label": "Hide the Music title", "type": "toggle", "default": false},
  {"key": "play-button", "label": "Play button", "type": "choice", "options": ["White", "Green"], "default": "White"}
]
```

```css
:root {
    --panel-radius: var(--setting-panel-radius);
}

@setting hide-app-name {
    .app-name { visibility: collapsed; }
}

@setting play-button = Green {
    .play-button > Grid#Root { background: $AccentFillColorDefaultBrush; }
}
```

- `--setting-<key>` holds the chosen value, or the default. A toggle gives `true` or `false`, a number a plain number
  (pixels in a length).
- `@setting <key> { }` applies while a toggle is on; `@setting <key> = <value> { }` while the setting has that value
  (case-insensitive, quotes allowed); `@setting <key> != <value> { }` while it has any other value.
- A `@setting` block holds rules, `:root`, `@media` and `@import`. It may sit inside `@media` and the reverse, and inside
  another `@setting`: its rules then apply only while every condition around them holds (`@setting bar-position = Top {
  @setting bar-height != 72 { ... } }`). No `@keyframes` inside it.
- The defaults should give the theme's normal look.

### Properties

| CSS | XAML | Values |
|---|---|---|
| `background` | `Background` | Color, `$name`, `transparent`, `url(images/file.png) [cover\|contain\|fill]`, `artwork`, `linear-gradient(...)` |
| `backdrop` | `Background` | `acrylic(tint, opacity)` or `liquid-glass(tint, opacity[, blur[, saturation]])` |
| `color` or `foreground` | `Foreground` | Same as `background` |
| `border-color` | `BorderBrush` | Same as `background` |
| `checked-color` | `CheckedForeground` | A toggle's icon while on (`.shuffle-button`), a color or `$name`. Themes with an accent give it to shuffle |
| `hover-background` | `PointerOverBackground` | A list row (`.list-item`) under the pointer |
| `active-background` | `PressedBackground` | The same row while pressed |
| `selected-background` | `SelectedBackground` | A selected list row (`.list-item`) |
| `selected-hover-background` | `SelectedPointerOverBackground` | The same row under the pointer |
| `selected-active-background` | `SelectedPressedBackground` | The same row while pressed |
| `border-width` | `BorderThickness` | 1 to 4 lengths, not negative (top right bottom left) |
| `corner-radius` or `border-radius` | `CornerRadius` | 1 to 4 lengths, not negative (top-left, top-right, bottom-right, bottom-left). On a button or a hover, keep it under half the height: more turns a wide button into a pill, and a radius past both halves turns a small one into an oval. A `Rectangle` (a slider's track, `.scrubber Rectangle#HorizontalTrackRect`) takes one radius for its four corners, the top-left one |
| `font-family` | `FontFamily` | A font name, or `url(fonts/file.ttf#Name)` |
| `font-size` | `FontSize` | Pixels, or relative to the element's own size (`120%`, `1.2em`) |
| `font-weight` | `FontWeight` | `normal`, `bold`, or 1 to 1000 |
| `font-style` | `FontStyle` | `normal`, `italic` or `oblique` |
| `letter-spacing` | `CharacterSpacing` | `normal`, em (`0.05em`) or pixels, up to 10em or 200px |
| `line-height` | `LineHeight` (text) | `normal`, pixels, or a factor (`1.5`, `150%`, `1.5em`) up to 10 |
| `text-align` | `TextAlignment` (text) | `left`, `center`, `right`, `justify`, `start`, `end`. Also on the word-by-word lyrics (`.lyrics-words`, `.lyrics-background-words`), which Apple draws without a text element: each wrapped row moves to the centre or the right; `left` and `justify` give Apple's own placement back |
| `text-overflow` | `TextTrimming` (text) | `ellipsis` or `clip` |
| `white-space` | `TextWrapping` (text) | `nowrap` or `normal` |
| `margin` | `Margin` | 1 to 4 lengths, negative allowed. A negative margin grows an element only when it has no `width` of its own: with one, it keeps that size and only moves |
| `padding` | `Padding` | 1 to 4 lengths, not negative |
| `width`, `height` | `Width`, `Height` | A length or `auto`. On an artwork (`.artwork`), a size above Apple's own cap (270 at the top of an album page) lifts that cap, so the cover is not cut. On a side pane (`.sidebar`, `.lyrics-panel`), `auto` is the pane's open length less its left and right margins, so it fits its column |
| `min-width`, `min-height` | `MinWidth`, `MinHeight` | A length, or `auto` |
| `max-width`, `max-height` | `MaxWidth`, `MaxHeight` | A length, or `none` |
| `compact-pane-length` | `CompactPaneLength` | On `.navigation`: the width of the folded sidebar (48 in Apple Music) |
| `track-height`, `track-radius` | `TrackHeight`, `TrackRectRadius` | On `.scrubber`: properties of Apple's progress line itself, the height and corner radius its template gives the track. A length, not negative and not `auto`. Unlike `height`, they leave the control's place in the display alone. A track thicker than 2 pixels, set here or by `height` on `.scrubber Rectangle#HorizontalTrackRect` and `#HorizontalDecreaseRect`, makes Ampify grow the line's slot upwards so the whole track shows and its bottom stays in place (checked from 1 to 6 pixels with the rectangles' `height`); the elapsed and remaining times above it rise by as much as the track did, unless the theme gives them a `transform` of its own |
| `thumb-visibility` | `ThumbVisibility` | On `.scrubber`: `visible` or `collapsed`, the line's thumb (Apple hides it in the play bar). Not yet checked on screen |
| `opacity` | `Opacity` | 0 to 1. On `.sidebar` or `.sidebar-column`, it holds only while the sidebar does not stand over the pages: open over them (under the pointer, or pinned in a narrow window), the pane stays opaque |
| `visibility` | `Visibility` | `visible` or `collapsed` (`hidden` and `none` mean `collapsed`). `visible` holds over Apple's visual states that collapse the element or fade it out (the play bar's times, shown only under the pointer) |
| `display` | `Visibility` | `none` hides; `block`, `flex`, `grid`... show (use them to undo a `display: none`) |
| `template` | `Template` | `url(templates/file.xaml)`, a full `ControlTemplate`. Skipped on a control with a negative padding or border |
| `flow-direction` or `direction` | `FlowDirection` | `ltr` or `rtl` |
| `color-scheme` | `RequestedTheme` | `light`, `dark` or `normal`: the element and everything inside it draw in that mode |
| `order` | Position | An integer from 0: the place among the parent's children |
| `move-to` | Move | A selector, quoted or not, of the element to move into, such as `"Grid#TransportBar > Grid:nth-child(1)"`: the element goes into the first one it matches, at the end of its children. Not under `:hover`, `:focus` or `:active`. It is undone with the theme, and redone when Apple Music rebuilds the element (the move is refused, and logged, when nothing matches) |
| `horizontal-alignment` | `HorizontalAlignment` | `left`, `center`, `right` or `stretch` |
| `vertical-alignment` | `VerticalAlignment` | `top`, `center`, `bottom` or `stretch` |
| `grid-row`, `grid-column` | `Grid.Row`, `Grid.Column` | An integer from 0 (direct children of a `Grid` only) |
| `grid-row-span`, `grid-column-span` | `Grid.RowSpan`, `Grid.ColumnSpan` | An integer from 1 |
| `z-index` | `Canvas.ZIndex` | An integer; the highest is drawn in front |
| `transform` | `RenderTransform` | `scale(x[, y])`, `scaleX`, `scaleY`, `rotate(Ndeg)`, `translate(x[, y])`, `translateX`, `translateY`, `none` |
| `transform-origin` | `RenderTransformOrigin` | Percentages or `left`, `center`, `right`, `top`, `bottom` (no pixels) |
| `box-shadow` | `Shadow`, `Translation` | A depth from 0 to 100 pixels, or `none` |
| `filter` | The element drawn blurred or recoloured | `blur(Npx)` from 0 to 20 pixels, optionally followed by `edges(N% [N%])` (up to 50% each); `saturate`, `grayscale`, `sepia`, `brightness`, `contrast`, `invert`, `hue-rotate`, `tint(color[, amount])`, `duotone(dark, light)`; or `none` |
| `overflow` | `Clip` | `hidden` (or `clip`): the element and its children are cut to its own rectangle; `visible` |
| `clip-path` | `Clip` | `inset(top [right [bottom [left]]])` in pixels, in CSS order (negative reaches past the edges), or `none` |
| `transition` | Implicit transitions | `<property> <duration>, ...` for `transform`, `opacity`, `background`, `filter`, `width` or `all`; or `none` |
| `animation` | Composition animations | Runs a `@keyframes` block (see [Animations](#animations)) |
| `font-role` | `FontFamily`, `FontWeight`, `FontSize` | A font role, such as `title` |
| `content` | Image of `::before`/`::after` | `url(images/file.gif) [cover\|contain\|fill]` or `none` |

- "(text)" properties exist on a `TextBlock` only; `font-style` and `letter-spacing` also work on a control.
- A property an element does not have is refused once, in the log, without harm. Everything comes back to its
  original value when the theme is removed.
- A panel (`Grid`, `StackPanel`) has no colour or font: `color`, `font-family`, `font-size`, `font-weight`,
  `font-style`, `letter-spacing`, `line-height` and `text-align` on a panel go to the texts inside it, as CSS inherits
  them. `.shelf-title { font-family: Georgia; }` reaches the title's texts, while a `background` or a `width` stays on
  the panel. A text's own rule wins, the nearest panel wins over those around it, and a control with its own rule for
  the property keeps it for what it holds. Values under the pointer (`:hover`) are not passed down.
- An unknown property is ignored, and the log suggests the equivalent when there is one: `background-color` →
  `background`, `backdrop-filter` → `backdrop`, `justify-content`/`align-items` → alignment on the children, `gap` →
  `margin` on the children, `border` → `border-width` and `border-color`, `overflow-x`/`overflow-y` → `overflow` or
  `clip-path: inset(...)`. `pointer-events`, `cursor`, `user-select`, `mix-blend-mode` and `text-transform`
  have no equivalent.

### Gradients, images and acrylic

```css
.content { background: linear-gradient(135deg, #0E1424 0%, $AccentFillColorDefaultBrush 60%, #2A1F45); }
.sidebar { backdrop: acrylic($LayerFillColorDefaultBrush, 0.55); }
.window  { background: url(images/stars.png) cover; }
```

- `linear-gradient` takes an optional direction (an angle such as `90deg`, or `to right`) and at least two colors, each
  with an optional percentage. Without a direction it goes down. `color` and `border-color` accept it too. As in CSS,
  the angle holds on the element as it shows, whatever its shape, and the line runs from corner to corner.
- Image fits: `cover` (fills, crops), `contain` (whole image), `fill` or nothing (stretches). Paths start at the theme
  folder and cannot leave it.
- `acrylic(tint, opacity)` blurs the window's elements behind (not the desktop). With transparency effects off in
  Windows, only the tint shows.
- `liquid-glass(tint, opacity[, blur[, saturation]])`, Ampify's own, is a lighter glass: what lies behind is blurred
  by `blur` (12px when left out, up to 40px), its colours made `saturation` times more vivid (1.8 when left out, 0 to
  4; 1 keeps them), under the tint at `opacity`, with a sheen of light along the top edge and a faint rim at the
  bottom. It eases in over a third of a second when the pane appears or the rule starts to apply (a `:hover` rule,
  say). A `border` of a light colour, such as `rgba(255, 255, 255, 0.25)`, completes the rim. It paints any panel,
  border or control, whatever its children. Nothing bends what lies behind (no refraction), and over Apple's own
  blurred full-screen background it blurs that background again: keep it to the main window's panes.

### The artwork of the playing song

`background: artwork` shows the current song's artwork and follows it as soon as it loads. On the window, under acrylic
panels, it gives the blurred background of Spicetify's Hazy or Comfy themes. Example: `themes/artwork`.

`artwork` takes a fit: `cover` (the default: fills the element and cuts the edges), `contain` (the whole picture) or
`fill` (stretched). As the `content` of a [decorative image](#decorative-images) it becomes a layer that can move:

```css
.window::before {
    content: artwork cover;
    transform-origin: center;
    animation: drift 90s linear infinite;  /* the artwork turns slowly behind the panels */
}
```

Example: the Moving artwork setting of [`themes/glass/`](../../themes/glass/).

On a new song, a `content: artwork` layer cross-fades: the previous cover fades out over the new one in 500 ms. In eco
glass (`theme.glass = eco`) or with Windows' animation effects off, it changes at once. A `background: artwork` changes
at once.

Ampify also gives the artwork's colors as variables. Before the first artwork they do not exist, so give a fallback.

| Variable | Value |
|---|---|
| `--art-1` | The most common color |
| `--art-2` | The most vibrant one, for accents |
| `--art-3` | A dark vibrant one, for backgrounds and glass |
| `--art-text` | `#000000` or `#ffffff`, whichever reads best on `--art-1` |
| `--art-1-hex`, `--art-2-hex`, `--art-3-hex` | The same without `#`, to put an alpha in front |
| `--art-luma` | How light the artwork is, from `0.00` (black) to `1.00` (white), a plain number |
| `--art-sat` | How coloured it is, from `0.00` (grey) to `1.00` (full colour), a plain number |

```css
.playbar { backdrop: acrylic(var(--art-3, #0B0B10), 0.7); }
.lcd { background: #33var(--art-2-hex, FFFFFF); }
/* A light cover gets a black veil, so white text still reads: the veil grows with --art-luma. */
.playbar { backdrop: acrylic(#000000, var(--art-luma, 0.5)); }
```

The engine does no arithmetic on these numbers: a value takes one as it is, as above. Without a cover, `--art-luma`
and `--art-sat` have no value, so give a fallback.

A cover that is almost all black (or white), with no real colour in it, gives greys for `--art-2` and `--art-3`
rather than a colour made up from the noise of its darkest pixels.

Snippets get the artwork's colours too, even with no theme active. A snippet using `var(--art-1)` (or `--page-art-1`)
works on its own, as the glow of the [`album-colors`](../../themes/snippets/album-colors/) snippet does.

The album or playlist on screen gives the same five colours of its own artwork, read from its page's header, with the
prefix `page-`: `--page-art-1`, `--page-art-2`, `--page-art-3`, `--page-art-text` and their `-hex` forms. They change
when another album or playlist opens, and keep the last one's colours elsewhere, so use them on `.list-page` and the
names inside it. Example: the [`album-colors`](../../themes/snippets/album-colors/) snippet.

```css
.list-page { background: linear-gradient(to bottom, #FFvar(--page-art-3-hex, 000000), #00var(--page-art-3-hex, 000000) 75%); }
```

### The window's material

`--window-material` in `:root` gives Apple Music's window one of Windows 11's materials: `mica` (the desktop's
colours, faint, as in Windows' Settings), `mica-alt` (darker, for tabs and title bars) or `acrylic` (the blur of the
windows behind). It shows wherever the theme leaves the window and its panels see-through, so set
`--window-background: transparent` with `floating-panels`. `none`, or no variable, keeps Apple's own window. On a
Windows that has no such material, Windows paints a plain colour instead. Example: [`themes/fluent/`](../../themes/fluent/).

### Decorative images

`::before` and `::after` add an image from the theme folder to a panel, behind (`::before`) or on top of (`::after`)
its children. Animated GIFs play.

```css
.lcd::before {
    content: url("images/dance.gif");
    width: 36px;
    height: 36px;
    horizontal-alignment: right;
    margin: 0 44 0 0;
    opacity: 0.55;
}
```

- Panels only (`Grid`, `StackPanel`...). `.lcd`, `.sidebar`, `.content` and `.lyrics-panel` are good hosts.
- Files: png, jpg, gif, bmp, webp or ico from the folder. No web address, no SVG, no text content, no state
  (`:hover::after` is refused).
- The image never takes the pointer. Give a `width` and `height`, or it takes the panel's size.
- Properties that apply: size and min/max sizes, `margin`, alignments, grid placement, `z-index`, `opacity`,
  `visibility`, `transform`, `transform-origin`, `animation` and `transition` (it animates a change of values while
  the picture stays the same).
- Selectors and the inspector do not see these images.

Example: [`themes/snippets/spinning-artwork/`](../../themes/snippets/spinning-artwork/).

### Bars that follow the music

`content: bars(n)` puts `n` bars (2 to 32, 12 by default) in place of an image. They move with the song that plays and
rest low while it is paused. `color` paints them (a colour, a gradient or `var(--art-2)`); the system accent by
default. The other properties of `::before` and `::after` apply.

```css
.lcd::before {
    content: url("images/dance.gif");  /* shown by Ampify versions before bars() */
    content: bars(10);
    color: var(--art-2, #FFFA2D48);
    width: 44px;
    height: 26px;
    horizontal-alignment: right;
    margin: 0 44 0 0;
}
```

After the count come, in any order, a shape, how high the bars may reach (10% to 100% of the decor's height, 100% by
default) and their width: `bars(16, line, 60%)`, `bars(10, mirror, thick)`. `thin`, the default, keeps each bar a few
pixels wide, centred in its column, so that bars across a wide decor do not read as grey blocks; `thick` makes them as
wide as their column, as before Ampify 1.3. A ring and a line keep their own thickness. When only the width changes
(a setting switched), the bars stay in place and ease to their new width.

| Shape | What it draws |
| --- | --- |
| `bars` | Bars side by side, rising from the bottom (the default) |
| `line` | One smooth line through the tops of the bars, `color` its stroke |
| `circle` | A ring of bars pointing outwards from the middle of the decor; give it a square size |
| `mirror` | Bars growing up and down from the middle, the low notes in the centre and the high ones at both ends |

Bars move by their transform only, and the line within its own fixed size, so the panel around them is never laid out
again while they play. Ampify versions that do not know a shape
skip the whole declaration: put a plain `content: bars(n)` (or an image) before it.

The sound is analysed only while bars are on screen. With Windows' animation effects off, they stand still. The
Animated equalizer snippet is built on this same `content: bars()`, with its places and shapes as settings:
[`themes/snippets/display-equalizer/`](../../themes/snippets/display-equalizer/).

### An image that follows the song's progress

Apple's progress bar (`.scrubber`) is a slider whose thumb is hidden. Its track, `Grid#HorizontalTemplate`, keeps the
played part in column 0 and the thumb in column 1, right after it. An image put in column 1 rides at the song's
position (a Nyan Cat, a Kirby). Side margins as large as half its width keep it centred on that point without taking
room from the bar; top and bottom margins sit it on the line, since the display cuts what hangs below its edge:

```css
.scrubber Grid#HorizontalTemplate::after {
    content: url("images/cat.gif") contain;
    grid-column: 1;
    width: 28px;
    height: 18px;
    margin: -15px -14px -3px;
}
```

It moves when Apple moves the bar: as the song plays, and on a seek. Apple's line is only 6 high and cuts what it
holds to that: when an image goes into it, Ampify makes it as high as its template asks (18), or as high as the image
takes in its column (its `height` plus its top and bottom margins) when that is more, and moves it down by what it
gained, so the line stays where it was and the image shows whole. Negative margins, as above, take no room. A theme
that sets the line's own `height` or `transform` keeps them. At the very start
and end of a song, the half of the image past the bar is cut by the display around it.

A `::before` or `::after` whose picture stays the same while its values change (its `transform`, its size) is updated
in place, not made again, so a `transition` declared on it animates the change. The Progress line snippet grows its
mascot that way between its two sizes (`transform: scale(...)` with `transition: transform 200ms`):
[`themes/snippets/progress-line/`](../../themes/snippets/progress-line/).

### The row playing

`:playing` marks the list row of the song playing, in the lists where Apple shows its moving bars next to it (Library >
Songs, for one). The row draws its colour on the `HoverGrid` inside it:

```css
.list-item:playing > * > HoverGrid { background: #FF7A1F4A; }
.list-item:playing TextBlock { color: #FFFFD24A; }
```

The mark moves with the song, within the theme's next pass, and leaves the row it was on with its own colours.

### Layout and ready-made layouts

Alignment, grid placement, `margin` and `z-index` move Apple Music's zones. One line puts the playback bar at the
bottom:

```css
Grid#TransportBar, BackDrop { vertical-alignment: bottom; }
```

For a whole layout whose edges line up, import a ready-made one, then add your own rules after it:

```css
:root { --gap: 10; --panel-background: #121212; }

@import "ampify:floating-panels";
@import "ampify:bottom-bar";

.play-button { background: #1ED760; }
```

| Layout | What it does |
|---|---|
| `ampify:floating-panels` | The sidebar and the pages become two rounded panels on the window's background, with even gaps |
| `ampify:bottom-bar` | The playback bar goes to the bottom across the window: the song left, buttons in the middle of the window, volume right. It hides the column Apple keeps for the window buttons, so a theme needs no rule of its own for it. Import it after `floating-panels` |
| `ampify:centre-bar` | `bottom-bar` with the buttons centred and the progress line under them, the elapsed time at its left end (the length stays hidden, as `bottom-bar` keeps it). The line and the time move into the middle cell with `move-to`. Import it after `bottom-bar` |
| `ampify:scrollbars` | Scroll bars inside their panel, clear of its rounded corners, without Windows' arrows, out of sight until the pointer reaches them. Part of `floating-panels` |

| Variable | Default | Meaning |
|---|---|---|
| `--window-background` | `#0B0B0D` | The window around the panels |
| `--panel-background` | `#17171A` | Both panels |
| `--panel-radius` | `10px` | Their corners |
| `--band-background` | `#1E1E22` | `floating-panels`: the band under an album's tracks |
| `--gap` | `8` | Space around and between the panels |
| `--content-top` | `64` and two gaps | `floating-panels`: where the pages start, a gap under the bar at the top |
| `--bar-top-end` | `150` | `floating-panels`: the room the bar leaves at the top right for the window's buttons |
| `--compact-pane` | `48` | `floating-panels`: width of the folded sidebar |
| `--artwork-veil` | none (Apple's opaque veil) | Strength, 0 to 1, of the veil over the bar's artwork under the pointer: lower, the artwork (and a zoom on `.lcd-artwork-picture`) shows through it, its icon still white |
| `--bar-band-reach` | `--compact-pane`, `0` without `floating-panels` | How far a band painted edge to edge at the top (Artwork colours, glow Bar) reaches back under the floating column, to the window's left edge |
| `--page-top` | `8`, or `40` with `bottom-bar` | Space above a page's first line |
| `--bar-height` | `72` | `bottom-bar`: height of the bar |
| `--bar-room` | `--bar-height` + two `--gap` | `bottom-bar`: space the panels leave at the bottom (the bar stands a gap above the window's foot, and a gap under the panels) |
| `--bar-reach` | `calc(var(--gap) - 290)` | `bottom-bar`: how far left the bar reaches past the page column |
| `--bar-reach-folded` | `calc(var(--gap) - var(--compact-pane))` | `bottom-bar`: the same with the sidebar folded |
| `--bar-reach-end` | `0` | `bottom-bar`: how far right the bar reaches past the page column, for a sidebar on the right |
| `--line-inset` | the song's width (`--song-width`... at each step), a gap and 16 | `centre-bar`: space from the cell's edges to the times; the line starts `--time-room` further in, on both sides, so it stays clear of the song and centred under the buttons |
| `--time-room` | `44` | `centre-bar`: width kept for each time at the ends of the line |
| `--bar-reach-end-folded` | `0` | `bottom-bar`: the same with the sidebar folded |
| `--caption-room` | `40` | `bottom-bar`: the strip the panels leave at the top for the window's buttons |
| `--sidebar-top` | `var(--caption-room)` | `bottom-bar`: where the sidebar starts. The back arrow and the folded menu button follow it |
| `--song-width` | `560` | `bottom-bar`: width of the song (artwork, title, progress line), a clear space short of the buttons |
| `--song-width-medium`, `-folded`, `-narrow`, `-small` | `460`, `380`, `340`, `270` | The same in a window 1600, 1323, 1100 and 960 pixels wide (1323: the window's width, whether the sidebar is folded or not) or less |

`@import url(ampify:bottom-bar);` works too, in a theme or in a snippet. Other `@import` targets (files, URLs) are ignored.

What the layouts take care of, so that a theme needs no rule for it:

- `floating-panels`: with the bar at the top, Ampify's own buttons (an extension's, the volume in percent) never push
  the volume icon out of its column: the toggles stand against the window's buttons, and the song keeps a clear space
  before the volume icon even in the narrowest window, whatever the theme's `--gap`. A shelf's previous and next
  arrows stand on a grey tile that reads on dark and light pages. In a window 1100 pixels wide or less, the display
  shows only the time left under the pointer.
- `bottom-bar`: the progress line runs from the artwork to the display's end, and the elapsed and remaining times
  have their place at the end of the artist's line, not over the artist. Apple Music shows those times under the
  pointer only while its display is in the bar at the top: with the bar at the bottom they stay hidden. The Playback
  bar times snippet shows them there, under the pointer or always (`visibility: visible`, then `opacity`).

The layouts are plain CSS, in `src/core/theme/layouts/` (one file each, built into Ampify). To change one beyond its
variables, copy its file into your theme's `user.css` instead of importing it, and edit the copy. A few of its rules
work with Ampify's own code: keep them in a copy, or the matching behaviour stops.

| Rule | What Ampify's code does with it |
|---|---|
| `.now-playing { grid-column-span: 2; }` (`floating-panels`) | The song spans the toggles' column, and Ampify ends it a clear space short of the first toggle, whatever their number. A `width` instead (`bottom-bar`) keeps the song's own size, kept clear of the buttons |
| `@media (max-width: 1324px)` and `(min-width: 1325px)` | The width where Apple Music folds its sidebar. With `apple.sidebar = hover`, Ampify keeps it folded and these bounds follow that, not the window |
| `--caption-room` of `40` (`bottom-bar`) | The strip the top search bar and its veil are drawn in |
| `Grid#ShadowCaster { visibility: collapsed; }` (`floating-panels`) | Without Apple's square shadow, Ampify draws a hairline round the sidebar opened over the pages, where nothing else draws an edge |
| `.apple-logo { opacity: 0; }` (`bottom-bar`), never `visibility` | Apple Music sets the logo's visibility from what plays: the value Ampify restores when the theme goes would come back stale and show the logo over the title |

#### The playback bar's place

Where the bar goes is one setting of Ampify for every theme, `apple.playbar` (Settings > Ampify > Playback bar):
`theme`, the default, leaves it where the theme plans it; `top`, `bottom` and `centred` hold whatever the theme. A
theme names its own place in its manifest, `"playbar": "bottom"` (or `"centred"`; `top` when it says nothing), and
every `user.css`, a theme's or a snippet's, reads the place in use as the setting `bar-position`, with the values
`Top`, `Bottom` and `Centred`. The theme declares no such setting itself:

```css
@import "ampify:floating-panels";

@setting bar-position = Bottom {
    @import "ampify:bottom-bar";
}

@setting bar-position = Centred {
    @import "ampify:bottom-bar";
    @import "ampify:centre-bar";
}
```

A theme with at least one `@setting bar-position` block lays out the bar itself, at every place. Under any other theme,
and with no theme, Ampify puts the bar at the bottom by itself while the place is `Bottom` or `Centred`: `bottom-bar`,
plus `centre-bar` when centred, applied before the snippets. Themes made before 1.3.0 declared a `bar-position` setting
of their own: its value still holds while `apple.playbar` is `theme`.

#### A column for the panels

The panels of elements (`Ampify.ui.panel`) float over the right of the pages. A theme that gives them a column of their
own leaves room on the right of `.content`: a right margin at least as wide as a panel (380) and two gaps. Ampify
docks the panels there, as high as the pages, with the pages' background and corners, and the panels' own cards
melt into the column. Apple's lyrics and queue pane, when open, stands on the right of that room, never under it.
Without the room (the setting off, a narrower margin), the panels float again. Without a theme that leaves room, Ampify makes a column
itself when the window is wide enough: it meets the playback bar, the window's right edge and its bottom, one gap from
the pages.

```css
@import "ampify:floating-panels";
@import "ampify:bottom-bar";

@setting now-playing {
    .content { margin: var(--caption-room, 40) calc(380 + var(--gap) * 2) var(--bar-room, 80) calc(var(--gap) * 2); }
}
```

#### What a theme tells snippets and extensions

Each shared shape or place has one way to be set, read by everything that needs it. A theme sets the variables in its
`:root` (or inside a `@setting` block); a snippet that changes the same shape sets them too, and wins, since it comes
after the theme.

| Set with | Read by |
|---|---|
| `"playbar"` in the manifest, and the user's `apple.playbar` | Every `user.css` as `@setting bar-position` ([The playback bar's place](#the-playback-bars-place)) |
| `--panel-radius` | The layouts' panels, and the panels of extensions (`Ampify.ui.panel`), which take no larger radius inside. Corners sets it to `0` for square corners |
| `--bar-radius`, `--lcd-radius` | Snippets that draw over the playback bar or the song's display, such as the glow of Artwork colours |
| `--artwork-size` | Spinning artwork, which turns a record of that size. Set it only while the theme resizes the bar's artwork |
| A right margin on `.content` | The panels of extensions, docked in that room ([A column for the panels](#a-column-for-the-panels)) |
| The accent (`AccentFillColorDefaultBrush`, or `theme.accent`) | Apple's controls, the progress line, the marks of `Ampify.ui.playbar.mark`, and `$AccentFillColorDefaultBrush` in any rule ([Chosen accent](#chosen-accent)) |
| The cover playing (nothing to set) | `--art-1` and the other artwork variables, in themes and snippets ([The artwork of the playing song](#the-artwork-of-the-playing-song)), and `Ampify.color.playing()` in extensions |

### Window size (`@media`)

```css
@media (max-width: 1324px) {
    .lcd { visibility: collapsed; }
}
```

`min-width`, `max-width`, `min-height` and `max-height`, joined by `and`, in the window's pixels (bounds included).
Apple Music folds its sidebar at 1324 pixels wide and less. With `apple.sidebar = hover`, these bounds follow the
sidebar instead of the window (folded: the 1324 rules hold at every width), until the sidebar is pinned open. No
variables and no nested `@media` inside.

`(prefers-color-scheme: light)` or `(dark)` holds rules for the theme's scheme in use (dark or light from its
background colour), whatever Windows' mode; it joins the sizes with `and`:

```css
@media (prefers-color-scheme: light) and (max-width: 1324px) {
    .sidebar-column { backdrop: acrylic(#FFF4F8FF, 0.8); }
}
```

### Hover, press and focus

```css
.content Button { transform-origin: center; }
.content Button:hover { transform: scale(1.05); box-shadow: 16px; }
.content Button:active { transform: scale(0.95); }
.search-area:focus { background: #3A1F5C; }
```

- While a state holds, its rules beat normal rules. Press beats focus, focus beats hover. When it ends, the element
  takes back exactly what it had.
- `order` is not allowed under a state. A control that keeps the pointer for itself does not react to `:hover`.
- Apple Music's buttons, menu entries and sliders change their own colors under the pointer. A `background`, `color` or
  `border-color` you give them stays in those states; a `:hover` color goes to both hover and pressed, an `:active`
  color to pressed only. A toggle that is on, a selected row and a greyed control keep Apple Music's colours until a
  rule names that state with `:checked`, `:selected` or `:disabled` ([Selectors](#selectors)).
- On a slider, `color` fills the part left of the thumb and `background` the rest of the track.

#### A state on an ancestor

A state written on an earlier part belongs to the element that part matches. The rule styles the last part while that
ancestor is in the state:

```css
.track-row .favorite-button { opacity: 0; transition: opacity 150ms; }
.track-row:hover .favorite-button { opacity: 1; }
```

- The favourite button of a song shows while the pointer is anywhere over its row, the button included, and fades
  out when the pointer leaves the row. The `transition` declared on the button applies both ways.
- `:active` (the row pressed), `:focus` (the focus anywhere in the row) and `:current` work the same way.
- When several ancestors match the part, the nearest one counts. A list reuses its rows as it scrolls: each pass looks
  for the row the element sits in again, so a value never stays on a row it no longer belongs to.
- A rule with a state on the ancestor and one with a state on the element itself both apply; while both states hold,
  the usual priority decides between them.
- `::before` and `::after` take no state, on any part.

### The line being sung

`:current` holds while Apple lights a line of the synced lyrics, on the line (`.lyrics-line`) and on anything inside it:

```css
.lyrics-line-content { corner-radius: 8; }
.lyrics-line-content:current { background: #40FF5C8A; }
```

- It follows the song line by line, without waiting for the theme's next pass. Under the pointer, `:hover` beats it.
- Apple fades the lines by their own states: an `opacity` on `.lyrics-line-content` loses to them, and a word-by-word
  song draws its words in Apple's own colors. A background or a corner radius shows.
- Leave the line's room alone: Apple sets a word-by-word line's text width when it builds the line, so a `margin`,
  `padding` or `border` on `.lyrics-line-content` cuts off the end of its words.

To blur the lyrics away from the sung line, blur their column towards its edges: Apple keeps the sung line near the
top of the side panel and a little above the middle of the full-screen player, and one blur serves every line.

```css
.lyrics-panel .lyrics-synced { filter: blur(9px) edges(8% 50%); }
.fullscreen-lyrics .lyrics-synced { filter: blur(9px) edges(28% 40%); }
```

Examples: the Blur setting of the [`lyrics-style`](../../themes/snippets/lyrics-style/) snippet; colour filters in
[`artwork-filters`](../../themes/snippets/artwork-filters/), liquid glass in the "Liquid glass" setting of
[`glass`](../../themes/glass/).

### Flyouts, menus and tooltips

They open in popups, outside the window's tree. A selector reaches what is inside a popup (`.flyout Slider`), never
what opened it. A popup takes the theme the first time it opens.

```css
.menu, .flyout, .tooltip { background: #282828; }
.menu-item:hover { background: #3E3E3E; }
```

### Transitions

```css
.card { transition: transform 180ms, opacity 150ms; }
.card:hover { transform: scale(1.04); }
```

`transform`, `opacity`, `background` (plain colors only) and `filter` animate, `all` means the four, from 0 to 5 s. Easing words
and delays are accepted and ignored: XAML's own ease-out is used.

`width` is apart, never part of `all`: the widths Apple Music writes to the element glide there in a straight line,
over the time each write took to come (at most the duration), instead of jumping; a move back, or forward by more than
8 % of the parent's width (a seek, a new song), still jumps. It runs on the interface thread, one layout per frame, so
keep it to small elements. Every theme has it on the played part of the progress line
(`.scrubber Rectangle#HorizontalDecreaseRect { transition: width 1s linear; }`, the smooth progress bar); `transition:
none` there brings Apple's steps back. A few elements Apple Music animates itself (such as the
playback bar's play button and the sidebar's entries) take a transition of `transform` alone; `opacity` and
`background` are refused there.

`transition: background` on a control (a menu entry, a button) goes to the root of its template, which is what
paints its background in each state; an element with no background starts from a transparent one, since a brush
eases only from another brush. The artwork of the playback bar (`.lcd-artwork-image`) and its area (`.lcd-artwork`)
refuse a transform of their own, and the display cuts whatever grows past the artwork's place: to zoom on hover, scale
the picture inside its frame (`.lcd-artwork:hover .lcd-artwork-picture`), as the Artwork theme does.

A `scale()` without `transform-origin` grows from the element's centre, as in CSS.

Scale a button, not the `FontIcon` inside it. An icon's box is larger than its glyph and off the glyph's centre, so
`.play-button FontIcon { transform: scale(0.8) }` moves the glyph about 2 px towards the bottom right of the play
disc. The icons already sit in the middle of their buttons at their own size.

### Animations

```css
.lcd-artwork { animation: spin 20s linear infinite playing; }
.play-button:hover { animation: breathe 1.2s ease-in-out infinite alternate; }

@keyframes spin { to { transform: rotate(360deg); } }
@keyframes breathe { to { transform: scale(1.08); opacity: .8; } }
```

- Stops are `from`, `to` or percentages, and animate `opacity`, `transform` and a plain `background` colour. A colour
  mixes channel by channel, alpha included; a gradient, an image or `acrylic` in a stop is refused.
- `animation` takes, in any order: a name, a duration, an easing (`linear`, `ease`, `ease-in`, `ease-out`,
  `ease-in-out`, `cubic-bezier(...)`), a delay, a count or `infinite`, a direction (`normal`, `reverse`, `alternate`,
  `alternate-reverse`) and a fill mode (`none`, `forwards`, `backwards`, `both`). Up to 4 animations, 120 s at most.
  `steps()`, negative delays and `paused` are refused.
- `playing`, with `infinite`, runs the animation only while a song plays: in pause it holds still where it is and goes
  on from there when the song plays again, like a turntable (Spinning artwork). Ampify steps nothing for it meanwhile.
- Under a state, an animation starts each time the state begins.
- An `infinite` animation moves 30 times a second and stops while Apple Music is hidden or minimized: left to the
  compositor, it would redraw the whole window at every frame of the screen. A finite one runs on the compositor.
  A `background` colour is always moved this way, finite or not, 30 times a second. While it runs, the theme's own
  `background` for that element waits; it comes back when the animation ends without `forwards`.
- A turning or scaling animation pivots on the element's middle (or its `transform-origin`) and follows its size when
  the theme or Apple Music changes it. A child that Apple Music gives a fixed width (the artwork's 54) still overflows
  a smaller parent: size it too, as the Spinning artwork snippet does.
- Elements that Apple Music moves itself (many artworks, such as `.lcd-artwork-image`) or that the theme gives a
  `transform` refuse an animated transform: animate their parent (`.lcd-artwork`), or put the transform in the stops.
- With Windows' "Animation effects" off, animations do not run. `@keyframes` inside `@media` are not read.

### Fonts

`font-family: url(fonts/Inter-Regular.ttf#Inter)` uses a font from the theme folder (`.ttf`, `.otf`, `.ttc`). The name
after `#` is the family name inside the file. Ampify never changes the font of icon glyphs.

A font role names the font of a kind of text. Set it in `:root` and use it with `font-role`:

```css
:root {
    --font-title: "Segoe UI Variable Display";
    --font-title-weight: bold;
    --font-title-size: 30px;
}

.page-title { font-role: title; }
```

Seven roles exist by default: `caption`, `body`, `body-strong`, `subtitle`, `title`, `title-large` and `display`, with the
family, weight and size of Microsoft's Windows typography guidance. The values are in `src/core/font_roles.cpp`
(`DefaultFontRoles`); `Ampify.fonts` reads the ones in force.

A theme can create its own roles (`--font-lyrics`). Extensions read them with `Ampify.fonts`
([API](../extensions/api.md#56-services-preview)).

### Other properties

- `box-shadow: 24px` puts a Windows `ThemeShadow` under the element at that depth. Color, offset and blur come from
  Windows, and the shadow may not show on every element.
- `filter: blur(3px)` draws the element blurred, with the same standard deviation as in CSS; what lies behind it
  stays sharp. `filter: blur(9px) edges(30%)`, Ampify's own, blurs only towards the top and bottom, over 30% of the
  height each (`edges(10% 50%)`: top, then bottom), fading to sharp in the middle.
- The colour filters of CSS go in the same `filter`, before or after the blur, applied in the order written:
  `saturate()`, `grayscale()`, `sepia()`, `brightness()`, `contrast()`, `invert()` (a number or a percentage, as in
  CSS, up to 10) and `hue-rotate(90deg)`. Ampify adds `tint(color[, amount])`, which multiplies the colours by
  `color` (white leaves them; `amount` from 0 to 1, 1 when left out), and `duotone(dark, light)`, which prints the
  image in two inks, black becoming `dark` and white `light`. Colours are those of the theme or hexadecimal values,
  `var(--art-1)` included. `drop-shadow()`, `opacity()` and `url()` are not available.

  ```css
  .artwork { filter: duotone(#1B1035, var(--accent, #FA2D48)); transition: filter 400ms; }
  .card-artwork:hover { filter: saturate(1.4) brightness(1.05); }
  .lyrics-panel .lyrics-synced { filter: blur(9px) edges(8% 50%) grayscale(0.6); }
  ```
- With `transition: filter <duration>`, a blur eases from where it is, and a change of colours fades the new ones in
  over the old: both run on the compositor, so they stay smooth while Apple Music is busy. Without a transition the
  filter changes at once. A filter applies to one element only: the element must hold a single child, and its parent
  no composition layer of its own, or the value is refused. A filtered element is drawn again on every frame it
  changes (twice with `edges()`): filter a column, a panel or the artwork of the song playing, not every row of a
  long list.
- `overflow: hidden` cuts what spills out of an element (a child moved by `transform`, a decorative image); many of
  Apple's panels already cut their children. `clip-path: inset(0 0 40px 0)` cuts 40 pixels off the bottom. The clip
  follows the element's size. Only rectangles: no `round`, `circle()` or `polygon()` (use `corner-radius`).
- `flow-direction: rtl` flips an element and everything inside it.
- `color-scheme: dark` puts an element and everything inside it in dark mode, whatever the window's mode: Apple's
  text and controls turn light, so a dark background painted on a light theme stays readable. `light` does the
  opposite, `normal` follows the window.
- `order` moves an element among its parent's children (`0` first). A parent that cannot reorder ignores it.

## Snippets

A snippet is a small style that people switch on by itself, in `%USERPROFILE%\Ampify\snippets\<id>\`: a
`manifest.json` with `"kind": "snippet"`, plus a `user.css`, a `resources.xaml` (named by `"resources"`), or both.

- It applies on top of the active theme (or alone), and its rules win. Several snippets apply in alphabetical order.
- It can import a ready-made layout too (`@import "ampify:bottom-bar";`), and read where the playback bar is with
  `@setting bar-position` (see [The playback bar's place](#the-playback-bars-place)).
- It sees the theme's colors, variables, settings, `@keyframes` and font roles. A `colors.ini` or `color.ini` in a
  snippet is ignored.
- `theme.parts.*` do not affect snippets. A refused key in its `resources.xaml` refuses the snippet only.
- A snippet's `::before` or `::after` with a `content` of its own never replaces the theme's in the same slot: the
  theme's image or bars stay as the theme draws them, and the snippet's declarations for that slot are set aside. On a
  slot the theme leaves empty (or `content: none`), the snippet's go in; without a `content`, a snippet's declarations
  still tune the theme's decor.
- It can declare settings of its own, like a theme ([Theme settings](#theme-settings)): the Ampify
  card shows them while the snippet is on, they are stored as `snippets.<id>.<key>`, and its `user.css` reads them
  with `@setting` and `--setting-<key>`. A key the theme declares too is the snippet's there. Example: the Animated
  equalizer, [`themes/snippets/display-equalizer/`](../../themes/snippets/display-equalizer/).
- A snippet that changes a shared shape (the panels' corners, the bar's) sets the same variable as a theme would
  ([What a theme tells snippets and extensions](#what-a-theme-tells-snippets-and-extensions)), so the other snippets
  and the extensions' panels follow it.

Switch it with `ampify snippet enable <id>` / `disable <id>`. Example: Corners,
[`themes/snippets/round-corners/`](../../themes/snippets/round-corners/), which sets `--panel-radius: 0` for square
corners.

## Automatic scheme

| Setting | Effect |
|---|---|
| `theme.scheme = Dark` | That scheme |
| `theme.scheme =` (empty) | The theme's `defaultScheme`, or the first scheme |
| `theme.scheme = auto` | Follows Windows' light or dark mode |
| `theme.schedule = 07:00=Light,19:00=Dark` | Follows the clock, and wins over `theme.scheme` |

With `auto`, dark mode takes the first scheme named `Dark` or `Night` (or containing it), light mode takes `Light` or
`Day`, else the first scheme that is not dark. In a schedule, the last time passed wins, and a scheme can be `auto`
(`07:00=auto,22:00=Night`). Ampify checks both every 3 seconds.

## Chosen accent

`theme.accent = FA2D48` gives every theme that accent in place of its own, and gives it to Apple Music's own look when
no theme is active. It replaces `AccentFillColorDefaultBrush` (and Spicetify's `button` and `playback-bar`) before
Ampify derives the rest, so the hover and pressed accents, toggles, sliders, the progress bar, links and selected rows
follow it. The colours a scheme gives along with its own accent (`AccentFillColorSecondaryBrush`,
`AccentFillColorTertiaryBrush`, `TextOnAccentFillColor*`, `AccentTextFillColor*`) are derived again from the chosen
one. A colour a theme writes as a hexadecimal value in `user.css` stays: name `$AccentFillColorDefaultBrush` for what
should follow the accent. Keys the theme's `resources.xaml` defines stay as well. Empty, the theme keeps its own.

`theme.accent = artwork` (the last choice of Settings > Ampify, "Follows the artwork") takes the accent from the song
playing instead: its vibrant colour, `--art-2`. It changes when the song does, and only then. Before the first cover,
the theme keeps its own accent. The colour is kept between a lightness of 30% and 70%, so it never turns into black
or white on a dark or a light page: a very dark or very pale cover gives a grey-tinted accent of that lightness. Text
on the accent is still chosen for it as above.

A theme's own "Artwork" accent setting (Fluent's) and `theme.accent` do not fight: the theme's other choice
(Windows) is written as `$AccentFillColorDefaultBrush`, so it follows `theme.accent`; its Artwork choice uses
`--art-2` where the theme names its accent, whatever `theme.accent` says, and Apple's own accented controls keep
`theme.accent`. A new theme needs no setting of its own for this: `theme.accent = artwork` does it for every theme.

## Glass level

`theme.glass` lightens what every theme and snippet asks of the graphics card, for a laptop on battery or an older
card. `full` (or nothing) keeps the theme as written. `balanced` stops the animations that never end (a turning
artwork) and leaves out the depth shadows of `box-shadow`; acrylic keeps its blur. `eco` does the same and paints
acrylic panes as plain colours, the tint made more opaque so the text keeps its contrast, an acrylic window
(`--window-material: acrylic`) as Mica, liquid glass as a plain colour too, and leaves out the blur of `filter`
(its colour filters stay, they cost one light pass). A theme needs nothing for
it: write the effects as usual. People choose it under the accent in Settings > Ampify ("Glass effects").

## What Ampify adjusts for every theme

Some of Apple Music's defaults only show once a theme changes something. Ampify adds the matching rule after reading
the theme, under its `user.css`: a theme that writes the same property for the same name keeps its own value. The log
and the inspector list them as "Ampify (derived from the theme)".

| When the theme... | Ampify adds |
|---|---|
| rounds `.lcd` | `.scrubber` stops before the display's corner |
| puts a `::before` or `::after` image in `.scrubber` | the line grows to fit it, keeping its place (see "An image that follows the song's progress") |
| has an accent | the progress line (`.scrubber`) takes it, like the volume slider |
| has an accent | the bars next to the row playing (`.now-playing-indicator`) take it, and follow it when it changes with the song |
| has an accent | Up Next's "Clear" (`.queue-clear`) and its autoplay toggle while on (`.queue-autoplay`) take it, not Apple's red and blue |
| has a link colour or an accent | `.album-artist` takes it (Apple's own blue barely reads on a light page) |
| has a secondary text colour | `.card-subtitle`, the artist under a tile, takes it (Apple paints that link in code), at full opacity: Apple fades it to 70% for its own grey |
| has primary and secondary text colours | the song's title and artist in the display (`.track-title`, `.track-artist`) take them: Apple colours them for the window's mode when it builds them, white on a light theme's bar under a dark Windows |
| has a secondary text colour and a dark scheme | Apple's own `SecondaryTextBrush` (shelf headers, captions, the search field's hint) takes it. A light scheme leaves Apple's: its dark captions over artwork read that key too |
| has a card colour | the top results of a search take it |
| has selected-row colours | Apple's lists use them, though their style never reads them |
| lights `.track-row:hover` | Apple's own light of the row goes, so only one shows |
| lights `.card:hover` | `.card` keeps 8 pixels of room around its picture and titles |

With or without a theme, Ampify also keeps a flyout inside the window (the volume flyout passed its right edge), hides
the folded sidebar's logo while the back arrow covers it, and keeps the progress row of the full-screen player wide
enough for the Lossless badge in a narrow window.

## Theme parts

| Setting (default `1`) | When `0` |
|---|---|
| `theme.parts.colors` | `colors.ini` sets no resource (its colors stay usable as `$name`) |
| `theme.parts.resources` | `resources.xaml` and `user.css` are read but not applied |
| `theme.parts.assets` | Images and fonts of the folder are refused |
| `theme.parts.script` | The theme's `main.js` does not run |

## Sidebar and home (`sidebar.json`, `home.json`)

These files, next to `settings.txt`, hide entries and change their order, with or without a theme.

```json
{
  "version": 1,
  "reorder": true,
  "items": [
    {"name": "RadioItem", "hidden": false},
    {"name": "BrowseItem", "hidden": true}
  ]
}
```

`name` is the element's `x:Name` (`NavigationViewItem#<name>` in the sidebar, `#<name>` on the home page). `hidden`
gives `visibility: collapsed`. With `"reorder": true`, the list order becomes `order`, so list every entry. After an
inspector pass, Ampify writes `sidebar.json` if it is missing, with the real entries, none hidden.

## Check a theme and read the log

`ampify theme check <folder>` reads a theme without Apple Music and lists each rule kept and each problem. In Apple
Music, `logs\AppleMusic.log` shows the theme loaded with its scheme, each value written (`Grid#Bar Background =
#FF1C1C1E (user.css line 3)`), each refusal, and the number of values restored when the theme goes.

The inspector (`ampify devtools`, or `theme.inspect = 1`) writes the tree of the page on screen to
`logs\inspector.txt` and `logs\inspector.json`: the color resources, then each element with its selector, sizes,
properties and the visual states it has (`States=`). A button never hovered may show no states yet. `ampify inspect
--tree` prints it.

## Limits

- Element names come from Apple Music and can change with an update. The [stable names](names.md) follow them.
- A resource read as `StaticResource` keeps its value until the element is recreated.
- Images and fonts must be in the theme folder.
- New pages and rows take the theme within a fraction of a second; `auto` and schedules within 3 seconds.
