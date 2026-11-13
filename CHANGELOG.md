# Changelog

## 1.4.0 (2026-11-13)

AutoMix, steadier lyrics, and an Ampify that does less while nothing changes.

- AutoMix, in the Marketplace: each song flows into the next. It reads Apple's analysis of both songs and mixes on
  the beat when the tempos are close, swaps on a bar or crossfades when they are not. Albums in order, classical music
  and "repeat one" keep Apple's gapless playback, and Dolby Atmos songs are left alone. It is young, and the mixes
  will keep getting better. Extensions can plan their own with `Ampify.automix`.
- Lyrics in the full-screen player no longer settle a line too low before each new line, and the Song panel lights a
  whole line at once, as Apple's own pane does.
- The settings page orders the entries Ampify, its apps and the extensions add to the sidebar, as it already did for
  Apple's entries and the playback bar's buttons.
- Now Playing, Time on hover, the Song panel and AutoMix now come from the Marketplace and update from it, like every
  other element. Now Playing and Time on hover are installed and on at the first start; the copies you have stay.
- Time on hover works with the centred playback bar.
- AutoMix and Time on hover have a picture in the Marketplace.
- Themes and settings are read again only when something changes, instead of every three seconds, and nothing is
  redrawn while Apple Music's window is covered.

## 1.3.4 (2026-11-11)

Lyrics that glide from line to line, a full-screen button in the player, more room for the song on the playback bar,
and your own order for the sidebar and the playback bar's buttons.

- Full-screen lyrics move with Apple's own smooth scroll, the credits no longer overlap the last lines, and the lyrics'
  menu button stays sharp under Lyrics style's blur.
- The full-screen player has a full-screen button next to its close button.
- "Song on the playback bar" (Look tab) gives the song a quarter or a half more of the bar, under every theme.
- An Order group on the Look tab: drag the sidebar's entries or the playback bar's buttons into your own order.
- A theme's `sidebar.json` works again with Apple Music 1.1792.

## 1.3.3 (2026-11-11)

The playback bar's place back in the settings, a finished progress line under the buttons, and a moving background for
every theme.

- The playback bar's place is on the Look tab, under the theme it overrides.
- The moving background is the song's artwork as soft fields of colour drifting behind the pages, under every theme.
- Under the buttons, the progress line shows the elapsed time and the song's length at its ends, as Spotify does.
- With a number of lyrics lines around the sung one, the sung line stands in the middle of the full-screen player.
- A theme can set `"accent": "artwork"`; Artwork uses it, and Cinema puts its progress line under the buttons.
- Ampify's timers stop while Apple Music is minimised, hidden or on another desktop.

## 1.3.2 (2026-11-11)

- The song panel docks again in narrow windows (1.3.1 could leave it over the pages).
- Volume percentage sits next to the volume button (`beside: "volume"` for extension buttons).
- New stable name `.audio-formats`, and Declutter's quality badges setting hides that whole row.

## 1.3.1 (2026-11-10)

- The sidebar that opens under the pointer slides shut instead of snapping.
- A theme's colours reach the song's title and artist in the playback bar.
- Small fixes to the panels' column and to the times above a thick progress line.

## 1.3.0 (2026-11-10)

A new installer, a settings page in tabs, fewer snippets with more settings each, and one place for the playback bar
under every theme.

- Install and updates: an installer and an uninstaller with a window of their own, `ampify uninstall`, update
  offers in a notification, and Marketplace elements that update themselves once a day.
- Settings: five tabs and a search box that finds any setting, each element's settings folded under its name.
- Marketplace: a picture on every card, Install turns the element on, and a Developers tab for the examples.
- Playback bar: one setting places it for every theme, at the top, the bottom or centred under the buttons.
- Themes: Fluent rebuilt on Windows 11's materials, liquid glass for Glass, Apple's mini player themed too, colour
  filters (`filter`, `tint()`, `duotone()`), and the `:checked`, `:selected` and `:disabled` states. Ember joins
  Artwork; High contrast is gone.
- Snippets: merged into fewer, richer ones (Corners, Lyrics style, Progress line, Sidebar style, Declutter, Artwork
  colours), your choices carried over. New: Artwork filters and an equalizer in four shapes.
- Extensions: new Time on hover, A-B loop marks on the progress line, new building blocks (checkbox, progress,
  spinner, divider), a smoother Song panel, and song pages in Listening stats.
- Choose how many lyrics lines show around the sung one in the full-screen player.
- Ampify speaks Italian and Korean too, ten languages in all.

## 1.2.1 (2026-11-07)

The stable release of the 1.2.0 work. Ampify now loads in every build of an Apple Music version it was tested on.

## 1.2.0 (2026-11-07)

A top bar, grouped settings, more of Spicetify's API, and many fixes to the sidebar and the panels.

- Settings: a top bar with Apple's search and a back arrow; settings grouped under headings, with search, reset,
  export and import.
- Reliability: a freeze log, a fix for a stuck library agent (error 9039), skipping proxy detection, IPv4 first
  for songs stuck at 0:00, and warnings about programs known to crash Apple Music.
- Themes: Cosy, Fluent and Ember; a centred playback bar layout; "Follows the artwork" accent; `move-to`,
  `:current` for the sung lyrics line, `@media` for light and dark; new snippets such as Progress mascot, Lyrics
  spotlight and Compact sidebar.
- Extensions: the Song panel, Bookmarks, A-B loop, Copy link; grids, tabs, menus and lyrics nodes; much more of
  Spicetify's API (progress events, mute, clipboard, popups).
- The hover sidebar can be pinned, works on the right (Mirror) and from the keyboard.
- Titles cut with "…" scroll under the pointer, and the full-screen player's cover is sharp on large screens.

## 1.1.1 (2026-11-05)

- Shuffle takes the theme's accent while on, and the progress line reaches a rounded display's corner.
- The full-screen player in a window can be dragged, snaps with Windows' layouts and gives the main window back where
  it was.

## 1.1.0 (2026-11-04)

- Extensions can read and set shuffle, repeat and the volume, add submenus and lit buttons, and be written in
  TypeScript or JSX.
- Themes get `overflow`, `clip-path`, `:playing`, animated backgrounds and names for the lyrics.
- `ampify devtools pick` names the element under the pointer.

## 1.0.0 (2026-11-04)

The first release of Ampify: themes, extensions and a Marketplace for the official Apple Music app on Windows.

- Nine themes and 21 schemes, applied at once, plus snippets to stack on any theme.
- Discord presence, Last.fm scrobbling, a sleep timer, global shortcuts, a skip list, listening stats and new releases.
- Now Playing, an Atmos leveler and a playback guard, shipped with Ampify.
- A Marketplace inside Apple Music, the `ampify` command line, eight languages, and a one-line install without
  administrator rights.
- Nothing is written into Apple Music's package and copy protection is never touched.
