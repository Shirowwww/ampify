# Changelog

## Unreleased

- Extensions can read and set Apple's shuffle and repeat (`Ampify.player.shuffle()`, `repeat()`, `setShuffle()`,
  `setRepeat()`, `toggleShuffle()`, `toggleRepeat()`) and set the player volume (`Ampify.player.setVolume()`).
- The Atmos leveler's log now says which audio stream format it follows or leaves alone.

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
