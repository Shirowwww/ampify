# Changelog

## Unreleased

### Themes

- Ember: three glass panels over the colours of the song playing, the playback bar at the bottom, and a column for
  the song with its artwork, its title and the lyrics as they come.
- A theme that leaves a column on the right of the pages gets the panels of elements docked there, as high as the
  pages, beside Apple's lyrics pane rather than over it.
- An `image` node with `src: "artwork"` shows Apple's image of the song playing and follows it.
- A theme's `main.js` reads the theme's own settings with `Ampify.config.get`.

### Other

- Nothing of Ampify runs in the background any more: Windows hands each Apple Music process to the launcher as it
  starts, and the launcher exits once the kernel is in.
- The library could stay empty and Apple Music's engine stay behind after closing: the kernel now goes into the engine
  once it has opened the library.
- `Ampify.player.state().positionMs` follows the song while it plays; Apple Music 1.1792 gave 0 the whole time. With
  Apple's lyrics pane open, it follows the line Apple lights up, so a jump or a repeated song no longer leaves the
  lyrics behind.
- Ember's column is titled "Now playing" (in the display language) rather than with the theme's name.
- Ember and Greenroom: the play button's icon sits in the middle of its disc again.
- The log no longer writes a line each time a panel's text changes.

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
