# The best of Spicetify, and what Ampify does with it

A survey of the most popular Spicetify themes, extensions and apps (ranking of the GitHub topics `spicetify-themes`,
`spicetify-extensions` and `spicetify-apps` by stars, read on 2026-10-30), with their screenshots looked at one by one. The
goal: know what people do with Spicetify, so that Ampify allows the same in Apple Music. Star counts are rounded.

## What recurs in the most popular themes

Screenshots looked at: Hazy, Comfy, Lucid, Bloom, Fluent, Text, Galaxy, Beautiful Lyrics.

1. **The playing song's artwork as the background**, blurred, behind the whole interface (Hazy 460, Comfy 800, Lucid 500,
   Galaxy, Dribbblish Dynamic 370). The most copied effect.
2. **Floating panels:** sidebar, page and right panel separated by margins, rounded corners, often translucent (Lucid, Hazy,
   Comfy, Bloom, Liquify).
3. **Glass:** acrylic or blur, inspired by Windows 11 (Bloom 680, Fluent 320).
4. **A playback bar at the bottom**, sometimes floating as well.
5. **A large banner** at the top of playlists and albums (Comfy, Nord).
6. **Palette series:** Catppuccin (600, 4 variants), Sleek (19 schemes), Text (20 variants).
7. **Whole looks:** Text (terminal, monospace font, 1 px borders), WMPotify (Windows Media Player 11).

## Most used extensions and apps

| Element | Stars | What it does |
|---|---|---|
| Beautiful Lyrics | 2,400 | Full-screen lyrics, Page, Cinema and Full-screen views, background drawn from the artwork |
| Marketplace | 1,600 | Install themes and extensions inside the app |
| Spicy Lyrics | 690 | Word-by-word lyrics, dynamic background |
| Statistics (harbassan) | 670 | Top artists and songs, full pages |
| Liquid Lyrics, ivLyrics | 250, 190 | Full-screen lyrics, animated background from the artwork, control pill |
| Visualizer (Konsl) | 300 | Audio visualizer |
| Cat Jam, Oneko | 210, 170 | Small fun animations |
| Hide podcasts, sorting, labels | 150 to 260 | Tidy and filter |

## How Spicetify does it, and how Ampify does it

Spotify is a web page: Spicetify injects CSS (the theme) and JavaScript (extensions, React) into the page. Apple Music is a
native app (XAML): Ampify translates a restricted CSS into XAML properties and runs extensions in JavaScript (QuickJS) with
an API modeled on Spicetify's (`Ampify.React`, `Ampify.player`, `Ampify.ui`...). The result is lighter (no browser), but each
effect has to be taught to the engine once.

## What already works in Apple Music (checked on screen)

| Spicetify effect | In Ampify | Example |
|---|---|---|
| Blurred artwork background | `background: artwork` under `acrylic()` panels | theme `artwork` |
| Rounded floating panels | `margin`, `corner-radius`, `backdrop: acrylic()`, `box-shadow` | `glass`, `artwork` |
| Playback bar at the bottom | `Grid#TransportBar, BackDrop { vertical-alignment: bottom; }` | [themes/format.md](../docs/themes/format.md), "Layout" |
| Palette series | A Spicetify `color.ini` as it is, `ampify theme import` | Catppuccin and Sleek imported |
| Animated hover | `:hover` with `transform: scale()` | `glass` |
| Theme fonts | `font-family: url(fonts/x.ttf#Name)`, roles `--font-title` | `glass` |
| Marketplace in the app | The `apps/marketplace` page | Button next to lyrics |
| Panels and pages in React | `Ampify.ReactDOM.render`, `Ampify.components` | `examples/react-panel` |

## What is still missing, by impact

Updated on 2026-10-31 (see [spicetify-ecosystem.md](spicetify-ecosystem.md) for the counts behind this order).

1. **Feedback states beyond hover**: `:active` and `:focus` (13 of 18 themes), `@keyframes` (11).
2. **Names for states and sub-parts**: selected and playing rows, the parts of the progress bar, the action bar and the
   header background of album pages, lyrics lines, scrollbars, context menus.
3. **Apple's own player views**: the mini player and the full-screen player, with karaoke lyrics, live in a second window
   that themes do not reach ([apple-music-native-views.md](apple-music-native-views.md)). Ampify's Now Playing exists but
   is simpler.
4. **An animated background** (a warped artwork that moves, Kawarp): needs a Composition effect.
5. **Spotify's CSS does not translate:** it targets HTML classes that do not exist in Apple Music. A Spicetify theme keeps
   its colors, not its layout.

Done since the first survey: transitions (`transition`, see [docs/themes/format.md](../docs/themes/format.md)), names for
cards, shelves and album headers ([docs/themes/names.md](../docs/themes/names.md)), the player on the kernel side, and a
full-window Now Playing view.

Images looked at (addresses on the date of the survey): Hazy `github.com/Astromations/Hazy/raw/main/hazy_home.png`, Comfy
`github.com/Comfy-Themes/Spicetify/raw/main/images/preview.png`, Lucid
`raw.githubusercontent.com/sanoojes/spicetify-lucid/main/assets/images/base.png`, Bloom
`raw.githubusercontent.com/nimsandu/spicetify-bloom/main/images/main-cover.png`.
