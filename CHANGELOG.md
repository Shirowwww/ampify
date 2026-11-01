# Changelog

## 0.5.0 (unreleased)

The first version meant to be tested in full on a real PC. It is not published yet: it waits for a listening check of the
Atmos leveler.

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
  use `calc()`. `@media` rules follow the window's size. In a narrow window, `floating-panels` keeps the folded
  sidebar at Apple Music's 48 pixels, whole, with the same gap to the pages as when it is open, and `bottom-bar` keeps
  the song in view. The window's own buttons
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

### Preview

The rest exists but is not at the same level yet. It is marked "preview" or off by default: apps (added pages), snippets,
the equalizer, audio analysis, Discord presence, scrobbling, the local remote, MIDI and the notification-area icon.
The fullscreen view (`Ampify.ui.fullscreen`, permission `ui.fullscreen`) and the style attributes of the interface
primitives (`layer`, backgrounds and acrylic, text size and color, opacity, scale and transitions) are in preview too.

### Known limits

- Tested on Apple Music 1.1540 and 1.1792. The shipping build injects the kernel only on the versions of its list
  (`core/version_gate.hpp`, or the `versions.txt` downloaded by `ampify versions update`). On any other version, Apple Music
  opens normally with no mods.
- The places of panels, of the Ampify card and of the theme choice may still move.
- The Atmos leveler passed the automated tests, and still needs a listening check on a real PC.
- On Apple Music 1.1792, Apple keeps the cover's address internal, so `track.artwork` is empty (Now Playing takes the
  cover from the iTunes Search API), and Apple's lyrics only come once its lyrics panel has been open for the song.
- The binaries are not code-signed: Windows SmartScreen can ask for a confirmation.
