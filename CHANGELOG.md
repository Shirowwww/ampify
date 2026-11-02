# Changelog

## Unreleased

- Ampify speaks German, Spanish, Portuguese, Russian, Japanese and Simplified Chinese, besides English and French: the
  Settings card, the menus, the installer, the Marketplace, and every shipped theme, snippet and extension. Any text
  without a translation shows in English. The `language` setting (`ampify config language de`) chooses one whatever
  the Windows language.
- Manifests accept `translations` for de, es, pt, ru, ja and zh. Ampify 1.2.1 and older refuse those languages, so an
  element that uses them needs this version.
- Shorter, task-by-task documentation.
- Themes can draw bars that move with the music, with `content: bars(n)` on a `::before` or `::after`. The display
  equalizer snippet uses them, and keeps its GIF on older Ampify. The sound is analysed only while something shows it.
- Extensions of the interface see the sound too (`Ampify.audio`), and a button can change its label
  (`Ampify.ui.setLabel`). Two new examples: Volume percentage, in the play bar, and Listening stats, an app page with
  your top artists and songs.
- A new Cover ambience snippet tints the song's display with the colours of the artwork. The spinning artwork is now a
  round record, and square corners reach every panel.
- `@import "ampify:scrollbars"` keeps scroll bars inside rounded panels, without their arrows. Floating themes get it
  on their own.
- `docs/spicetify-parity.md` lists every Spicetify feature with its status in Ampify.
- Ampify's descriptions and captions are dimmed like Apple Music's own, so names stand out in the Settings card, the
  Marketplace and extension pages. Page titles use Apple's weight, and panels no longer repeat their name.

## 1.2.1 (2026-11-02)

- The Playback guard restarts a song that Apple Music leaves stuck, then goes back to the second it froze at: the music
  plays on about 2 seconds later. It used to skip to the next song and back, which Apple ignored while it was still
  loading the next song, so another song played instead. Switch it on in the Ampify card of Apple Music's Settings.
- The README lists what ships with Ampify.

## 1.2.0 (2026-11-02)

### Themes

- Eight new themes after the most used ones on Spicetify: Catppuccin, Comfy, Dracula, Dribbblish, Gruvbox, Nord,
  Rosé Pine and Tokyo Night. Midnight and Rose gold have a look of their own, and Greenroom puts the whole play bar at
  the bottom of the window, as Spotify does. Every theme shows a preview in the Marketplace.
- A theme can declare settings (numbers, switches, choices) and translate their labels. The Ampify card in Apple
  Music's Settings shows them, and chooses the theme and its scheme.
- The Ampify card gains two Apple Music settings: when notifications show (as Apple Music shows them, only when it is
  not in front, or never), and where the full-screen player opens (over the window, in place of the window, the whole
  screen with the taskbar, or the whole screen).
- The theme's accent reaches every selection mark, link and switch, and leaves with the theme. Extension panels take
  the theme's colours, stay above app pages and no longer cover the window's buttons.
- Themes and snippets can add images and animated GIFs to Apple Music's elements (`::before` and `::after` with
  `content: url(...)`), set letter spacing, italics, line height, alignment and trimming, hide an element with
  `display: none`, and use the selectors `+`, `~`, `:not()`, `:only-child` and `:nth-last-child()`. A property Ampify
  does not know gets a hint in the log. Three snippets show it: an animated equalizer, a spinning artwork and square
  corners.

### Extensions and Apple Music

- Seeking goes through Apple Music's own player, which ignores the Windows media seek. A new Playback guard
  extension, off until you switch it on, reloads a song that Apple Music leaves stuck at the same second.
- Discord presence shows the cover (local files too), a progress bar, clickable titles and two buttons of your choice.
  The Last.fm scrobbler and the other examples work out of the box. Extensions can send forms and read the artwork.
- Closing Apple Music's window can keep it in the notification area (setting `apple.close`).
- Only one Ampify kernel loads into an Apple Music process, even when two launchers are present.

### Tools

- `ampify publish <folder>` checks an element, uploads it and opens the pull request to the catalog.
- `ampify devtools` asks the running Apple Music for the element tree of the page on screen and waits for it.
- `ampify report` writes an anonymised diagnostic report, also offered by a button of the Ampify card.
- `ampify config` refuses unknown keys and wrong values, and `--dry-run` is honoured by every command that accepts it.
- The local remote refuses header lines that carry control characters.

### Project

- Ampify is free software under the GPL v3; themes, snippets, extensions and apps made for it keep their own license,
  and the examples are MIT. A website with a theme gallery, in English and French:
  [shirowwww.github.io/ampify](https://shirowwww.github.io/ampify/).
- Elements can carry a French name and description (`translations` in the manifest). The Marketplace and the Ampify
  card in Apple Music's Settings show them on a French Windows. All bundled elements are translated.
- `ampify theme names <inspector.json>...` replaces `ampify_theme_check --names`, and `ampify theme check` replaces
  the rest of that program, which is gone.
- Removed the settings `app_path`, `prefs_path` and `telemetry.block`, which nothing read, and the hidden
  `ampify path userdata`. `ampify help` and `docs/cli.md` now list the same commands.

## 1.1.0 (2026-11-01)

- Ampify loads with Apple Music each time it starts, from Apple Music's own icon, as Spicetify stays applied to
  Spotify. The installer starts a small background launcher now and at each Windows sign-in; the "Apple Music with
  Ampify" Start menu shortcut is gone. `ampify restore` turns this off and `ampify apply` turns it back on.
- Install in one line of PowerShell: `irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex`.
  The installer also puts `ampify` in your Path.
- A logo: Apple Music's tile and note as a puzzle piece. It is the icon of the programs, of the notifications and of
  the Ampify entry in Apple Music's sidebar.
- Fewer, better elements. The Marketplace keeps five themes, the four palettes and three example extensions, now
  named "(example)". Large text, Rounded buttons, Output gain and Now playing notify are gone, with the less useful
  extension examples.
- The repository is reorganized: the C++ code is under `src/`, the themes sit directly under `themes/`, and the
  catalog lives only in [ampify-catalog](https://github.com/Shirowwww/ampify-catalog).

## 1.0.2 (2026-11-01)

- The installer puts the Marketplace and Now Playing in your Ampify folder, in place of any older copy. A new install
  had no Marketplace at all, and an update kept the old page. `ampify update --apply` replaces them too.
- The Marketplace and Now Playing start until you turn them off. Other extensions and apps still wait for you to switch
  them on.
- The download is for users: the test kit (`Ampify-Test.exe`, `prepare-test.cmd`, `TEST-GUIDE.txt`) is no longer in it.
- The programs carry version information (file Properties > Details): product, publisher, description, version.

## 1.0.1 (2026-11-01)

- The Atmos leveler measures every song. Apple Music does not set a volume on every player, so the leveler did not
  always see the next song: that song went unmeasured, the song before was never closed, and its gain could stay on.
  A song on a player the leveler does not follow is now measured as stereo, and it ends the song before.
- `logs\AppleMusic.log` keeps its lines when Apple Music opens a `music://` link: the short second Apple Music process
  adds to the log instead of emptying it.

## 1.0.0 (2026-11-01)

The first complete version: themes, extensions, apps, snippets, the Marketplace and the `ampify` command line, each
checked in Apple Music.

### Included

- The installer `ampify_setup.exe`, and a clean uninstall from Windows Settings. Ampify never touches Apple Music's package.
  After an uninstall, Apple Music starts as before.
- The launcher and the kernel, loaded into both Apple Music processes (`AppleMusic.exe` and `AMPLibraryAgent.exe`), with
  safe mode: after two failed starts, Apple Music opens with no mods.
- JavaScript extensions, with the whole API (`Ampify.*`) and the React-style layer (`Ampify.React`, `Ampify.components`).
- Themes (colors and `user.css` rules), chosen in the Marketplace page. Spicetify `color.ini` files work as they are, and
  `ampify theme import` brings in a whole Spicetify theme.
- Stable names for about 120 zones of Apple Music (`.sidebar`, `.playbar`, `.card`...) and ready-made layouts
  (`@import "ampify:floating-panels"`, `"ampify:bottom-bar"`) with aligned edges. `ampify_theme_check --names` checks the
  names against inspector trees after an Apple Music update.
- Themes apply before a new page is drawn, with animated transitions, `:nth-child`, and four example themes: Greenroom
  (play bar at the bottom), Artwork, Glass and Midnight. A button keeps the theme's colours under the pointer and while
  pressed (Greenroom's white play button used to turn black when clicked). Rules accept `border-radius` as well as
  `corner-radius`, and `min-width`, `max-width`, `min-height`, `max-height`, `compact-pane-length`, and values can
  use `calc()`. `@media` rules follow the window's size. `@keyframes` and `animation` animate opacity and transform
  on the compositor, under `:hover` too; the `spinning-artwork` snippet turns the playback bar's artwork like a record. In a narrow window, `floating-panels` keeps the folded
  sidebar at Apple Music's 48 pixels, whole, with the same gap to the pages as when it is open, and `bottom-bar` keeps
  the song in view, with the Lossless and Dolby Atmos badge at the far end of the display, clear of the title. The
  window's own buttons
  (minimize, maximize, close) follow the theme's light or dark mode: they were black on dark themes on a PC in light
  mode.
- The inspector shows what each element's visual states change. It no longer walks Apple Music's resource dictionaries,
  which made the app crash at the next theme change.
- Now Playing: a button of the playback bar that opens Apple Music's own full-screen player as a window laid over the
  main one, without a title bar (`Ampify.ui.applePlayer.open()`). The view Ampify drew before, the cover on a blurred
  background with the lyrics in time (Apple Music's own, or LRCLIB's), is now the `lyrics-view` example.
- The Marketplace page, opened from the Ampify entry at the bottom of the sidebar. It reads the public catalog,
  Shirowwww/ampify-catalog. An app's page mounts anew each time it opens, as a Spicetify app does: the Marketplace
  showed the theme of its first opening after a change made elsewhere. The page sits on the theme's panel in place of
  Apple Music's page, and Apple Music's own navigation (sidebar, back arrow, search) leaves it: it used to stay over
  the page the user had just chosen. Its main buttons keep the accent colour under the pointer and after a theme
  change: they turned plain, or kept the previous theme's colour.
- The "Ampify" card in Settings > Playback: one switch per extension, with an immediate effect.
- The Atmos leveler: Dolby Atmos songs are brought to the level of stereo songs, without ever moving the volume slider.
- The `ampify` command line and the catalog (`ampify market`, `ampify install`, `ampify update`).
- The player: current song, play and pause, next song.
- The installer puts `ampify.exe` next to the kernel, because the Marketplace page runs it.
- `Ampify.http.get` and `post` no longer block the thread that runs JavaScript: a slow request does not freeze timers or
  events, and an element may have 4 requests in flight.
- Themes reach the flyouts, menus and tooltips, which Apple Music opens outside its window: the volume flyout, the "..."
  menus and their submenus, the tips under the buttons (`.flyout`, `.menu`, `.menu-item`, `.menu-separator`,
  `.tooltip`). The volume slider has its names (`.volume-slider`, `.volume-mute-button`): its track, its fill and its
  thumb keep the theme's colours under the pointer and while dragged, and Greenroom draws it white on grey. A `:hover`
  colour on a menu entry or a button is what it shows under the pointer, and an entry stays lit while its submenu is
  open. Keeping those colours no longer stops the window (the first opening of the volume flyout froze it for 0.7 s),
  and removing a theme gives back Apple Music's colours under the pointer (a slider stayed white on a white flyout).
- A theme with a button template no longer stops Apple Music: the search box's buttons have a negative padding, which
  a template's border refuses. A negative padding, border or corner in `user.css` is refused with a line in the log.
- A theme starts on the `defaultScheme` of its manifest (Midnight opened in its light scheme). A font size can be
  relative to each element, `120%` or `1.2em`: Large text now enlarges every text and keeps titles above body text.
  Sizes are no longer written again every 3 seconds when WinUI reads them back rounded.
- A manifest can name a `preview` picture and up to five `tags`, which the catalog carries to the Marketplace;
  `tools/pack.py` publishes the picture next to the archive. Every theme of the public catalog has its preview, and
  the catalog now offers all seven example themes.
- The Marketplace is a grid of cards with their preview, in tabs (Themes, Extensions, Apps, Snippets, Installed), with
  search, tags, a clear state on each card ("In use · update to 1.1.0"), the scheme menu, and a detail view. An element
  that asks for permissions installs off and opens its detail view, where it is turned on under the list of what it
  may do; the permissions an update adds are marked there before it runs. A card never grants a permission itself. A
  failed uninstall stays on the detail view with ampify's error, and "Refresh" no longer clears the catalog's warning.
- Kit pages: `title` and `subtitle` texts follow Apple Music's sizes, buttons and menus keep their width on the left
  instead of stretching after a render, and the keys of `Ampify.React` reach the native controls, so a line that
  appears above a list no longer rebuilds the whole list. An open page takes the new theme's accent at once.
- A theme that gives only its accent also gets its hover and pressed accents (Glass and Midnight showed Apple Music's
  red under the pointer), and text on a light accent turns black so it stays readable. `ampify theme set` keeps the
  scheme when the same theme is set again, and keeps `auto` for every theme; another theme starts on its own default.
- The "Open the Marketplace" button of the Ampify card in Settings is readable again: Apple Music's accent style drew
  its text in a symbol font. A theme no longer reports values refused on elements that are gone or never realized,
  and Greenroom loads with none refused.
- Menu entries: `Ampify.ui.menu.addItem("app", ...)` adds an entry to Apple Music's main menu, and `"context"` one to
  the menu of a song, an album or a playlist (right click, the "..." of a row), whose `onClick` receives the item's
  name. They take the look of Apple Music's entries and leave the menu once it closes.
- `ampify new` starters show something at once: the extension adds a playback bar button that shows the song playing
  in a notification, the app opens a page with a counter from its own sidebar entry, the theme uses the stable names,
  and the snippet rounds the card artwork. Two five-minute guides walk through them (`docs/extensions/guide.md`,
  `docs/themes/guide.md`). An app page is titled with the app's name instead of its address.
- `ampify dev` on a theme shows a save in under a second: the theme goes over itself, and only what changed is written.
  It used to remove the whole theme and apply it again, which took about 4 seconds.
- The theme report and the inspector show a corner radius in CSS order (top left, top right, bottom right, bottom left).
- An answer to `Ampify.http.get` or `post` that came back while `main.js` was still running is no longer lost: its
  promise used to never settle.
- `user.css` reads `:active` (while pressed) and `:focus` or `:focus-within` (while the keyboard focus is inside), each
  with its own cascade like `:hover`. An `:active` colour on a button goes to its pressed state only.
- Files saved with a byte order mark (Notepad's "UTF-8 with BOM") are read: manifests, `user.css`, `color.ini`,
  settings. A snippet can be a `user.css` alone, without `resources.xaml`, and `ampify new snippet` makes one.
- An extension can declare its settings in its manifest (`settings`: toggles, choices, texts, numbers). The Ampify card
  in Settings > Playback shows them under its switch, a change restarts the extension, and `Ampify.config.get(key)`
  gives the manifest's default until the user changes it. The extension starter declares one. A value edited by hand
  into something that does not read as its type (an empty number, a toggle set to `maybe`) gives the default, and an
  option or a text default with spaces at either end is refused, since `settings.txt` would not give it back.
- Scroll bars take a theme's colours: `.scrollbar` gives the thin line its `color`, and the wide thumb and its track
  their `:hover` and `background` colours. The inspector lists the keys of selected rows, which `colors.ini` colours
  (`ListViewItemBackgroundSelected` and its pointer and pressed keys).
- The log names the first reason a value was refused for a pointer state, with its code, next to the count. A value an
  element refuses under the pointer no longer keeps its other values from showing, or from going back when the pointer
  leaves.
- An answer to `Ampify.http.get` or `post` whose body takes more than 15 seconds to arrive ends in a `network` error
  instead of holding one of the element's 4 requests.
- `scripts/build-msvc.ps1` runs the compiler in the console's OEM code page and checks the prefix of its include notes:
  with a localized Visual Studio, a changed header did not rebuild the files that include it. A build folder whose
  prefix differs is configured again once.
- `Ampify.player.queue.list()` gives Apple's queue section by section (history, current, added by hand, next,
  autoplay), with each song's album and catalog id, read through Apple's own play queue. `queue.remove(id)` removes an
  entry still to come, which leaves Apple's "Up Next" panel at once. `queue.add` answers `false` with the reason: Apple's
  player ignores the songs Ampify hands it (`knowledge/apple-music-queue.md`).
- `Ampify.fonts.roles()` and `Ampify.fonts.style(name)` give extensions the font roles of the applied theme, the
  counterpart of Spicetify's `getFontStyle`. `Ampify.color.fromArtwork(url)` gives the palette of an artwork of Apple
  Music, downloaded and decoded in the kernel. `Ampify.lyrics.current()` is `Ampify.player.lyrics()` as a promise.
- A setting changed in the Ampify card is no longer lost when an extension saves one of its own keys at the same moment:
  every writer of `settings.txt` changes its keys in the file as it is now, instead of saving an older copy.

### Preview

The rest exists but is not at the same level yet. It is marked "preview" in `docs/extensions/api.md` or off by default:
the equalizer, the gain and audio analysis, Discord presence, scrobbling, the local remote, MIDI, the notification-area
icon, side panels and windows, ES modules, and the engine process (`processes: ["engine"]`).

### Known limits

- Tested on Apple Music 1.1540 and 1.1792. The shipping build injects the kernel only on the versions of its list
  (`core/version_gate.hpp`, or the `versions.txt` downloaded by `ampify versions update`). On any other version, Apple Music
  opens normally with no mods.
- The places of panels, of the Ampify card and of the theme choice may still move.
- The Atmos leveler was checked by ear on a PC with Dolby Atmos. When several songs are started one after the other,
  it can miss some of them: those songs are not measured.
- `logs\AppleMusic.log` starts over when Apple Music opens a `music://` link, because Windows runs a short second
  Apple Music process for it.
- On Apple Music 1.1792, Apple keeps the cover's address internal, so `track.artwork` is empty (Now Playing takes the
  cover from the iTunes Search API), and Apple's lyrics only come once its lyrics panel has been open for the song.
- Apple Music builds its queue the first time its "Up Next" panel opens in a session: until then
  `Ampify.player.queue.list()` holds the current track only, and says so.
- Outside 1.0, with the reason in `docs/extensions/api.md` (section 11): the library and playlists, adding to the queue,
  calls to Apple's internal services, and blocking telemetry. Their permission names are reserved.
- The binaries are not code-signed: Windows SmartScreen can ask for a confirmation.
