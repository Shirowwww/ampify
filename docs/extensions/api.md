# The Ampify API

The reference for the `Ampify` object that extensions, apps and theme scripts use. To start, read
[Your first extension](guide.md). The manifest format is in [manifest.md](manifest.md). Coming from Spicetify? See [what maps to what](spicetify-api-coverage.md).

Each part is tagged **[stable]** (will not change without a new major API version) or **[preview]** (works, but can
still change).

## 1. Principles

- Elements are JavaScript run in a sandbox: no file system, no native calls, and network only to the hosts the
  manifest declares.
- An element only reaches what the API exposes, and each capability beyond the basics needs a permission the user sees
  before switching it on.
- The engine is [quickjs-ng](https://github.com/quickjs-ng/quickjs), one instance per element. Interfaces are drawn
  with Apple Music's own WinUI controls; there is no WebView.

## 2. Processes

Apple Music runs as two processes. `Ampify.process` tells an element where it runs:

| `processes` value | Process | What runs there |
|---|---|---|
| `ui` (default) | `AppleMusic.exe` | The interface: buttons, panels, pages, the player |
| `engine` | `AMPLibraryAgent.exe` | Sound: `audio.*`, MIDI, the local remote |

The manifest's `processes` picks one or both. An element in both runs twice; only one of them should write its storage.

## 3. Life cycle

1. At start, Apple Music reads each folder of `extensions`, `apps` and `themes` in `%USERPROFILE%\Ampify`. A folder
   without `manifest.json`, or whose name starts with a dot, is ignored. An app in `extensions` (or the reverse) is
   refused.
2. The manifest is checked: schema, `id` equal to the folder name, supported `kitApi` major version, declared files
   present. A failure gives the state `Failed` and a log line; the other elements go on.
3. An element starts only if it is switched on (`extensions.<id>.enabled`, `apps.<id>.enabled`; community elements
   start off). A theme's `main.js` runs while the theme is active and `theme.parts.script` is on.
4. `main` is evaluated. An exception gives `Failed` without stopping the others.
5. On stop, the functions given to `Ampify.onStop` run in reverse order.
6. `ampify` can start, stop and restart an element without restarting Apple Music (see [cli.md](../cli.md#files)).

States: `Off`, `Running`, `Failed`, and `Blocked` (switched on, but Ampify is in safe mode or on an untested Apple
Music version).

An app and an extension may share an id; they are then linked (section 5.8). Inside the kernel, the app is `app:<id>`.

### 3.1 Several files: ES modules [preview]

With `"type": "module"` in the manifest, `main.js` is an ES module and can import other files of its folder:

```js
import { format } from "./lib/text.js";
import settings from "./settings.mjs";
Ampify.log(format(settings.title));
```

- Only relative paths (`./x.js`, `../x.js`) that stay inside the element's folder. No bare names, absolute paths, `\`
  or `:`.
- Names are letters, digits, `.`, `_` and `-`, with no leading dot. Only `.js` and `.mjs`, 1 MB at most each. No
  symbolic links.
- A refused import throws a `ReferenceError` starting with "import refused". A classic script can still call
  `import("./x.js")` under the same rules. Modules share the element's permissions.
- An error thrown in a handler (a timer, a listener, a click) is written to the log and the other handlers go on. An
  element that fails 5 times within a minute, including running out of time or memory, is stopped and the log says
  "stopped after 5 errors in a minute". It starts again when it is turned off and on, or with Apple Music.

## 4. Permissions

A function whose permission is not declared throws `PermissionDenied` and does nothing.

| Permission | What it gives | Status |
|---|---|---|
| (none) | `Ampify.log`, `onStop`, `config`, `uri`, `locale`, `i18n`, `icons`, `crypto`, `color`, `fonts`, `link`, `remote` | [stable] / [preview] |
| `storage` | `Ampify.storage`, one JSON file per element | [stable] |
| `notifications` | `Ampify.notify` | [stable] |
| `player.read` | Player state, events, lyrics, queue, artwork, `audio.volume` | [stable] |
| `player.control` | Play, pause, next, previous, seek, shuffle, repeat, volume, queue changes | [stable] |
| `audio.analyze` | Spectrum, levels, tempo and loudness of the sound | [preview] |
| `audio.gain` | `Ampify.audio.setGainDb`, a gain on the samples | [preview] |
| `audio.eq` | A 10-band equalizer | [preview] |
| `ui.topbar`, `ui.playbar`, `ui.sidebar`, `ui.menu`, `ui.popup` | Buttons, sidebar entries, menu entries, dialogs | [stable] |
| `ui.panel`, `ui.window` | A side panel, a secondary window | [preview] |
| `ui.fullscreen` | A view over the whole window, and Apple's own full-screen player | [stable] |
| `ui.page` | App pages and navigation | [stable] |
| `ui.keyboard` | Global or window shortcuts | [preview] |
| `ipc.discord` | Discord presence (also needs the setting `discord.enabled`) | [preview] |
| `midi.input` | A MIDI controller (also needs `midi.enabled`) | [preview] |
| `ui.tray` | Entries in the Ampify notification-area icon (also needs `tray.enabled`) | [preview] |
| `fs.output` | A text file in `output\<id>\` of the Ampify folder, and nowhere else | [preview] |
| `network:<host>` | `Ampify.http` to that host only, over https | [stable] |
| `library.read` | `Ampify.library.tracks`, the tracks of the library with their play counts | [preview] |
| `library.write`, `services.read` | Reserved names: a manifest that declares them loads, but no function uses them yet | Reserved |
| `kit.manage` | `Ampify.kit.run`, which runs `ampify.exe`. Only the Marketplace holds it; the catalog refuses it elsewhere | Internal |

Discord, the tray icon, the remote and MIDI are off until the user turns them on. Switching on an element that needs
`ipc.discord`, `ui.tray` or `midi.input` turns on the matching service; switching it off leaves the service on.

`fs.output` writes `.txt`, `.json`, `.csv`, `.html` or `.xml` files (256 KB and 16 files at most) that another
program can read, such as an OBS text source. The element cannot read them back or delete them.

## 5. The global `Ampify` object

Asynchronous functions return a `Promise`.

### 5.1 Base [stable]

```js
Ampify.version              // Ampify's version
Ampify.kitApi               // "1.0": the API version
Ampify.process              // "ui" or "engine"
Ampify.log(message)         // writes to the log, prefixed with the element id
Ampify.onStop(fn)           // fn runs when the element stops
Ampify.locale               // the app locale, e.g. "en-US"
```

Texts in several languages [preview]:

```js
Ampify.i18n.language                    // "de": the language Ampify shows, as two letters
Ampify.i18n.define({ en: { hello: "Hello {name}" }, de: { hello: "Hallo {name}" } })
Ampify.i18n.t("hello", { name: "Lea" }) // the text in that language, else English, else the key
Ampify.i18n.t("kit.tray.next")          // Ampify's own texts, prefix kit.
```

`Ampify.i18n.language` is one of `en`, `fr`, `de`, `es`, `pt`, `ru`, `ja`, `zh`, or another code when Windows uses a
language Ampify does not translate. It follows Windows' regional format, or the `language` setting.

### 5.2 Settings and storage [stable]

```js
Ampify.config.get(key, default)     // reads extensions.<id>.<key> (apps.<id>... for an app, themes.<id>... for a theme)
Ampify.config.set(key, value)       // writes it; an element only reaches its own keys
Ampify.storage.get(key, default)    // a JSON value in storage\<id>.json (1 MB), permission storage
Ampify.storage.set(key, value)
Ampify.storage.remove(key)
```

For a setting declared in the manifest, `config.get(key)` without a default gives the manifest's default, typed (a
boolean for a toggle, a number for a number). A change in Ampify's settings page restarts the extension.

### 5.3 Player [stable]

```js
Ampify.player.state()
// -> { playing, positionMs, durationMs,
//      track: { id, title, artist, album, variantId, atmos, lossless, queueItemId, artwork, artworkColor } | null,
//      source: "apple" | "smtc" | "" }
Ampify.player.on("trackchange" | "statechange" | "lyricschange", fn)   // "songchange" and "onplaypause" are Spicetify's names (onplaypause also fires on a position jump)
Ampify.player.off(event, fn)   // removes a listener added with on
Ampify.player.lyrics()      // -> null | { source: "apple", synced, language,
//                                         lines: [{ start, end, text, words: [{ start, end, text }] }], text }
Ampify.player.artwork()     // -> Promise<null | { type: "image/jpeg" | "image/png", base64 }>   [preview]
Ampify.player.play()        // true when the app accepted the command
Ampify.player.pause()
Ampify.player.toggle()
Ampify.player.next()
Ampify.player.previous()
Ampify.player.seek(ms)
Ampify.player.shuffle()     // -> true | false | null        [preview] Apple's own shuffle and repeat buttons
Ampify.player.repeat()      // -> "off" | "all" | "one" | null     (permission player.read, interface process)
Ampify.player.setShuffle(on)        // true once the player is in that state    (permission player.control)
Ampify.player.setRepeat(mode)       // "off" | "all" | "one"
Ampify.player.toggleShuffle()       // flips shuffle
Ampify.player.toggleRepeat()        // off, all, one, off...
Ampify.player.setVolume(volume)     // 0 to 1: Apple's player volume, interface or audio process, [preview]
Ampify.player.queue.list()  // -> { available, complete, source, reason,
//                                   items: [{ id, title, artist, album, durationMs, section, catalogId }] }
Ampify.player.queue.add(catalogId, { position: "next" | "last" })  // [preview] answers false for now
Ampify.player.queue.remove(itemId)                                 // [preview]
```

- `track.id` is computed from title, artist and album (`t-` and 16 characters), the same whatever the source.
  `track.queueItemId` is Apple's queue entry, so the same song played twice has two.
- `track.variantId` is `"lossless"`, `"hi-res-lossless"`, `"dolby-atmos"`, `"spatial"`, `"surround"`, `"other"`, or
  empty. `track.artwork` is an https cover address or `""` (Apple Music 1.1792 keeps it internal, so it is empty
  there); `track.artworkColor` is `"#RRGGBB"` or `""`.
- `source` is `"apple"` when Ampify reads Apple's own player (in `AppleMusic.exe`), `"smtc"` for Windows' media
  controls, `""` when unknown.
- `statechange` fires when playback starts or stops, the length becomes known, or the position jumps. Between events
  `state().positionMs` moves on: read it on a timer to follow the song. Apple Music 1.1792 gives no position of its
  own while playing, so Ampify counts the time played, and sets it again each time Apple's lyrics pane lights up
  another line. While that pane is open, a jump, a repeated song or a slow start are caught up at the next line;
  closed, they show once Apple reports a position again.
- `lyrics()` is filled once Apple Music has loaded the song's lyrics, which it does when its lyrics pane opens.
- `lyrics()` gives Apple Music's own lyrics, or `null` until Apple has them. Times are milliseconds. With
  `synced: true` each line has `start`, `end` and, when Apple times them, `words`. `lyricschange` fires when they
  arrive.
- `artwork()` gives the cover Windows shows in its media flyout. Right after `trackchange` it may still be the
  previous one for a second or two.
- Commands go through Windows' media controls, then a media key. `seek` uses Apple's player. With the setting
  `player.appleCommands = 1`, play, pause, next and previous use Apple's player first.
- `queue.list()` gives Apple's "Up Next" by `section` (`history`, `current`, `next-added`, `next`, `autoplay`), 500
  entries at most. Apple Music builds its queue the first time that panel opens; before that, only the current track,
  with `complete: false` and the reason. `queue.remove(id)` removes an entry still to come.
- `queue.add` answers `false`: Apple's player ignores the songs handed to it
  ([knowledge/apple-music-queue.md](../../knowledge/apple-music-queue.md)).
- Shuffle and repeat press Apple's own buttons of the main window, so Apple Music keeps one state, and read them back.
  They exist in the interface process (`"processes": ["ui"]`); in the audio process the readers answer `null` and the
  setters `false`. They answer `null` while the window does not have its buttons (another Apple Music version). Apple
  redraws its repeat button on its own turn: the first press after Apple Music starts can take about 3 seconds, the next
  ones a tenth of a second; a setter waits for the answer up to 4 seconds and gives `false` after that.
- `setVolume` sets Apple's player volume. From the interface process (`"processes": ["ui"]`) it moves Apple's own
  volume slider, so the slider and the speaker icon redraw and the player follows, as if the user had dragged it; the
  slider is in decibels, so `0.5` is not the middle of it (30 dB range: the slider shows about 80 % for a gain of 0.5).
  From the audio process (`"processes": ["engine"]`) it sets the player's volume directly and Apple's slider keeps
  showing the old value until Apple moves it. It answers `false` until a song has started, because Apple sets its
  volume then; the log says why when it fails. `audio.volume()` follows it.

### 5.4 Audio [preview]

Sound is processed in `AMPLibraryAgent.exe` (`"processes": ["engine"]`), on the audio hooks of the Atmos leveler,
which go in by themselves when the interface asks for the sound. The interface (`AppleMusic.exe`) reads what that
process shares: `onFrame`, `analyze()`, `loudness()` and `volume()` work there too, about 100 ms behind. The gain and
the equalizer stay in the engine: in the interface they answer `false` and `eq.bands()` is empty, without throwing.

```js
Ampify.audio.setGainDb(db)          // gain on the samples, capped by audio.maxGainDb, back to 0 at stop
Ampify.audio.onFrame(fn)            // fn({ bands, rms, peak, time }), up to 20 times a second
Ampify.audio.analyze()              // -> { tempo, confidence, loudness: { momentary, shortTerm }, rms, peak, bands, frequencies }
Ampify.audio.loudness()             // -> { momentary, shortTerm } in LUFS, or null
Ampify.audio.volume()               // -> the player's gain, 0 to 1, or null (permission player.read)
Ampify.audio.eq.setBand(index, gainDb)   // 10 bands, 31 Hz to 16 kHz, +/- 12 dB
Ampify.audio.eq.preset(name)        // "flat", "rock", "jazz", "bass-booster"...
Ampify.audio.eq.reset()
Ampify.audio.eq.bands()             // -> [{ frequency, gainDb, q, type }]
Ampify.audio.eq.presets()           // -> preset names
```

- `bands` are 16 log-spaced bands from 40 Hz to 16 kHz, each 0 (-80 dBFS) to 1 (0 dBFS). `rms` and `peak` are linear.
- Loudness follows BS.1770 (400 ms and 3 s), measured before the gain and the equalizer.
- `volume()` is known once Apple Music has set it, usually when the first song starts. Apple Music's slider is in
  decibels: its position p gives a gain of 30 × (p − 1) dB, so half way is about 0.18.
- Order: gain, then equalizer, then a soft limiter. A flat equalizer leaves the samples untouched. There is one
  equalizer per process; the last element that sets it wins.
- Scripts only receive these numbers, never the samples, and nothing is written to disk.

### 5.5 Interface [stable]

An interface is a tree of nodes `{ type, ...attributes, children }`, written directly or with `Ampify.React`
(section 5.9). Ampify draws it with WinUI controls and, on each render, only changes the nodes that changed (matched by
position or `key`). Each node gets the AutomationId `AMPIFY_<element>_<node>`. Limits: 8 levels, 300 nodes, 500
characters per text; attributes are plain values.

| Node | Attributes |
|---|---|
| `stack`, `row`, `list` | `gap`, `children` |
| `layer` | `children`, stacked on top of each other |
| `card` | `title`, `subtitle`, `onClick`, `children` |
| `text` | `text`, `variant` (`title`, `subtitle`, `body`, `caption`) and the text style below |
| `button` | `label`, `icon` (SVG), `disabled`, `variant` (`primary`, `secondary`, `tertiary`), `onClick` |
| `toggle` | `label`, `checked`, `disabled`, `onChange` |
| `slider` | `label`, `min`, `max`, `step`, `value`, `disabled`, `onChange` |
| `textfield` | `label`, `value`, `placeholder`, `disabled`, `onChange` |
| `dropdown`, `option` | `label`, `value`, `disabled`, `onChange`; children are `option` nodes with `value`, `label` |
| `image` | `src` (https, a file path, or `"artwork"`: Apple's image of the song playing, following it), `label`, `onClick`, `fit` (`cover`, `contain`, `fill`), `cornerRadius`, `width`, `height` |
| `icon` | `icon` (SVG), `label`, `onClick` |
| `lyrics` | `size`, `color`, `placeholder`, `onChange`: the lyrics of the song playing, lit word by word as Apple's own view does, the current line larger and kept in view; `placeholder` shows while the song has none; a click on a line gives its start in ms to `onChange` |
| any | `tooltip`, `tooltipPlacement` (`top`, `bottom`, `left`, `right`), `tooltipDelay` (0 to 10000 ms) |

A `card`, `image` or `icon` with an `onClick` works from the keyboard as well: Tab stops on it, Enter or Space clicks
it, and a card lights up while it has the focus as it does under the pointer.

| Where | Attribute | Values |
|---|---|---|
| any | `opacity`, `scale` | 0 to 1; 0 to 10 around the center |
| any | `transition` | `"opacity 400ms"`, `"scale 0.3s"`, `"translation 200ms"`, `"all 300ms"` (comma-separated, 10 s max) or `"none"` |
| any | `align`, `valign` | `start`, `center`, `end`, `stretch` (on a `text`, `align` is `left`, `center`, `right`) |
| any | `grow` | `true`: takes the space left in its `row` or `stack` |
| `stack`, `row`, `layer`, `card` | `background` | `#RRGGBB`, `#AARRGGBB`, `acrylic`, or `acrylic(#AARRGGBB, 0.6)` |
| `stack`, `row`, `layer`, `card` | `padding` | A number, or `"top,right,bottom,left"` |
| containers, `image` | `cornerRadius`, `width`, `height`, `minWidth`, `maxWidth` | Pixels |
| `text` | `size`, `weight`, `color`, `wrap`, `maxLines` | 8 to 120 px; 100 to 900; `#RRGGBB`; `false` for one line; 0 to 100 |

```js
Ampify.ui.topbar.addButton({ label, icon, onClick, active })   // no onClick: a text that only shows something
Ampify.ui.playbar.addButton({ label, icon, onClick, active })  // same; any other onClick than a function throws
Ampify.ui.sidebar.addItem({ label, icon, onClick, active })   // icon: a Segoe Fluent Icons glyph; returns the id
Ampify.ui.menu.addItem("context" | "app", { label, icon, onClick })
Ampify.ui.menu.addItem("context" | "app", { label, icon, items: [{ label, icon, onClick }, ...] })  // a submenu
Ampify.ui.popup.open(view)
Ampify.ui.fullscreen.open(view)        // permission ui.fullscreen; called again, updates in place
Ampify.ui.fullscreen.close()
Ampify.ui.fullscreen.onClose(fn)       // returns a function that removes fn
Ampify.ui.applePlayer.open()           // permission ui.fullscreen: Apple Music's own full-screen player
Ampify.ui.miniPlayer.open()            // Ampify's mini player: a small window on top (no permission)
Ampify.ui.miniPlayer.close()
Ampify.ui.panel.register(id, view)     // a 380 px column on the right; it slides in from the right
Ampify.ui.panel.remove(id)             // slides it back out; register brings it back
Ampify.ui.panel.onClose(id, fn)        // the user closed it with its Close button: it is removed (register brings it
                                       // back, a React root unmounts) and fn runs; returns a function that removes fn
Ampify.ui.title.set(text)              // title of the element's panel, page and popup
Ampify.ui.setLabel(id, label)          // new text of a top bar or play bar button, by the id addButton returned
Ampify.ui.setActive(id, on)            // on or off: a button or sidebar entry that is on takes Apple Music's accent colour
Ampify.ui.page.register(path, view)    // for an app
Ampify.ui.navigate(path)
```

- Apple Music has no separate top bar, so `topbar` buttons go next to the lyrics and queue buttons, like `playbar`.
- `menu.addItem("app", ...)` adds to the main menu (the "..." next to "Apple Music"); `onClick` gets `{}`.
  `"context"` adds to the menu of a song, album or playlist; `onClick` gets `{ title, item }`: `title` is the item's
  name, `item` is what Apple Music's own data says about it, `{ kind, id, canonicalId }` (`kind` is `song`, `album`,
  `artist`, `composer`, `genre`, `playlist`, `station`, `video`, `concert` or `container`; `id` is the catalog id and
  `canonicalId` Apple's own identifier, each left out when Apple gives none: a song of the user's library often has
  neither, only local identifiers). `item` is missing when the menu has no such data behind it: then only `title` is
  there, and comparing it with `Ampify.player.state().track` is all that is left. With `items` the entry is a submenu holding up to 20 entries, one level only; it has no `onClick` of its own.
- A button with `active: true`, or switched by `setActive`, is drawn in the accent colour and reads "on" to screen
  readers: a toggle such as a sleep timer or a lyrics mode shows its state at a glance. A sidebar entry that is on also
  gets the bar Apple Music puts before its selected item, and it is on by itself while its element's page is open.
- The full-screen view covers the window, above everything. Escape or its Close button closes it. One at a time: the
  last element that opens it wins, and `onClose` runs when the user closes it or another element takes it.
- `applePlayer.open()` opens Apple's full-screen player where the user chose in Ampify's settings page (`apple.fullscreen`).
  If Apple Music refuses, nothing opens and the log says why.
- `miniPlayer.open()` and `close()` open or close Ampify's own mini player: the cover, the title, the play, previous and
  next buttons, and the progress, in a small window that stays on top. It remembers its place. The user opens it with
  the shortcut `mini.shortcut` (Ctrl+Alt+M) too, and the "Mini player" entry of the sidebar. Its colours come from the theme (`--mini-*`, see `docs/themes/names.md`).
- An app page takes the place of Apple Music's page, with a Back button; Apple Music's own navigation closes it.
- A `lyrics` node draws itself from Apple's player at the screen's pace: the element renders it once, with no timer of
  its own. Give it room, as a growing child of a `stack` of fixed `height`. An `image` with `src: "artwork"` shows
  the cover loaded large by Apple's own loader, not the play bar's small one.
- `ReactDOM` unmounting a `panel:` target removes the panel, as `panel.remove` does.
- Extensions cannot inject raw XAML. Themes are the only elements that ship XAML.

### 5.6 Services [preview]

```js
Ampify.notify({ title, text })                      // permission notifications
Ampify.keyboard.bind("Ctrl+Alt+L", fn)              // permission ui.keyboard
Ampify.http.get(url, { headers }) -> Promise<{ status, text }>   // [stable] permission network:<host>
Ampify.http.post(url, body, { headers }) -> Promise<{ status, text }>
Ampify.http.postForm(url, fields) -> Promise<{ status, text }>   // multipart/form-data
Ampify.http.gifFromVideo(url, { side, fps, seconds }) -> Promise<{ type, base64 }>   // an MP4 made a looping GIF
Ampify.http.open(url) -> boolean                    // opens the page in the browser, an Apple Music link in Apple Music
Ampify.crypto.md5(text) -> hexadecimal string
Ampify.uri.parse(text) / Ampify.uri.build(...)      // [stable] music:, itms: and https://music.apple.com links
Ampify.icons.get(name) -> SVG string
Ampify.icons.names() -> string[]
Ampify.color.extract(pixels, width, height)         // RGBA pixels -> PROMINENT, VIBRANT, DARK_VIBRANT, LIGHT_VIBRANT,
                                                    //   DESATURATED, VIBRANT_NON_ALARMING, palette
Ampify.color.fromArtwork(url) -> Promise<colors>    // an https://*.mzstatic.com artwork, same keys
Ampify.color.playing() -> Promise<colors | null>    // the cover playing now, library songs too (player.read)
Ampify.fonts.roles() -> { [role]: { family, weight, size, fromTheme } }
Ampify.fonts.style(name) -> { role, family, weight, size, fromTheme } | null
Ampify.lyrics.current() -> Promise<lyrics | null>   // player.lyrics() as a promise, permission player.read
Ampify.library.tracks() -> Promise<track[]>         // permission library.read
//   track: { persistentId, title, artist, album, albumArtist, genre, composer, year, playCount, skipCount,
//            lastPlayed, dateAdded, durationMs }   // text fields "" and year 0 when the library does not say
```

- `http` calls go over https to declared hosts only. A string body is sent as a form, an object as JSON. Allowed
  headers, on `get` and `post`: `content-type`, `authorization`, `accept`, `origin`. Bodies up to 64 KB (8 MB for `postForm`), replies up to 1 MB,
  4 requests in flight per element. An error resolves with status 0.
- `postForm` fields are texts or files `{ filename, type, base64 }`.
- `gifFromVideo` downloads a short video (MP4, fragmented or not, up to 8 MB) from a declared host and makes it a GIF
  that loops, on the PC: `side` pixels at most (200, up to 480), `fps` frames a second (10, up to 25), its first
  `seconds` (30 at most). The answer is a file for `postForm`, for a service such as Discord that shows pictures but no
  video. Windows decodes H.264, not HEVC; a Windows N edition needs its Media Feature Pack. A failure rejects with the
  reason.
- `http.open` answers `false` within 10 seconds of the element's previous page.
- `fonts` reads the font roles of the applied theme ([format](../themes/format.md#fonts)).
- `library.tracks()` reads Apple Music's own library file, so the counts are the ones Apple Music keeps for every track
  of the user's library. `persistentId` is Apple's id in 16 hexadecimal digits;
  `lastPlayed` and `dateAdded` are Unix times in milliseconds, `0` when unknown or never; `durationMs` is `0` when
  unknown. Apple Music saves its counts a little after a play. The answer is reused until the file changes. An
  unreadable or unknown file rejects the promise with the reason. Format: [knowledge](../../knowledge/apple-music-library-file.md).

### 5.7 Local services [preview]

Each service is off until the user sets its setting to `1` (`ampify config discord.enabled 1`). While off, calls return
`false` and log a line.

```js
// Discord presence, permission ipc.discord, setting discord.enabled
Ampify.discord.setActivity({ clientId, type, statusDisplay, details, state, largeImage, largeText, smallImage,
  smallText, detailsUrl, stateUrl, largeUrl, smallUrl, buttons: [{ label, url }], startMs, endMs })
Ampify.discord.clear()

// Notification-area icon menu, permission ui.tray, setting tray.enabled
Ampify.tray.setMenu([{ label, onClick, checked, enabled }, "-"])   // "-" is a separator, 20 entries at most
Ampify.tray.onClick(fn)

// MIDI controller, permission midi.input, setting midi.enabled (engine process)
Ampify.midi.on("note:60" | "cc:7" | "ch2:note:36" | "note:*" | "*", fn)   // fn({ type, channel, number, value })
Ampify.midi.devices() -> ["name", ...]

// Local remote, setting remote.enabled (engine process), no permission
Ampify.remote.setState(object)         // shown in GET /status under extensions.<id>, 4 KB at most

// Output file, permission fs.output
Ampify.fs.writeText("now-playing.txt", text)   // -> output\<id>\now-playing.txt
```

Discord: `clientId` is a Discord application id. `type` is `"playing"`, `"listening"`, `"watching"` or
`"competing"`; `statusDisplay` is `"name"`, `"state"` or `"details"`. Links are https; two buttons at most. Ampify
sends one update every 4 seconds at most, and the last element that calls `setActivity` wins.

The local remote listens on `127.0.0.1:<remote.port>` (8974 by default) only. Send the token (`ampify config
remote.token`) in the `X-Ampify-Token` header. Requests from web pages are refused.

| Request | Effect |
|---|---|
| `GET /status` | `{ playing, positionMs, durationMs, track, gainDb, extensions }` |
| `POST /play`, `/pause`, `/next`, `/previous` | Player command; `409` if it cannot be done now |
| `POST /volume?db=-6` | A gain on the samples, capped by `audio.maxGainDb` |

```sh
curl -H "X-Ampify-Token: <token>" http://127.0.0.1:8974/status
```

MIDI: `midi.device` picks the device, and `midi.map` links keys to commands without any element
(`note:60=next;note:62=previous;cc:20=play;cc:7=gain`).

### 5.8 App and extension link [preview]

An app and an extension with the same id run side by side and exchange JSON messages (like Spicetify's
`subfiles_extension`):

```js
Ampify.link.post(message)   // true if the other one is running; 64 KB at most
Ampify.link.on(fn)          // fn(message) for each message; returns a function that removes fn
Ampify.link.connected()     // true if the other one is running
```

Messages arrive in order on a later turn. If the other side is not running, `post` returns `false` and the message is
lost, so let the one that starts second say hello. Both share `storage\<id>.json`: let only one of them write it.

### 5.9 React and components [stable]

`Ampify.React` accepts React's style without JSX and produces the node tree of section 5.5. The target needs its own
permission (`ui.panel`, `ui.page`, `ui.popup` or `ui.fullscreen`).

```js
const { h, useState } = Ampify.React;

function Counter() {
  const [n, setN] = useState(0);
  return h("stack", null,
    h("text", null, "Clicks: " + n),
    h("button", { label: "Add one", onClick: () => setN(n + 1) }));
}

Ampify.ReactDOM.render(h(Counter), "panel:counter");
Ampify.ready.then(() => Ampify.log("ready"));
```

```js
Ampify.React.createElement(type, props, ...children)  // also Ampify.React.h
Ampify.React.Fragment
Ampify.React.isValidElement(value)
Ampify.React.cloneElement(element, props, ...children)
Ampify.React.Children.toArray(children)              // also map, forEach, count, only
Ampify.React.createContext(default)                   // -> { Provider, Consumer }
Ampify.React.memo(component)
Ampify.React.useState(initial)                        // also useReducer, useEffect, useLayoutEffect,
Ampify.React.useContext(context)                      //   useRef, useMemo, useCallback
Ampify.hooks.usePlayer()                              // player state, kept up to date (permission player.read)
Ampify.ReactDOM.render(element, target)
Ampify.ReactDOM.createRoot(target)                    // -> { render(element), unmount() }
Ampify.ReactDOM.unmountComponentAtNode(target)
Ampify.ready                                          // resolves once the element has started
```

- Types: a node name, a component function, `Fragment`, or a context's `Provider`/`Consumer`. A few HTML names map
  to nodes (`div` → `stack`, `span`/`p` → `text`, `h1` → title text, `img` → `image`, `select` → `dropdown`, `ul` →
  `list`). No class components.
- Props that are text, numbers or booleans become attributes; `style`, objects and other functions are ignored.
- Handlers get `{ type: "click", id }` or `{ type: "change", id, value, target: { value, checked } }`.
- State changes in one handler give one render. More than 50 renders in a row are stopped. An error in a later render
  is logged and the previous tree stays.
- Targets: `"panel:<name>"`, `"page:/<path>"`, `"popup:<name>"` (or `"popup"`), `"fullscreen"`. A page mounts again at
  each visit, so its state starts over; keep lasting values outside the component.
- An app that defines a global `render()` gets its result shown on the page `/<id>`.

| Component | Props |
|---|---|
| `Button` (`ButtonPrimary`, `ButtonSecondary`, `ButtonTertiary`) | `label` or a text child, `icon`, `disabled`, `variant`, `onClick` |
| `Toggle` | `label`, `value` or `checked`, `disabled`, `onChange(checked)` |
| `Slider` | `label`, `min`, `max`, `step`, `value`, `disabled`, `onChange(number)` |
| `TextField` | `label`, `value`, `placeholder`, `disabled`, `onChange(text)` |
| `Dropdown` | `label`, `value`, `options` (texts or `{ value, label }`), `disabled`, `onChange(value)` |
| `Card` | `title`, `subtitle`, `onClick`, children, box style |
| `List` | children |
| `Text` (`TextComponent`) | `text` or children, `variant`, `size`, `weight`, `color`, `align`, `wrap`, `maxLines` |
| `Tooltip` (`TooltipWrapper`) | `label` or `content`, `placement`, `delay`, children |

Every component accepts `tooltip`, `tooltipPlacement` and `tooltipDelay`. Full example: `examples/react-panel/`.

## 6. Themes, apps and snippets

### 6.1 Themes [stable]

A theme is a folder with `colors.ini`, `user.css`, `resources.xaml` and optionally a `main.js` that runs like an
extension while the theme is active. Format: [themes/format.md](../themes/format.md).

### 6.2 Apps [stable]

An app lives in `apps\<id>`, is switched on with `ampify app enable <id>`, and adds pages with
`Ampify.ui.page.register`. Its manifest `icon` does not create a sidebar entry by itself: the app adds one with
`Ampify.ui.sidebar.addItem` and opens its page with `Ampify.ui.navigate("/<path>")`. A page can lead to another of
its pages (`Ampify.ReactDOM.render(view, { page: "/<path>/item" })`, then `navigate`): the arrow of the page header
goes back to the previous one. Each page mounts anew when it opens, so keep what must outlive it (the tab, the search)
in a module variable. `Ampify.ui.navigate("/ampify/settings")` opens Ampify's settings page.

### 6.3 Snippets [stable]

A snippet is a `user.css`, a `resources.xaml` fragment, or both, switched on by itself ([format](../themes/format.md#snippets)).

## 7. Errors and the log

- Each log line names the element. Errors in `main.js` are logged without stopping other elements.
- A call without its permission throws `PermissionDenied`, naming the missing permission.
- Follow the log with `ampify logs --follow`.

## 8. Not in API 1.0

- Community native code (DLLs).
- Raw XAML in extensions.
- Playlists, and changing the library (`library.write`), and Apple Music's internal services (`services.read`): the
  names are reserved.
- Adding to the queue (`queue.add` answers `false`).

## 9. Versions

`kitApi` is `MAJOR.MINOR`. A minor version only adds; a major version removes or changes a stable function. A removed
function stays one more minor version, with a warning in the log. The kernel accepts any minor version of its major.
