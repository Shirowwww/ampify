# Changelog

## Unreleased

### Themes

- Ember: two glass panels over the colours of the song playing, the library and the pages, with the playback bar at
  the bottom.
- A theme that leaves a column on the right of the pages gets the panels of elements docked there, as high as the
  pages, beside Apple's lyrics pane rather than over it.
- An `image` node with `src: "artwork"` shows the cover of the song playing in large, loaded by Apple's own loader,
  and follows the song.
- A theme's `main.js` reads the theme's own settings with `Ampify.config.get`.
- Fluent: Apple Music as a Windows 11 app, the window in Mica, the pages on one light layer, light and dark.
- `--window-material: mica | mica-alt | acrylic` gives the window one of Windows 11's materials.
- `--page-art-1` to `--page-art-3` give the colours of the album or playlist on screen; the Album colours snippet
  tints those pages with them.
- `theme.glass = balanced | eco` lightens every theme for an older graphics card: no endless animations or depth
  shadows, and in eco no blur behind acrylic panes. "Glass effects" in Settings > Ampify chooses it.
- Declutter can hide the search bar above albums and playlists, which frees a row.
- `animation: ... infinite playing` runs an endless animation only while a song plays: Spinning artwork stops where it
  is in pause, like a turntable, and Glass's moving artwork rests, so neither costs anything while nothing plays.
- Progress mascot snippet: a comet, a note or a heart rides the progress line at the song's position, like Spicetify's
  Nyan Cat bar. An image put on the line now shows whole: Ampify gives the line room for it.
- Bottom playback bar snippet: the playback bar at the bottom of the window, across its whole width, as in Spotify,
  with or without a theme.
- Round corners snippet: one radius of your choice for the covers, tiles, rows, the display and the menus, on top of
  any theme.
- Cinema: the page no longer slides to the right at each change of tab. The capsule's curve no longer cuts into the
  cover: the cover and the display take its curve, and "Capsule corner radius" sets it. "Playback bar position" puts
  the capsule at the top, Apple's own place for the bar, where it shrinks with the window.
- Glass light: white frosted panes, so the dark text reads whatever the song; the panes took the cover's dark colour
  in both schemes.
- `@media (prefers-color-scheme: light)` and `(dark)` give a theme rules for each of its schemes.
- A theme's rule for the playback bar's cover (`.lcd-artwork-image`) now wins over its rule for every cover
  (`.artwork`): every theme's bar cover had taken the radius meant for the grids.
- A page's header no longer flashes Apple's grey frost before the theme's colour.
- An album's or a playlist's Play button takes each theme's corners instead of Apple's pill, and Glass's play bar
  buttons no longer light up as ovals under the pointer.
- A song display a theme gives a width no longer runs under the playback buttons in a narrow bar (the star on the
  shuffle button): it shortens to end a clear space before them, and takes its width back with the room.
- Up Next's "Clear" and its autoplay toggle take the theme's accent instead of Apple's red and blue
  (`.queue-clear`, `.queue-autoplay`).
- Glass dark and Artwork: the cards of the pages, the Marketplace and Settings > Ampify are a white veil on the glass
  instead of a fixed night blue or slate, so they take the colours of the song playing.
- The lyrics and queue pane of the floating-panel themes fits its panel: its right edge was cut (the durations, the
  autoplay button, the History tab; most in Mirror).

### Extensions

- Song panel, shipped with Ampify: a button of the playback bar slides in a column about the song playing, with its
  cover, the lyrics lit word by word, the album with its year and genre, the artist, the credits and what comes next.
- A `lyrics` node shows the lyrics of the song playing, lit word by word, without the element rendering it again.
- `Ampify.ui.panel.remove(id)` takes a panel away; panels slide in and out, and an empty column goes with them.
- In a narrow window, panels float over the right of the pages instead of squeezing them into a column too narrow for
  a playlist's buttons; they take their column again as the window widens.
- When the playback bar runs out of room, or leaves the song's display too narrow for its title, the buttons
  extensions add to it go one by one into a "More" button, the last one first, and come back as the window widens;
  Apple's own buttons stay where they are.
- A panel's close button closes it at the first click, and `Ampify.ui.panel.onClose(id, fn)` tells the extension; a
  React tree rendered into it is unmounted. The column of panels shows its scroll bar only under the pointer.
- `Ampify.library.tracks()` gives each track's album artist, genre, composer and year.
- `Ampify.color.playing()` gives the colours of the cover playing, also for the songs of the library, for which Apple
  gives no colour.
- `Ampify.player.lyrics()` has the lyrics without Apple's lyrics pane open: Ampify asks Apple's player for them.
- Skip list: an album or an artist listed from its menu only skips that album or that artist, a song by its catalog
  id when Apple gives one, and the list is exported and imported as text.
- Bookmarks: keep a song, an album, an artist, a playlist or a moment of a song, and open it again later.
- A-B loop: loop a passage of the song playing, marked from the playback bar.
- Discord presence: an album whose motion artwork moves a lot (Currents) showed its still cover, its GIF being over
  the 3 MB Ampify makes; a lighter GIF is made instead. A song heard before animated covers were turned on now gets
  its album's too, and the setting says that few albums have one.

### Other

- Nothing of Ampify runs in the background any more: Windows hands each Apple Music process to the launcher as it
  starts, and the launcher exits once the kernel is in.
- The library could stay empty and Apple Music's engine stay behind after closing: the kernel now goes into the engine
  once it has opened the library.
- Songs that stayed at 0:00 while others played: Apple Music tries a server's IPv6 address first and never falls back
  to IPv4 where IPv6 does not answer. Ampify now has it try IPv4 first (`apple.network = ipv4-first`, the default;
  `system` keeps Windows' order).
- `Ampify.player.state().positionMs` follows the song while it plays; Apple Music 1.1792 gave 0 the whole time. It
  stops where Apple's player stops, while a song loads or stalls, so the lyrics never run ahead of what is heard. With
  Apple's lyrics pane open, it follows the line Apple lights up, so a jump or a repeated song no longer leaves the
  lyrics behind.
- With Apple Music hidden in the notification area, Ampify no longer paints lyrics, equaliser bars, theme animations or
  menus nobody sees.
- A start counts as healthy after 8 s instead of 20 s, so a crash later on no longer counts towards safe mode.
- The launcher leaves alone the processes Windows hands it that are not Apple Music's.
- Ember and Greenroom: the play button's icon sits in the middle of its disc again.
- The log no longer writes a line each time a panel's text changes.
- The title, the artist and the album in the playback bar's display open their pages, as on the Mac.
- The full-screen player's cover is sharp on a large or high-resolution screen: a song of the library showed the
  library's 600 px copy stretched. Ampify lays the catalog's cover, at the size shown, over Apple's ("Full-screen
  cover" in Settings > Ampify turns it off).
- A title or a name cut with "…" scrolls while the pointer is on it, then comes back, in the lists, the cards, the
  menus and Ampify's panels ("Long texts" in Settings > Ampify turns it off).
- Ampify's entry can stand next to Apple's "···" at the top of the sidebar ("Ampify's entry" in Settings > Ampify).
- The README shows how to download the install script and read it before running it.

## 1.1.1 (2026-11-05)

Fixes for the play bar and for the full-screen player in a window.

### Themes

- `checked-color` sets a toggle's icon colour while it is on. Shuffle now takes the theme's accent while on, like
  repeat, instead of Apple's blue.
- The progress line runs to a rounded display's corner instead of stopping short.
- Floating panels: in a narrow window, the song's title uses the room up to the shuffle and repeat buttons.

### Other

- The full-screen player in a window can be dragged by its background like any window: it lets go on release and
  works with Windows' snap layouts and PowerToys FancyZones.
- Its shuffle and repeat buttons show when they are on, and follow the main window's both ways.
- Closing it brings the main window back where the player was, also when the main window was maximized or the player
  was moved to a screen of another scale.

## 1.1.0 (2026-11-04)

More for extension authors, and more of Apple Music's elements within reach of a theme.

### Extensions

- Extensions can read and set Apple's shuffle and repeat (`Ampify.player.shuffle()`, `repeat()`, `setShuffle()`,
  `setRepeat()`, `toggleShuffle()`, `toggleRepeat()`) and set the player volume (`Ampify.player.setVolume()`). From an
  interface extension, `setVolume()` moves Apple's own volume slider and the player follows.
- A context-menu entry gets the kind and catalog id of the item it was opened on (`{ title, item: { kind, id } }`)
  when Apple Music's data has them, and an entry can hold a submenu (one level, 20 entries).
- Top bar, play bar and sidebar entries can be switched on (`active`, `setActive`): they take the accent colour, and a
  sidebar entry also gets Apple's selection bar.
- Extensions can be written in TypeScript or JSX: `ampify` compiles `main.ts`, `main.tsx` and `main.jsx` with esbuild
  when it builds, develops or watches them.

### Themes

- `overflow` and `clip-path: inset(...)` cut an element to a rectangle, and `:playing` matches the list row of the song
  playing.
- `@keyframes` can animate an element's `background` colour.
- The lyrics' lines, words and messages have names (`.lyrics-line`, `.lyrics-words`...). An image can ride at the
  song's position in the progress bar.
- The Spinning artwork snippet turns the record on the spot: it stayed off its centre and was not quite round.

### Command line

- `ampify devtools pick` names the element under the pointer, for a theme's CSS. `ampify types` knows the new menu
  and button options.

### Other

- The Atmos leveler's log now says which audio stream format it follows or leaves alone.
- After an update, Settings > Apps shows the new version (it kept the one that was installed first), and uninstalling
  removes the old copies and the package's text files that an update leaves beside the programs.

## 1.0.0 (2026-11-04)

The first release of Ampify, themes, extensions and a Marketplace for the official Apple Music app on Windows.

### Themes

- Nine themes, each with a layout of its own: Glass (Apple's Liquid Glass, clear panes tinted by the song over its
  artwork), Artwork, Palettes (the Spotify layout in eight colour families: Catppuccin, Tokyo Night, Rosé Pine, Nord,
  Gruvbox, Dracula, Rose gold and Midnight), Console, Mirror (the sidebar on the right), Cinema (a centred column of
  pages), Paper, Greenroom and High contrast. 21 schemes, light and dark.
- A theme applies at once, without a restart. Its settings (corners, spacing, where the play bar goes...) and the
  accent colour are chosen in the Marketplace, on each card, or in Settings > Ampify.
- Snippets switch on over any theme: Cover ambience, Display equalizer, Spinning artwork, Square corners, Sidebar icons
  and Declutter.
- Themes are CSS on Apple Music's own elements, with ready-made layouts (`ampify:floating-panels`,
  `ampify:bottom-bar`), the song's artwork and its colours, bars that move with the music, images, animations, states
  and `@media`. Ampify adds on its own the rules a theme's choices call for, such as the accent on the progress line
  or room inside a lit card. Spicetify `color.ini` palettes work as they are.

### Extensions and apps

- Discord presence with the cover (animated when the album has a motion artwork), a progress bar and buttons; Last.fm
  scrobbling; a sleep timer; global shortcuts; a
  skip list; the volume in percent in the play bar.
- Pages inside Apple Music: Listening stats (Apple Music's own play counts, or what plays on this PC over 7 days, 4
  weeks or since the start) and New releases from the artists you listen to.
- Shipped with Ampify: Now Playing (Apple's full-screen player, with shuffle and repeat), an Atmos leveler that brings
  Dolby Atmos songs to the loudness of stereo ones and applies the moment it is switched on, and a playback guard
  for songs Apple Music leaves stuck.
- Each element shows the permissions it needs before it is switched on. Extensions are JavaScript with the `Ampify`
  object: player, interface, storage, network, notifications, sound.

### Ampify

- A Marketplace inside Apple Music, with previews, details, settings and one-click installs.
- English, French, German, Spanish, Portuguese, Russian, Japanese and Simplified Chinese.
- One line of PowerShell installs it, without administrator rights. It then loads with Apple Music from its own icon,
  updates itself while Apple Music is closed, and `ampify restore` turns it off.
- The `ampify` command line: themes, installs, `ampify new`, `ampify dev`, `ampify publish`, `ampify report`.
- Nothing is written into Apple Music's package, copy protection is never touched, and on an Apple Music version that
  was not tested Ampify stays off.
- Source available under the PolyForm Noncommercial License 1.0.0. What creators make for Ampify is theirs.
