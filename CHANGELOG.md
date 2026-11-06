# Changelog

## Unreleased

### Settings

- "Top bar" in Settings > Ampify (off by default): "Search and back arrow" moves Apple Music's own search field and a
  back arrow into the empty strip at the top of the window, next to the window buttons, as in Spotify; the sidebar's
  field is hidden meanwhile. Typing, suggestions and going back stay Apple Music's own. It shows while the playback bar
  is at the bottom and the window is at least 612 pixels wide, and turning it off puts everything back where it was.
  Themes reach it as `.topbar-search` and `.topbar-back` (`apple.topbar`).

- The settings of an extension, a theme or a snippet can be grouped under headings: a `group` on each setting, and a
  `description` line under its label. Translations cover both (`groups`, `settingDescriptions`). Without groups, the
  card looks as before.
- Each element's settings card has a search box from six settings on (it matches labels and descriptions, whatever the
  case and accents), a Reset per setting, a Reset all, and Export and Import: a small JSON file in the Ampify folder,
  copied to the clipboard, read back on Import. Import refuses a file that is for another element or that holds a
  wrong value, and says why under the card.

### Reliability

- Opening the lyrics over a song with lyrics no longer ends Apple Music under Glass and the other floating-panel
  themes: the pane keeps the place its `SplitView` gives it, at a fixed width that fits its panel, for every theme.
- "Freeze log" in Settings > Apple Music (off by default): each time Apple Music's window stops answering for 0.3 s
  or more, AppleMusic.log says how long and what Ampify was doing then, with a summary each minute. It shows where the
  freezes come from, with or without the themes.
- "Stuck library agent" in Settings > Apple Music (off by default): when a library agent left behind by a killed
  session still runs, Ampify ends it so Apple Music can open its library again (error 9039). It gives the agent 10 s
  to leave when Apple Music is started from Ampify's launcher, and where the background launcher runs, 10 s after
  Apple Music is closed. The agent is never ended while Apple Music runs, and a refused end is not tried again.
- "Proxy detection" in Settings > Apple Music (off by default): "Skip automatic detection" stops Apple Music from
  waiting for Windows' automatic proxy detection (WPAD) before it loads a page or a song. A proxy set by hand or by a
  script address still applies (`apple.proxy`).
- The freeze log also writes name lookups and proxy detections that took 0.3 s or more, and the memory of each Apple
  Music process as it grows by 512 MB, and names Ampify's searches of the player's tree when one is under way.
- `AppleMusic.log` names, once per start, a running program known to crash Apple Music (3Dconnexion's driver, and
  RivaTuner, which draws MSI Afterburner's on-screen display) with its fix. `ampify doctor` says the same, and also gives the Apple Music version, a
  library agent left running without Apple Music, and Apple Music's crash dumps of the last 7 days.

### Extensions

- `inert: true` on any node of a view: the node and everything under it take no click, hover or keyboard focus, so a
  faded control no longer takes a click.
- Each entry of `Ampify.player.queue.list()` has an `artwork`, ready for an `image` node's `src`.
- The full-screen view of an element starts under its Close button, not behind it.
- An `image` with `src: "artwork"` shows the song playing even where Apple's play-bar image cannot be found, through
  Apple's own loader, and follows each new song.
- A view written as a plain tree, without `Ampify.React`, can give its nodes `onClick` and `onChange` functions; they
  run as in a React tree; they were dropped before.
- An `image` with `src: "artwork"` stays sharp when Apple shows the same cover again, as for two songs of one album; it
  kept Apple's small thumbnail stretched.
- More of Spicetify's API: a `progress` event (`onprogress`) once per second of the song, `player.skipForward`,
  `skipBack`, `formatTime` and `progress()`, mute (`muted`, `setMute`, `toggleMute`) and volume steps on Apple's
  slider, `keyboard.unbind`, `Ampify.clipboard` (permissions `clipboard.write` and `clipboard.read`), `http.put`,
  `patch` and `del`, `storage.clear`, the `uri.isSong`/`isAlbum`... predicates and `itmss:` links,
  `Ampify.appVersion` and `activeTheme()`, `Ampify.components.ConfirmDialog` and `hooks.useArtworkColor()`.
- Buttons and menu entries can be taken out (`ui.topbar.remove(id)`, `playbar`, `sidebar`, `menu`) and greyed
  (`disabled`, `ui.setDisabled`); a context entry can keep to some kinds of item (`kinds: ["song"]`). Named popups
  open and close by code (`ui.popup.show(name, view)`, `ui.popup.close(name)`), and `ui.title` has `get` and `reset`.
- New example `copy-link`: copies an item's link from its menu, and what plays now with Ctrl+Alt+C.

### Themes

- Cosy, a twelfth theme: a soft, flat evening look where the sidebar and the pages share one colour, the playback bar
  runs along the bottom with a large rounded artwork, and albums start under a wash of their own colours. Four
  schemes (Dusk, Night, Moss, Linen) and grouped settings: bar position, corners, artwork size and corners, flat or two
  panels, the strength of the wash, and the times on the bar.
- The settings of Artwork, Cinema, Ember, Glass, Greenroom and Palettes are grouped under headings (Layout, Shapes,
  Colours, Details), with a line of help where a setting needs one.
- Artwork and Fluent: "Playback bar position" puts the bar at the bottom, across or centred; the top stays the
  default. Glass and Mirror: "Space around the panels". High contrast: "Outline width". Paper: "Title face", serif or
  Windows' own.
- Ember's amber follows the accent chosen in Settings > Ampify, as Fluent's blue already did.
- Accent colour in Settings > Ampify: red, yellow and teal join the presets.
- Playback bar times, a snippet: the time played and the length under the pointer or always, also with the bar at
  the bottom, where Apple hides them.
- Album colours: a strength for the wash under the header (strong, medium, soft).
- An image a theme or snippet puts on the progress line gets the room it takes: the line grows to the image's height
  (with its margins) when that is more than the 18 pixels it was given before, and keeps its place in the display.
- Experimental: `track-height`, `track-radius` and `thumb-visibility` on `.scrubber` write Apple's own properties of
  the progress line for the height and corners of its track and for its thumb. The inspector lists them. Their effect
  in the play bar is not checked on screen yet, nor whether the track can grow past the line's 6 pixels.
- An `animation` on a `::before` or `::after` decor runs: the Animated artwork snippet's two layers turn again.
- Ampify's own sidebar entries (Marketplace, extensions) fit the folded sidebar: a centred icon in the column, with
  the label as its tooltip; the full row with its label when the sidebar is open, including the hover overlay.
- Chosen accent "Follows the artwork" (Settings > Ampify, or `theme.accent = artwork`): the accent of every theme takes
  the vibrant colour of the song playing, kept light enough to read on a dark or a light page.
- `--art-luma` and `--art-sat` give how light and how coloured the artwork is, as plain numbers from 0 to 1.
- Snippets get the artwork's colours (`--art-*` and `--page-art-*`) with no theme active.
- Glass "Eco" also paints an acrylic window as Mica, as it already did for acrylic panes.
- A `centre-bar` layout: the playback buttons centred, the progress line under them, the times at its ends. It is one more choice of the `bar-position` setting in Cinema, Ember, Glass, Greenroom and Palettes, and a setting of the Bottom playback bar snippet. Themes can move an element with the new `move-to` property. The line keeps clear of the song at every width, shows its thumb under the pointer, and the times always show, centred on it: `visibility: visible` now holds over Apple's visual states that hide an element. An element moved out of its template no longer takes that template's pointer states with it, which closed Apple Music at the first hover of the song.
- Floating panels: the folded sidebar's icons sit on one axis, and the Apple Music logo is centred on it. The
  column's corners stop at 16 pixels so the items at its ends are not cut. The "Apple Music" title moves 3 pixels
  down to meet the logo, and the "···" button of the open sidebar is level with it. Mirror keeps the whole logo on
  the axis of its folded column on the right, and the back arrow takes its place while it shows.
- New names: `.sidebar-selection`, `.sidebar-count`, `.sidebar-drag-indicator`, `.search-suggestions`, `.loading`,
  `.library-status`, `.cover-play-button`, `.explicit-group`, `.focus-ring` and `.popup-scrim`.
- Dialogs have names: `.dialog` (the box), `.dialog-scrim`, `.dialog-title`, `.dialog-content`, `.dialog-buttons`,
  `.dialog-primary`, `.dialog-secondary`, `.dialog-close` and `.dialog-checkbox`, found whatever the dialog's type.
- More names for Apple Music's own parts: the sidebar (`.more-button`, `.more-icon`, `.sidebar-subitems`,
  `.sidebar-chevron`, `.sidebar-item-image`, `.sidebar-footer-scroll`, `.sidebar-border`, `.account-badge`, `.topnav`),
  the search box (`.search-input`, `.search-placeholder`, `.search-button`, `.search-clear`), the banners
  (`.info-bar-icon`, `-title`, `-message`, `-content`, `-close`), the scroll bars (`.scrollbar-thumb`, `-track`,
  `-arrow`), Home (`.shelf-title-link`, `.shelf-title-text`, `.shelf-subtitles`, `.stacked-page-header`, `.page-scroll`,
  `.card-text-grid`), the playback bar (`.shuffle-icon`, `.repeat-icon`, `.airplay-icon`, `.transport-icon`,
  `.lcd-loading`, `.lcd-artwork-hover`), the full-screen player's controls, Apple's mini player (`.mini-player`...) and
  Apple's other views (AirPlay, reactions, equalizer, info panel, activity window...). `.lcd-more-button` named the
  sidebar's "..." button, not one of the display: it stays, and `.more-button` is its right name.
- `calc()` takes `min()` and `max()`, so a theme can cap a size.
- Fluent: an accent colour setting, Windows' blue or the colour of the artwork (play button, volume and progress).
- Mirror: a panel corners setting, from square to round (the default is the current look).
- `.card-poster` and `.card-poster-chin`: Home's large editorial tiles and the band under their text, which clip with a
  corner of their own and stayed rounded under square corners. Mirror's corners setting, Square corners and Round
  corners now reach them.
- The Marketplace writes a number setting's label on its slider, in the same text as the other settings' labels.
- Animated artwork (snippet, off by default): the window's background is the song's artwork in two slowly turning
  layers. It stops in pause and while Apple Music is hidden.
- On a new song, `content: artwork` and an `image` node with `src: "artwork"` cross-fade from the previous cover in
  500 ms; at once in eco glass or with Windows' animation effects off.
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
- `:current` matches the lyrics line being sung and its parts, line by line as the song goes:
  `.lyrics-line-content:current { background: ... }` lights it.
- Lyrics spotlight snippet: the line being sung stands on a soft tile of the song's colour in Apple's lyrics pane, on
  top of any theme.
- Compact sidebar snippet: rows of 32 pixels instead of 40, so more playlists fit. `.sidebar-entry` names Ampify's own
  entries at the bottom of the sidebar, which themes could not reach before.
- Glass and Ember: "Playback bar position" puts the bar at the top, Apple's own place, as in Cinema, Greenroom and
  Palettes. Glass keeps its capsule there, lined up with the pages and clear of the window's buttons.
- Progress line thickness snippet: the display's progress line from 1 to 6 pixels, with round ends. `corner-radius`
  now rounds a `Rectangle` too, such as a slider's track.
- New snippets: `hide-explicit` (the E badge), `centred-lyrics` (the lyrics in the middle of their panel),
  `large-album-artwork` (a size setting for the cover of an album or playlist page) and `hover-row-actions` (a song's
  favourite, add, download and reaction buttons appear under the pointer).
- A theme's rule for the playback bar's cover (`.lcd-artwork-image`) now wins over its rule for every cover
  (`.artwork`): every theme's bar cover had taken the radius meant for the grids.
- A page's header no longer flashes Apple's grey frost before the theme's colour.
- An album's or a playlist's Play button takes each theme's corners instead of Apple's pill, and Glass's play bar
  buttons no longer light up as ovals under the pointer.
- A song display a theme gives a width no longer runs under the playback buttons in a narrow bar (the star on the
  shuffle button): it shortens to end a clear space before them, and takes its width back with the room.
- Up Next's "Clear" and its autoplay toggle take the theme's accent instead of Apple's red and blue
  (`.queue-clear`, `.queue-autoplay`).
- Palettes: Catppuccin's Macchiato, Frappé and Latte, the light one, beside Mocha.
- Glass dark and Artwork: the cards of the pages, the Marketplace and Settings > Ampify are a white veil on the glass
  instead of a fixed night blue or slate, so they take the colours of the song playing.
- The lyrics and queue pane of the floating-panel themes fits its panel: its right edge was cut (the durations, the
  autoplay button, the History tab; most in Mirror).
- A state can sit on any part of a selector, not only the last: `.track-row:hover .favorite-button { opacity: 1 }`
  shows a song's buttons while the pointer is over its row, and gives them back when it leaves. `:active`, `:focus` and
  `:current` work the same way.
- Texts keep 4.5:1 under every theme with a dark scheme: Apple's shelf headers, captions and the search field's hint
  take the theme's secondary text instead of Apple's white at 51%, and a tile's subtitle shows the theme's secondary
  text at full opacity, not faded to 70%. Palettes Frappé and Latte get darker or lighter shades of their own family.
- Declutter: Apple's info banners and its logo in the display each have a switch, on by default as before.
- Square corners keeps Apple's round buttons and badges round.
- Palettes and Greenroom: the playback bar height setting works with the bar at the top too (8 less than at the
  bottom, so its default keeps Apple's height), and the panels start below it.
- New snippets, from Spicetify's most useful ones: `hide-buttons` (a switch each for the lyrics, Up Next, AirPlay,
  volume and download buttons), `dark-lyrics` (the lyrics and Up Next pane dark with light text, plain or from the
  artwork), `hover-search` (the search box fades out until the pointer reaches it, and stays while one types) and
  `minimal-fullscreen` (the full-screen player without title, times or lyrics, its buttons under the pointer only).
- Declutter can hide the account name, the moving bars beside the song playing, the cover at the top of albums and
  playlists, and the coloured backdrop or video behind pages.
- Progress line thickness also sets the volume slider's thickness. Spinning artwork can hold still and leave out its
  grooves, for a plain round cover. Progress mascot has a running cat and a rainbow trail on the played part.

### Extensions

- `Ampify.player.off(event, fn)` removes a listener, and `player.on` accepts Spicetify's `songchange` and
  `onplaypause` names.
- A manifest typo gets a suggestion: `unknown permission: storag (did you mean "storage"?)`.
- New example `audio-meter`: the loudness and the peak of the sound, in the log. Permission `audio.analyze`.
- Spicetify API coverage page: [docs/extensions/spicetify-api-coverage.md](docs/extensions/spicetify-api-coverage.md).
- The TypeScript types include `http.open` and `player.off`, and no longer declare a type twice.
- Three more interface nodes for apps and panels: `grid` (cells filled row by row, 1 to 12 columns), `tabs` with
  its `tab` nodes (the one that is `selected` is highlighted), and `menu` with its `item` nodes, opened as a flyout
  from a button. `Ampify.components` gets `Grid`, `Tabs` and `Menu`.
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

- Hover sidebar (`apple.sidebar = hover`): the hamburger button pins the sidebar open, as Apple Music's own mode does,
  and a second click folds it again. The pin is not saved. The hamburger shows a pin while pinned, and the folded
  column hides the Library and Playlists groups until the sidebar opens.
- The hover sidebar stays open while you type in its search field, and only while the window is in front; it no
  longer stays open on an idle field.
- The buttons of an Ampify dialog keep the text font: every button in it, its content included, not only the three
  of the template (a button added later showed WebDings again).
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
- Tests: the Marketplace's command list, the kit command limits, Ampify.color.playing and the process gain board are
  now tested. The bench checks the new Glass bar position, the hover sidebar, the Compact sidebar and the :current
  lyrics snippet without Apple Music. The stable-names check of the inspector is reported, not failed, until its survey.

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
