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
- Values are hexadecimal, with or without `#`: `FA2D48`, `#FA2D48`, or `#80FA2D48` (alpha first).
- The default scheme is the manifest's `defaultScheme`, or the first section. The setting `theme.scheme` picks
  another (see [Automatic scheme](#automatic-scheme)).

From the accent (`AccentFillColorDefaultBrush` or Spicetify's `button`), Ampify works out what WinUI would otherwise
leave in Apple Music's blue: the hover and pressed accents (90% and 80%), toggles, sliders, check boxes, links, checked
buttons, and black text on a light accent. It also gives the theme's link color to an album's artist, the card color
to the search's top results, and the accent at low opacity to selected rows. A key the scheme or `resources.xaml` sets
itself is never replaced, and a rule in `user.css` on those elements wins.

## resources.xaml

A XAML resource dictionary. When `%USERPROFILE%\Ampify\resource-keys.txt` exists, each `x:Key` must be listed in it
(one key per line); a single key outside the list refuses the whole theme, and the log names it.

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
| `A:nth-child(2)`, `A:nth-last-child(2)` | The second child of its parent, from the start or from the end (counting from 1) |
| `A:first-child`, `A:last-child`, `A:only-child` | Position among its parent's children |
| `A:not(B)` | An `A` that does not match `B`, where `B` is one part: a type, a `#name` and the pseudo-classes above |
| `A::before`, `A::after` | A decorative image put into `A` (see [Decorative images](#decorative-images)) |

- One state (`:hover`, `:active` or `:focus`) per selector, on its last part.
- `:not()` takes no state, no `::before`/`::after`, no combinator and no class.
- Not supported: attributes (`[x=y]`), `An+B` forms (`odd`, `2n+1`) and other pseudo-classes (`:checked`...). A rule
  that uses one is ignored, with a line in the log.
- Names from WinUI templates (`Grid#RootGrid`, `Border#Root`, `Grid#ContentRoot`) appear many times: always put them
  under a named parent.

### Priority

The ranking is `!important`, then source, then specificity, then place in the file. Sources apply in this order: the
theme, each active snippet (alphabetical), then `sidebar.json` and `home.json`; a later source wins whatever its
specificity. Within a source, `#name` beats a type, a type beats `*`, and the last rule wins a tie.

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
- `var(--name, fallback)` gives a fallback. A missing variable without one makes the declaration ignored.
- `calc()` takes `+`, `-`, `*`, `/` and brackets. Lengths can be added or subtracted, not multiplied together.
- A snippet sees the variables of the active theme.

### Theme settings

A theme can declare settings in its manifest, with the same `settings` field as an extension
([manifest.md](../extensions/manifest.md#settings)). People change them in Ampify's settings page,
and the theme applies again at once.

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
  (case-insensitive, quotes allowed).
- A `@setting` block holds rules, `:root`, `@media` and `@import`. It may sit inside `@media` and the reverse. Not inside
  another `@setting`, and no `@keyframes` inside it.
- The defaults should give the theme's normal look.

### Properties

| CSS | XAML | Values |
|---|---|---|
| `background` | `Background` | Color, `$name`, `transparent`, `url(images/file.png) [cover\|contain\|fill]`, `artwork`, `linear-gradient(...)` |
| `backdrop` | `Background` | `acrylic(tint, opacity)` |
| `color` or `foreground` | `Foreground` | Same as `background` |
| `border-color` | `BorderBrush` | Same as `background` |
| `selected-background` | `SelectedBackground` | A selected list row (`.list-item`) |
| `selected-hover-background` | `SelectedPointerOverBackground` | The same row under the pointer |
| `selected-active-background` | `SelectedPressedBackground` | The same row while pressed |
| `border-width` | `BorderThickness` | 1 to 4 lengths, not negative (top right bottom left) |
| `corner-radius` or `border-radius` | `CornerRadius` | 1 to 4 lengths, not negative (top-left, top-right, bottom-right, bottom-left) |
| `font-family` | `FontFamily` | A font name, or `url(fonts/file.ttf#Name)` |
| `font-size` | `FontSize` | Pixels, or relative to the element's own size (`120%`, `1.2em`) |
| `font-weight` | `FontWeight` | `normal`, `bold`, or 1 to 1000 |
| `font-style` | `FontStyle` | `normal`, `italic` or `oblique` |
| `letter-spacing` | `CharacterSpacing` | `normal`, em (`0.05em`) or pixels, up to 10em or 200px |
| `line-height` | `LineHeight` (text) | `normal`, pixels, or a factor (`1.5`, `150%`, `1.5em`) up to 10 |
| `text-align` | `TextAlignment` (text) | `left`, `center`, `right`, `justify`, `start`, `end` |
| `text-overflow` | `TextTrimming` (text) | `ellipsis` or `clip` |
| `white-space` | `TextWrapping` (text) | `nowrap` or `normal` |
| `margin` | `Margin` | 1 to 4 lengths, negative allowed |
| `padding` | `Padding` | 1 to 4 lengths, not negative |
| `width`, `height` | `Width`, `Height` | A length or `auto` |
| `min-width`, `min-height` | `MinWidth`, `MinHeight` | A length, or `auto` |
| `max-width`, `max-height` | `MaxWidth`, `MaxHeight` | A length, or `none` |
| `compact-pane-length` | `CompactPaneLength` | On `.navigation`: the width of the folded sidebar (48 in Apple Music) |
| `opacity` | `Opacity` | 0 to 1 |
| `visibility` | `Visibility` | `visible` or `collapsed` (`hidden` and `none` mean `collapsed`) |
| `display` | `Visibility` | `none` hides; `block`, `flex`, `grid`... show (use them to undo a `display: none`) |
| `template` | `Template` | `url(templates/file.xaml)`, a full `ControlTemplate`. Skipped on a control with a negative padding or border |
| `flow-direction` or `direction` | `FlowDirection` | `ltr` or `rtl` |
| `order` | Position | An integer from 0: the place among the parent's children |
| `horizontal-alignment` | `HorizontalAlignment` | `left`, `center`, `right` or `stretch` |
| `vertical-alignment` | `VerticalAlignment` | `top`, `center`, `bottom` or `stretch` |
| `grid-row`, `grid-column` | `Grid.Row`, `Grid.Column` | An integer from 0 (direct children of a `Grid` only) |
| `grid-row-span`, `grid-column-span` | `Grid.RowSpan`, `Grid.ColumnSpan` | An integer from 1 |
| `z-index` | `Canvas.ZIndex` | An integer; the highest is drawn in front |
| `transform` | `RenderTransform` | `scale(x[, y])`, `scaleX`, `scaleY`, `rotate(Ndeg)`, `translate(x[, y])`, `translateX`, `translateY`, `none` |
| `transform-origin` | `RenderTransformOrigin` | Percentages or `left`, `center`, `right`, `top`, `bottom` (no pixels) |
| `box-shadow` | `Shadow`, `Translation` | A depth from 0 to 100 pixels, or `none` |
| `transition` | Implicit transitions | `<property> <duration>, ...` for `transform`, `opacity`, `background` or `all`; or `none` |
| `animation` | Composition animations | Runs a `@keyframes` block (see [Animations](#animations)) |
| `font-role` | `FontFamily`, `FontWeight`, `FontSize` | A font role, such as `title` |
| `content` | Image of `::before`/`::after` | `url(images/file.gif) [cover\|contain\|fill]` or `none` |

- "(text)" properties exist on a `TextBlock` only; `font-style` and `letter-spacing` also work on a control.
- A property an element does not have is refused once, in the log, without harm. Everything comes back to its
  original value when the theme is removed.
- An unknown property is ignored, and the log suggests the equivalent when there is one: `background-color` →
  `background`, `backdrop-filter` → `backdrop`, `justify-content`/`align-items` → alignment on the children, `gap` →
  `margin` on the children, `border` → `border-width` and `border-color`. `filter`, `pointer-events`, `cursor`,
  `overflow`, `user-select`, `mix-blend-mode`, `clip-path` and `text-transform` have no equivalent.

### Gradients, images and acrylic

```css
.content { background: linear-gradient(135deg, #0E1424 0%, $AccentFillColorDefaultBrush 60%, #2A1F45); }
.sidebar { backdrop: acrylic($LayerFillColorDefaultBrush, 0.55); }
.window  { background: url(images/stars.png) cover; }
```

- `linear-gradient` takes an optional direction (an angle such as `90deg`, or `to right`) and at least two colors, each
  with an optional percentage. Without a direction it goes down. `color` and `border-color` accept it too.
- Image fits: `cover` (fills, crops), `contain` (whole image), `fill` or nothing (stretches). Paths start at the theme
  folder and cannot leave it.
- `acrylic(tint, opacity)` blurs the window's elements behind (not the desktop). With transparency effects off in
  Windows, only the tint shows.

### The artwork of the playing song

`background: artwork` shows the current song's artwork and follows it (within 3 seconds). On the window, under acrylic
panels, it gives the blurred background of Spicetify's Hazy or Comfy themes. Example: `themes/artwork`.

Ampify also gives the artwork's colors as variables. Before the first artwork they do not exist, so give a fallback.

| Variable | Value |
|---|---|
| `--art-1` | The most common color |
| `--art-2` | The most vibrant one, for accents |
| `--art-3` | A dark vibrant one, for backgrounds and glass |
| `--art-text` | `#000000` or `#ffffff`, whichever reads best on `--art-1` |
| `--art-1-hex`, `--art-2-hex`, `--art-3-hex` | The same without `#`, to put an alpha in front |

```css
.playbar { backdrop: acrylic(var(--art-3, #0B0B10), 0.7); }
.lcd { background: #33var(--art-2-hex, FFFFFF); }
```

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
  `visibility`, `transform`, `transform-origin` and `animation`.
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

The sound is analysed only while bars are on screen. With Windows' animation effects off, they stand still. Example:
[`themes/snippets/display-equalizer/`](../../themes/snippets/display-equalizer/).

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
| `ampify:bottom-bar` | The playback bar goes to the bottom across the window: the song left, buttons in the middle, volume right. Import it after `floating-panels` |
| `ampify:scrollbars` | Scroll bars inside their panel, clear of its rounded corners, without Windows' arrows. Part of `floating-panels` |

| Variable | Default | Meaning |
|---|---|---|
| `--window-background` | `#0B0B0D` | The window around the panels |
| `--panel-background` | `#17171A` | Both panels |
| `--panel-radius` | `10px` | Their corners |
| `--band-background` | `#1E1E22` | `floating-panels`: the band under an album's tracks |
| `--gap` | `8` | Space around and between the panels |
| `--content-top` | `64` | `floating-panels`: where the pages start |
| `--compact-pane` | `48` | `floating-panels`: width of the folded sidebar |
| `--page-top` | `8`, or `40` with `bottom-bar` | Space above a page's first line |
| `--bar-height` | `72` | `bottom-bar`: height of the bar |
| `--bar-room` | `80` | `bottom-bar`: space the panels leave at the bottom |
| `--bar-reach` | `calc(var(--gap) - 290)` | `bottom-bar`: how far left the bar reaches past the page column |
| `--bar-reach-folded` | `calc(var(--gap) - var(--compact-pane))` | `bottom-bar`: the same with the sidebar folded |

`@import url(ampify:bottom-bar);` works too. Other `@import` targets (files, URLs) are ignored.

### Window size (`@media`)

```css
@media (max-width: 1324px) {
    .lcd { visibility: collapsed; }
}
```

`min-width`, `max-width`, `min-height` and `max-height`, joined by `and`, in the window's pixels (bounds included).
Apple Music folds its sidebar at 1324 pixels wide and less. No variables and no nested `@media` inside.

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
  color to pressed only. Checked and selected states keep Apple Music's colors.
- On a slider, `color` fills the part left of the thumb and `background` the rest of the track.

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

`transform`, `opacity` and `background` (plain colors only) animate, `all` means the three, from 0 to 5 s. Easing words
and delays are accepted and ignored: XAML's own ease-out is used. A few elements Apple Music animates itself (such as the
playback bar's play button) refuse a transition.

### Animations

```css
.lcd-artwork { animation: spin 20s linear infinite; }
.play-button:hover { animation: breathe 1.2s ease-in-out infinite alternate; }

@keyframes spin { to { transform: rotate(360deg); } }
@keyframes breathe { to { transform: scale(1.08); opacity: .8; } }
```

- Stops are `from`, `to` or percentages, and animate `opacity` and `transform` only.
- `animation` takes, in any order: a name, a duration, an easing (`linear`, `ease`, `ease-in`, `ease-out`,
  `ease-in-out`, `cubic-bezier(...)`), a delay, a count or `infinite`, a direction (`normal`, `reverse`, `alternate`,
  `alternate-reverse`) and a fill mode (`none`, `forwards`, `backwards`, `both`). Up to 4 animations, 120 s at most.
  `steps()`, negative delays and `paused` are refused.
- Under a state, an animation starts each time the state begins.
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

| Role | Default family | Weight | Size |
|---|---|---|---|
| `caption` | Segoe UI Variable Small | 400 | 12 |
| `body` | Segoe UI Variable Text | 400 | 14 |
| `body-strong` | Segoe UI Variable Text | 600 | 14 |
| `subtitle` | Segoe UI Variable Display | 600 | 20 |
| `title` | Segoe UI Variable Display | 600 | 28 |
| `title-large` | Segoe UI Variable Display | 600 | 40 |
| `display` | Segoe UI Variable Display | 600 | 68 |

A theme can create its own roles (`--font-lyrics`). Extensions read them with `Ampify.fonts`
([API](../extensions/api.md#56-services-preview)).

### Other properties

- `box-shadow: 24px` puts a Windows `ThemeShadow` under the element at that depth. Color, offset and blur come from
  Windows, and the shadow may not show on every element.
- `flow-direction: rtl` flips an element and everything inside it.
- `order` moves an element among its parent's children (`0` first). A parent that cannot reorder ignores it.

## Snippets

A snippet is a small style that people switch on by itself, in `%USERPROFILE%\Ampify\snippets\<id>\`: a
`manifest.json` with `"kind": "snippet"`, plus a `user.css`, a `resources.xaml` (named by `"resources"`), or both.

- It applies on top of the active theme (or alone), and its rules win. Several snippets apply in alphabetical order.
- It sees the theme's colors, variables, settings, `@keyframes` and font roles. A `colors.ini` in a snippet is ignored.
- `theme.parts.*` do not affect snippets. A refused key in its `resources.xaml` refuses the snippet only.
- Since Ampify 1.3 it can declare settings of its own, like a theme ([Theme settings](#theme-settings)): the Ampify
  card shows them while the snippet is on, they are stored as `snippets.<id>.<key>`, and its `user.css` reads them
  with `@setting` and `--setting-<key>`. A key the theme declares too is the snippet's there. Example:
  [`themes/snippets/display-equalizer/`](../../themes/snippets/display-equalizer/).

Switch it with `ampify snippet enable <id>` / `disable <id>`. Example:
[`themes/snippets/square-corners/`](../../themes/snippets/square-corners/).

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
