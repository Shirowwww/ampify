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
| `audio.mix` | `Ampify.automix`: the current song mixed into the next one, in the audio process | [preview] |
| `audio.sing` | `Ampify.audio.sing`: the karaoke mode, from either process | [preview] |
| `ui.topbar`, `ui.playbar`, `ui.sidebar`, `ui.menu`, `ui.popup` | Buttons, sidebar entries, menu entries, dialogs | [stable] |
| `ui.panel`, `ui.window` | A side panel, a secondary window | [preview] |
| `ui.fullscreen` | A view over the whole window, and Apple's own full-screen and mini players | [stable] |
| `ui.page` | App pages and navigation | [stable] |
| `ui.keyboard` | Global or window shortcuts | [preview] |
| `clipboard.write`, `clipboard.read` | `Ampify.clipboard.copy` and `paste`: text put on, and read from, the Windows clipboard | [preview] |
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
Ampify.appVersion           // Apple Music's version, e.g. "1.1792.23042.0", "" when unknown   [preview]
Ampify.activeTheme()        // the id of the theme in use, null for none; read it again, the user may change it
```

Texts in several languages [preview]:

```js
Ampify.i18n.language                    // "de": the language Ampify shows, as two letters
Ampify.i18n.define({ en: { hello: "Hello {name}" }, de: { hello: "Hallo {name}" } })
Ampify.i18n.t("hello", { name: "Lea" }) // the text in that language, else English, else the key
Ampify.i18n.t("kit.tray.next")          // Ampify's own texts, prefix kit.
```

`Ampify.i18n.language` is one of `en`, `fr`, `de`, `es`, `pt`, `ru`, `ja`, `zh`, `it`, `ko`, or another code when Windows uses a
language Ampify does not translate. It follows the Windows display language, or the `language` setting.

### 5.2 Settings and storage [stable]

```js
Ampify.config.get(key, default)     // reads extensions.<id>.<key> (apps.<id>... for an app, themes.<id>... for a theme)
Ampify.config.set(key, value)       // writes it; an element only reaches its own keys
Ampify.storage.get(key, default)    // a JSON value in storage\<id>.json (1 MB), permission storage
Ampify.storage.set(key, value)
Ampify.storage.remove(key)
Ampify.storage.clear()              // every value of the element
```

For a setting declared in the manifest, `config.get(key)` without a default gives the manifest's default, typed (a
boolean for a toggle, a number for a number). A change in Ampify's settings page restarts the extension.

### 5.3 Player [stable]

```js
Ampify.player.state()
// -> { playing, positionMs, durationMs,
//      track: { id, title, artist, album, variantId, atmos, lossless, sampleRate, bitDepth, catalogId, contentRating,
//               queueItemId, artwork, artworkColor } | null,
//      source: "apple" | "smtc" | "" }
Ampify.player.on("trackchange" | "statechange" | "lyricschange" | "progress", fn)   // "songchange", "onplaypause" and "onprogress" are Spicetify's names (onplaypause also fires on a position jump)
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
Ampify.player.setRate(rate)         // 0.5 to 2: Apple's player speed, kept across pauses and songs (1.6, player.control)
Ampify.player.rate()                // the speed in effect, 1 until one is set (player.read)
Ampify.player.skipForward(ms)       // seeks 15 s on (or ms), within the song; false without a track
Ampify.player.skipBack(ms)
Ampify.player.progress()            // 0 to 1, how far the song has gone; 0 while its length is unknown
Ampify.player.formatTime(ms)        // "3:07", "1:02:05"
Ampify.player.muted()               // -> true | false | null   [preview] (permission player.read)
Ampify.player.setMute(on)           // and toggleMute(): also player.control
Ampify.player.toggleMute()
Ampify.player.increaseVolume(step)  // a tenth of Apple's slider (or step, above 0 up to 1)
Ampify.player.decreaseVolume(step)
Ampify.player.queue.list()  // -> { available, complete, source, reason,
//                                   items: [{ id, title, artist, album, durationMs, section, catalogId, artwork }] }
Ampify.player.queue.add(catalogId, { position: "next" | "last" })  // [preview] answers false for now
Ampify.player.queue.remove(itemId)                                 // [preview]
Ampify.player.queue.move(itemId, beforeItemId)                     // [preview] the entry goes right before the other (1.6)
```

- `track.id` is computed from title, artist and album (`t-` and 16 characters), the same whatever the source.
  `track.queueItemId` is Apple's queue entry, so the same song played twice has two.
- `track.variantId` is `"lossless"`, `"hi-res-lossless"`, `"dolby-atmos"`, `"spatial"`, `"surround"`, `"other"`, or
  empty. `track.sampleRate` is the stream's rate in Hz (`44100`, `192000`) and `track.bitDepth` its bit depth (`16`,
  `24`), each `0` when the source does not say: Apple gives them a few seconds after the song starts (a `progress`
  event brings them), often without the bit depth, and SMTC never does (Ampify 1.5.9). `track.catalogId` is the
  song's id in Apple's catalog (for `Ampify.music.analysis`), empty for a song that is not in it; `track.contentRating`
  is `"explicit"`, `"clean"` (a censored version) or empty when Apple marks the song neither way (Ampify 1.6). `track.artwork` is an https cover address or `""` (Apple Music 1.1792 keeps it internal, so it is empty
  there); `track.artworkColor` is `"#RRGGBB"` or `""`.
- A queue item's `artwork` goes straight into an `image` node's `src`: an https address, or `"artwork:<id>"` when Apple
  keeps the address internal (Apple's own loader then draws that entry's cover), or `""` for an entry without one.
- `source` is `"apple"` when Ampify reads Apple's own player (in `AppleMusic.exe`), `"smtc"` for Windows' media
  controls, `""` when unknown.
- `statechange` fires when playback starts or stops, the length becomes known, or the position jumps. Between events
  `state().positionMs` moves on: `progress` fires each time it reaches another whole second (while playing, or after
  a seek), with the state, so a time display needs no timer of its own. Apple Music 1.1792 gives no position of its
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
  with `complete: false` and the reason. `queue.remove(id)` removes an entry still to come. `queue.move(id, beforeId)`
  puts an entry still to come right before another entry to come, as Apple's own queue moves them, and waits for
  Apple's queue to confirm, up to 3 s (Ampify 1.6, `player.control`). Moved one after the other, entries keep their
  order: moving each entry of an order before its last one lays the whole order out. `queue.list()` may show the old
  order for a moment after a move: work from ids, not from positions read in between.
- `Ampify.music.analysis(ids)` gives Apple's own audio analysis of catalog songs (`track.catalogId`, a queue entry's
  `catalogId`; one id or a list, the first 250 kept), as Apple's web player gets it: no account, no network permission, the kernel asks
  `amp-api.music.apple.com` with the token music.apple.com gives every visitor, in the storefront of `Ampify.locale`
  (`{ storefront: "fr" }` to choose), then in the US one for the songs missing there. Each song has, as Apple writes
  them: `bpm` (`beginning`, `main`, `ending`, `percentDeviation`), `key` (`{ tonic: "A", mode: "major" }` per part),
  `loudness` (`{ value, peak, range }` per part, dB), `energy`, `valence`, `danceability`, `acousticness`,
  `melodicness` (0 to 1 per part), `beats` (`beatsInMilliseconds`, `barsInMilliseconds`: the whole beat and bar grid),
  `vocalActivity` (`[{ startInMilliseconds, endInMilliseconds, strength }]`, strength `very-low` to `very-high`), and
  `mood`, Ampify's reading of the main valence and energy (`Ampify.music.mood`: happy or calm above the middle valence,
  tense or sad below, the first of each pair the livelier). A song Apple has not analysed is missing from the answer;
  the promise rejects with the reason when Apple cannot be reached. Answers are kept for the process.
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
- Mute and the volume steps work on the position of Apple's slider, as the user sees it: muted is the slider at its
  bottom, a step of `0.1` moves it a tenth of its length. `setMute(false)` puts back the level the element muted from,
  or half the slider when it does not know it. They need the volume known (`audio.volume()` not `null`), and answer
  `null` or `false` until then. From the interface process, the volume they read comes from the audio process, about
  100 ms behind.
- `skipForward` and `skipBack` seek from `state().positionMs`, never before the start or past the end.
- `setRate(rate)` asks Apple's player to play at `rate` times its speed, from either process: the audio process
  applies it to the player that plays at once, or at the next play, and keeps it across pauses, seeks and songs until
  `setRate(1)`. It answers `true` once asked; `rate()` says the speed in effect a moment later. Apple's own clock and
  lyrics follow the speed, and so does `state().positionMs` between Apple's readings, with the lyrics Ampify fills.

### 5.4 Audio [preview]

Sound is processed in `AMPLibraryAgent.exe` (`"processes": ["engine"]`), on Ampify's audio hooks (`src/win/audio/`),
which go in by themselves when the interface asks for the sound. The interface (`AppleMusic.exe`) reads what that
process shares: `onFrame`, `analyze()`, `loudness()` and `volume()` work there too, about 100 ms behind. The gain and
the equalizer stay in the engine: in the interface `setGainDb` answers 0, the equalizer `false` and `eq.bands()` is
empty, without throwing.

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

#### Sing [preview]

Permission `audio.sing`. Sing turns the singer's voice down, as Apple Music Sing does on the iPhone. The interface asks
and the audio process does the work, so `set` and `state` work from both processes, through the block they share.

```js
Ampify.audio.sing.set({ mode, voice, music, model })  // mode "off" | "light" | "studio", voice and music 0 to 1,
                                                      // model "standard" | "precise"; true once the request is written
Ampify.audio.sing.state()                // -> { available, mode, voice, music, model, phase, progress, delay, reason, installed, downloadSize }
Ampify.audio.sing.onChange(fn)           // fn(state) when the state moves (polled every 250 ms); returns the function that stops
```

- `voice` is how much of the singer's voice stays, `music` how much of everything else (1 when left out). Karaoke turns
  the voice down; `voice: 1, music: 0` keeps the singer alone. Ampify before 1.5.6 has no `music` in its state and
  ignores it.
- `Light` is the lighter quality; `Studio` is the studio quality, run on the graphics card, which holds the sound back
  for `delay` seconds so that the lyrics stay in step. A pause keeps what Studio holds, so playing again goes on at
  once; a move in the song, another song or Studio turned on starts it again silent for `delay`. AutoMix mixes with
  either quality on: in Studio, both songs of a mix are held back alike.
- Studio runs one of two voice models. `standard` (the default) is UVR's MDX-Net Voc_FT: light, and its `delay` is
  short. `precise` is Kim's Mel-Band RoFormer: it leaves less of the voice, but its download is far larger (465 MB),
  its `delay` is 1.5 s longer and it wants a graphics card from the last few years (GeForce RTX 2060 or RTX 3050, Radeon RX 6600 or better;
  about 1.6 GB of its memory). Changing the model while Studio is on stops Studio a moment, frees the
  graphics card of the old model, then starts again with the new one.
- `mode`, `voice`, `music` and `model` are what was asked last (`voice`, `music` and `model` stay when `set` is called
  without them). The rest is what the
  audio process says: `phase` is `off`, `downloading`, `loading`, `buffering`, `on` or `failed`; `progress` is 0 to 1
  while downloading or loading; `available` is whether Studio can run on this PC (`null` until known).
- `installed` says whether the files Studio needs with the model asked are downloaded and checked (`null` where it
  cannot be told), and `downloadSize` how many bytes are still to download for it (0 once installed), fetched the
  first time Studio is asked for with that model.
- `reason` says why Sing is not fully on: `none`, `no-graphics-card`, `download-failed`, `model-failed`,
  `dolby-atmos` or `too-slow`. `automix-mixing` is no longer given: Ampify 1.5.0 alone gave it, while AutoMix mixed.
  `too-slow`: the graphics card took longer than the model allows for most of its last windows, so the
  voice would come back for a moment every hop; Light plays instead, until Studio is asked for again or the other
  model is picked.
- Sing works on Apple's stereo stream. While any mode is on, Apple's player is asked for the songs' stereo versions:
  at once while a stereo song plays or none does, otherwise from the next time Apple Music starts playing (its first
  song after it opens). A song already playing in Dolby Atmos is never switched under way: Apple Music then plays it
  silent with its position running several times too fast. Until then `reason` is `dolby-atmos`. Turned off, Sing
  gives Apple's own choice back at once, and a song that can goes back to Dolby Atmos under way.
- A mode other than the three, a voice outside 0 to 1, or a model other than the two, throws `RangeError`. Where nothing shares the request (a host
  without audio) `set` answers `false` and `state()` says `off` and `null`, without throwing.
- Example: `extensions/sing/`.

#### AutoMix [preview]

`Ampify.automix` (permission `audio.mix`, audio process only) plays the end of the current song into the next one.
Ampify starts a second player on the current song a few seconds ahead, delays its sound until it is Apple's to the
sample, lets it take the song over, moves Apple's player to the next song at the plan's start, then shapes both
songs along the plan: volumes, bass, and the current song's speed. The `automix` extension plans each mix from
Apple's analysis of the two songs (beats, bars, phrases, key, fades, loudness).

```js
Ampify.automix.state()            // -> { available, playing, current, next, mix, features }
Ampify.automix.schedule(plan)     // -> { ok: true } or { ok: false, reason }
Ampify.automix.cancel(reason)
Ampify.automix.preferStereo(want) // -> true once the wish reached Apple's player, false in another process
```

- `current` and `next`: `{ name, id, storefront, time, duration, atmos }` or `null`. `name` is the song's stream
  address without its query, the name a plan gives; `id` is its catalog id and `storefront` its store's number (143442
  for France), `null` when Apple's player does not say; `time` and `duration` in seconds, 0 while unknown. `atmos`
  says whether the song plays in Dolby Atmos, `null` until Apple's player has loaded it: always `null` for `next`,
  except `false` while Apple's player takes stereo versions (asked by Sing or by `preferStereo`).
- `mix`: `{ phase, next, reason, stepErrorDb, seeks }`. `phase` is `idle`, `waiting`, `preparing`, `aligning`,
  `ready`, `taking over`, `mixing`, `recovering`, `done` or `failed` (then `reason` says why); `stepErrorDb` is what
  is left when the second player's copy is taken from Apple's sound, in dB below it, `null` until measured.
- A plan is `{ next, bStart, keys, rate, rateFrom, echo, loop, gate }`: `next` is the next song's `name`, `bStart` where
  that song starts (seconds into it), `keys` 2 to 64 points in time order, at most 60 s apart and before the end of
  both songs. The current song must go on for 3 s after the last key (3.3 s when it is stretched, and more when it is
  slowed: what the mix lasts times `1 - rate`), because the second player runs ahead of what is heard.
  A key is `[time in the current song, its volume, the next song's volume]`, from `[t, 1, 0]` to `[t2, 0, 1]`, or
  the same with two more numbers, the bass of each song (1 kept, 0 taken away below 150 Hz), the last key leaving the
  next song its bass, or with two more again, how far each song is filtered (0 not at all): the current song through
  a high-pass rising from 20 Hz at 0 to 2 kHz at 1, the next one through a low-pass coming down from 20 kHz at 0 to
  400 Hz at 1, the last key leaving the next song unfiltered, or with two more again, the crossover (0 not at all):
  the current song through a steep low-pass coming down from 20 kHz at 0 to 30 Hz at 1, the next one through a steep
  high-pass rising from 2 Hz at 0 to 16 kHz at 1. At one cutoff the two add up to the power of either song, so both
  can play at full volume while one hands its sound to the other; the last key leaves the next song open. Apple's
  player moves to the next song at the first key;
  between two keys the squared volumes follow a smoothstep, so a crossfade keeps its power, and the bass and the
  filters follow the same curve; the crossover's two numbers move in a straight line, so a sweep of many keys never
  stops on one.
- Two keys at the same time, `[[t, 1, 0], [t, 0, 1]]`, are a cut: Apple's player alone goes from the current song at
  `t` to the next one at `bStart`, with no overlap, no second player and no `rate`, so `t` may be right at the end of
  the song (a song heard to its last note, the next one from its first sound).
- `echo` (optional): `{ from, to, delay, feedback, level }`. From `from` to `to` (times in the current song, within
  the mix) the current song also goes into an echo, taken before its volume, so its repeats ring on once it is gone:
  `delay` seconds apart (0.05 to 2), each `feedback` (0 to 0.9) of the one before, a little darker and thinner each
  time. The first comes back at `level` (optional, over 0 and up to 2, 0.7 when left out; older versions of Ampify
  take every echo at 0.7) of what was thrown. They are faded out over their last second, once 40 dB down or 8 s after
  the last key at most.
- `loop` (optional): `{ from, length }`. From `from` (within the mix) on, the current song's `length` seconds there
  (0.2 to 8, a bar) play over and over, their seam joined so that they follow each other without a click, until the
  mix is over. The song's time runs on through the loop, as if it went on: later keys are in that time and may lie
  past the end of the song. The loop must end a second before the song does, and the mix start with the room above
  before the end of the song (it is the first key that needs it, not the last). `rolls` (optional, up to 8) rolls the
  loop shorter, as a DJ rolls a bar into a drop: `[[at, length], ...]`, each from `at` (in the loop's running time)
  playing the loop's first `length` seconds (0.03 or more) over and over. Each roll starts on a turn of the one before
  (of the loop, for the first) and is shorter than it.
- `gate` (optional): `{ beats, on, duck, until }`. The next song comes in as stabs: from each of `beats` (up to 128
  times in the current song, in order, within the mix: the next song's beats laid on it) it is heard for `on` seconds
  (0.02 to 2), its volume as the keys say, and not between them, until `until`, from where it plays on as the keys say.
  Under each stab the current song dips by `duck` (0 to 1 of its volume). Each stab opens 3 ms before its beat, so that
  the beat's attack is whole, and closes over 8 ms, before the next one opens.
- `state().features` lists what the kernel takes beyond volumes, bass and speed: `cut`, `filter`, `echo`, `loop`
  (added after Ampify 1.5.4), `crossover` (the keys of nine numbers, after Ampify 1.5.7), `gate` and `roll` (after
  Ampify 1.5.9). An older one has no `features`, refuses the longer keys and the cuts, and ignores `echo`, `loop`,
  `rolls` and `gate`: test the list first.
- `rate` (0.9 to 1.1, optional) is the speed the current song has during the mix, at its own pitch; it gets there
  evenly from `rateFrom` (seconds into the song, at most 30 s before the first key). Key times stay those of the song
  itself: a mix of 16 s at 1.05 lasts 15.2 s.
- The second player starts 8 s before the song is taken over (the first key, or `rateFrom`), a few seconds more for
  a stretched song: hand a plan over at least 14 s ahead. A plan whose mix has not begun is replaced by the next one.
  Pausing, seeking, skipping or changing the queue cancels the mix: Apple's player plays on alone, without a jump.
  Stopping the element cancels its plan.
- Dolby Atmos goes out as spatial objects, which a mix cannot take: a plan is refused while the current song plays
  in it, and a next song that turns out to play in it comes in late and at full volume. Read `audioTraits` in the
  catalog and leave such songs alone, as the `automix` extension does.
- `preferStereo(true)` asks Apple's player for the songs' stereo versions, as Sing does, so that songs with a Dolby
  Atmos version can be mixed: at once while a stereo song plays or none does, otherwise from the next time Apple
  Music starts playing (a song already in Dolby Atmos is never switched under way). `preferStereo(false)` gives
  Apple's own choice back, unless Sing still asks. Stopping the element gives it back too. The `automix` extension
  asks while its "Mix Dolby Atmos songs in stereo" setting is on (it is off by default). Added in Ampify 1.5.2: test
  `typeof Ampify.automix.preferStereo === "function"` first.

### 5.5 Interface [stable]

An interface is a tree of nodes `{ type, ...attributes, children }`, written directly or with `Ampify.React`
(section 5.9). Ampify draws it with WinUI controls and, on each render, only changes the nodes that changed (matched by
position or `key`). Each node gets the AutomationId `AMPIFY_<element>_<node>`. Limits: 8 levels, 300 nodes, 500
characters per text; attributes are plain values, except `onClick` and `onChange`, which take a function in a tree
written by hand as in a React one.

| Node | Attributes |
|---|---|
| `stack`, `row`, `list` | `gap`, `children` |
| `grid` | `columns` (whole number, 1 to 12, 2 by default), `gap` (0 to 1000, as in the other containers), `children`: equal columns, the children filled row by row |
| `tabs` | `gap`, `children`: `tab` nodes side by side |
| `tab` | `label`, `selected` (the tab that is on, drawn in the accent colour), `disabled`, `onClick`; only in `tabs` |
| `menu` | `label`, `disabled`, `children`: `item` nodes, opened as a flyout from the menu's button; only `item` nodes |
| `item` | `label`, `disabled`, `onClick`; only in `menu`; no icon in this version |
| `layer` | `children`, stacked on top of each other |
| `card` | `title`, `subtitle`, `onClick`, `children` |
| `text` | `text`, `variant` (`title`, `subtitle`, `body`, `caption`) and the text style below |
| `button` | `label`, `icon` (SVG), `disabled`, `variant` (`primary`, `secondary`, `tertiary`), `onClick` |
| `toggle` | `label`, `checked`, `disabled`, `onChange` |
| `checkbox` | `label`, `checked`, `disabled`, `onChange` (`true` or `false`): Apple's check box |
| `slider` | `label`, `min`, `max`, `step`, `value`, `unit` (text after the value shown in the header, `" %"`), `disabled`, `onChange` |
| `progress` | `value`, `max` (100 by default), `label`: a bar filled to `value`; without `value` it moves on its own |
| `spinner` | `size` (16 to 128, 32 by default), `label`: the spinning ring, for a wait of unknown length |
| `divider` | A thin line across its container, in Apple's divider colour |
| `textfield` | `label`, `value`, `placeholder`, `disabled`, `onChange` |
| `dropdown`, `option` | `label`, `value`, `disabled`, `onChange`; children are `option` nodes with `value`, `label` |
| `image` | `src` (https, a file path, `"artwork"`: Apple's image of the song playing, following it, the previous cover fading out in 500 ms on a new song except in eco glass or with Windows' animation effects off, or `"artwork:<id>"`: the cover of a queue entry, as `queue.list()` gives it), `label`, `onClick`, `fit` (`cover`, `contain`, `fill`), `cornerRadius`, `width`, `height` |
| `icon` | `icon` (SVG), `label`, `onClick` |
| `lyrics` | `size`, `color`, `placeholder`, `onChange`: the lyrics of the song playing, lit as Apple's own view does (the whole line, or word by word when Apple times the words), the current line larger and kept in view; `placeholder` shows while the song has none; a click on a line gives its start in ms to `onChange`; the wheel scrolls the lines once the pointer has rested on them for a second, and scrolls the page or panel around them before that |
| any | `tooltip`, `tooltipPlacement` (`top`, `bottom`, `left`, `right`), `tooltipDelay` (0 to 10000 ms) |

A `card`, `image` or `icon` with an `onClick` works from the keyboard as well: Tab stops on it, Enter or Space clicks
it, and a card lights up while it has the focus as it does under the pointer.

A `tab` is a button that never takes a value: its `onClick` runs, and the app moves `selected` to the new tab when it
draws again (`Ampify.components.Tabs` does this with `value` and `onChange`). A `menu` takes no `onClick` of its own;
each `item` has one, and the flyout closes when an item is clicked. A `grid` with a child that has `grow` does not
stretch it: every child gets one cell of the same width.

| Where | Attribute | Values |
|---|---|---|
| any | `opacity`, `scale` | 0 to 1; 0 to 10 around the center |
| any | `transition` | `"opacity 400ms"`, `"scale 0.3s"`, `"translation 200ms"`, `"all 300ms"` (comma-separated, 10 s max) or `"none"` |
| any | `align`, `valign` | `start`, `center`, `end`, `stretch` (on a `text`, `align` is `left`, `center`, `right`) |
| any | `grow` | `true`: takes the space left in its `row` or `stack` |
| any | `inert` | `true`: the node and everything under it take no click, no hover and no keyboard focus, and the pointer reaches what lies beneath. It is still drawn: pair it with `opacity: 0` for controls that fade out, which would take clicks otherwise |
| `stack`, `row`, `layer`, `card` | `background` | `#RRGGBB`, `#AARRGGBB`, `acrylic`, `acrylic(#AARRGGBB, 0.6)`, or `linear-gradient(#top, #bottom)`. A panel's root gives its background to the panel's whole card |
| `stack`, `row`, `layer`, `card` | `padding` | A number, or `"top,right,bottom,left"` |
| containers, `image` | `cornerRadius`, `width`, `height`, `minWidth`, `maxWidth` | Pixels |
| `text` | `size`, `weight`, `color`, `wrap`, `maxLines` | 8 to 120 px; 100 to 900; `#RRGGBB`; `false` for one line; 0 to 100 |

```js
Ampify.ui.topbar.addButton({ label, icon, onClick, active })   // no onClick: a text that only shows something
Ampify.ui.playbar.addButton({ label, icon, onClick, active })  // same; any other onClick than a function throws
Ampify.ui.sidebar.addItem({ label, icon, onClick, active })   // icon: a Segoe Fluent Icons glyph; returns the id
Ampify.ui.menu.addItem("context" | "app", { label, icon, onClick, disabled })
Ampify.ui.menu.addItem("context" | "app", { label, icon, items: [{ label, icon, onClick }, ...] })  // a submenu
Ampify.ui.menu.addItem("context", { label, onClick, kinds: ["song", "album"] })  // only in those items' menus
Ampify.ui.topbar.remove(id)            // also playbar.remove, sidebar.remove, menu.remove: false when not its own
Ampify.ui.popup.open(view)
Ampify.ui.popup.show(name, view)       // a named popup: shown again under the same name, it changes in place
Ampify.ui.popup.close(name)
Ampify.ui.fullscreen.open(view)        // permission ui.fullscreen; called again, updates in place
Ampify.ui.fullscreen.close()
Ampify.ui.fullscreen.onClose(fn)       // returns a function that removes fn
Ampify.ui.applePlayer.open()           // permission ui.fullscreen: Apple Music's own full-screen player
Ampify.ui.miniPlayer.open()            // permission ui.fullscreen: Apple Music's own mini player (1.6)
Ampify.ui.miniPlayer.close()           // as Alt+F4 on it does: Apple Music's main window comes back
Ampify.ui.miniPlayer.toggle()          // closes it when it is on screen, opens it otherwise
Ampify.ui.miniPlayer.set({ opacity, favorite, motion })  // what the element asks of its window; null: nothing
Ampify.ui.panel.register(id, view)     // a 380 px column on the right; it slides in from the right
Ampify.ui.panel.remove(id)             // slides it back out; register brings it back
Ampify.ui.panel.onClose(id, fn)        // the user closed it with its Close button: it is removed (register brings it
                                       // back, a React root unmounts) and fn runs; returns a function that removes fn
Ampify.ui.title.set(text)              // title of the element's panel, page and popup
Ampify.ui.title.get()                  // the title shown now
Ampify.ui.title.reset()                // back to the element's name
Ampify.ui.setLabel(id, label)          // new text of a top bar or play bar button, by the id addButton returned
Ampify.ui.setActive(id, on)            // on or off: a button or sidebar entry that is on takes Apple Music's accent colour
Ampify.ui.setDisabled(id, disabled)    // a top bar or play bar button, or a menu entry, greyed: it ignores clicks
Ampify.ui.playbar.mark(id, from, to)   // a stretch of the progress line for a play bar button it added (1.3)
Ampify.ui.playbar.readout(totalMs)     // the time under the pointer on the progress line, for a span that long (1.3)
Ampify.ui.playbar.onHover(fn)          // fn(part): where the pointer is on the line, 0 to 1, null when it leaves (1.3)
Ampify.ui.playbar.audioBadge({ label, icon })  // what Apple's Lossless badge says; null gives Apple's back (1.5.9)
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
- `kinds` keeps a context entry to the menus of those kinds of item (the `kind` above); a menu Apple Music's data does
  not describe then leaves it out, and a submenu whose entries are all left out goes too. It takes the place of
  Spicetify's `shouldAdd`: Apple's menu opens on the interface thread before an element could answer, so the choice is
  given beforehand. `disabled: true` (or `setDisabled`) shows an entry or a button greyed. `remove` takes out an entry
  or a button with its click handler, and a submenu with its entries.
- `playbar.addButton({ label, beside: "volume" })` puts the button, or the text, right after Apple Music's volume
  button instead of at the end of the bar, as Volume percentage does. `"volume"` is the only name for now; another
  throws. Ampify before 1.3.2 leaves `beside` out and puts the button at the end.
- `playbar.addButton({ label, icon, onClick, player: true })` also shows the button in Apple Music's full-screen
  player, just before its lyrics button, drawn like that button and checked while it is on (`active`, `setActive`),
  and since 1.6 in its mini player too, where the row has room for the first two such buttons.
  A click there runs the same `onClick`, and a popup opened then shows over the player. A text without `onClick`
  stays in the bar only. Sing does this. Ampify before
  1.5.6 leaves `player` out.
- `playbar.mark(id, from, to)` draws two ticks on the progress line of the playback bar and shades the stretch between
  them, as the A-B loop does with its passage. `from` and `to` are parts of the song, from 0 to 1 (a position divided by
  `state().durationMs`), in either order; without `to` only the first tick shows, and without `from` the line is clear
  again. The marks take the text colour of the bar, in its light or dark mode, fade in and out, and go with the
  button. Ampify before 1.3 has no `mark`: check that it is a function before calling it.
- `playbar.readout(totalMs)` shows, while the pointer is over the progress line, a thin marker where it is and, above
  it, the time at that point of a span `totalMs` long (`m:ss`, or `h:mm:ss` from an hour on). Ampify draws both at the
  pointer's own pace, so the element only says how long the line is: `state().durationMs` for the song playing, as the
  Time on hover extension does. `readout()` or `readout(0)` takes it away. The label takes the colours of Apple Music's
  tooltips, the marker the text colour of the bar; both follow the light and dark modes and every theme's line, and
  the label goes under the line when the bar sits at the top of the window. When several elements ask, the last one
  shows.
- `playbar.audioBadge({ label, icon })` makes Apple's audio badge, under the song in the play bar and in the mini
  player (since 1.6), say `label`: its
  words where the layout shows them, its screen reader name and the title of the flyout it opens. `icon` (SVG, drawn
  in the badge's own colour, as tall as Apple's mark and as wide as its `viewBox` asks) follows Apple's Lossless mark
  there and in the flyout, Apple's mark unchanged; leave room on its left for the gap. It acts only while Apple shows its Lossless
  mark: a Dolby badge keeps Apple's words, and they come back by themselves when the next song is not lossless.
  `audioBadge(null)` gives Apple's badge back as it was. When several elements ask, the last one shows. Needs
  `ui.playbar`; Ampify before 1.5.9 has no `audioBadge`: check that it is a function. The Hi-Res Lossless badge
  extension uses it.
- `playbar.onHover(fn)` calls `fn(part)` as the pointer moves over the progress line, at most every 50 ms, with `part`
  from 0 to 1 (a position divided by the line's length), and `fn(null)` once it leaves. It returns the function that
  stops listening. Both need `ui.playbar`; Ampify before 1.3 has neither.
- `popup.close(name)` closes a popup opened with `popup.show(name, ...)` or `ReactDOM.render(..., "popup:<name>")`, as
  the user's Close would. A popup of `popup.open` has no name and only closes by the user's hand. Names are lowercase
  letters, digits and dashes.
- A button with `active: true`, or switched by `setActive`, is drawn in the accent colour and reads "on" to screen
  readers: a toggle such as a sleep timer or a lyrics mode shows its state at a glance. A sidebar entry that is on also
  gets the bar Apple Music puts before its selected item, and it is on by itself while its element's page is open.
- The full-screen view covers the window, above everything. Escape or its Close button closes it. The view's tree
  starts 64 px from the top, under the Close button, and fills the rest: give its root `valign: "stretch"` to lay
  columns as tall as the window (a `lyrics` node that grows then scrolls within them). One at a time: the
  last element that opens it wins, and `onClose` runs when the user closes it or another element takes it.
- `applePlayer.open()` opens Apple's full-screen player where the user chose in Ampify's settings page (`apple.fullscreen`).
  If Apple Music refuses, nothing opens and the log says why.
- `miniPlayer.open()` opens Apple's mini player as the artwork of the playback bar does, or brings it to the front
  when it is already open; `close()` closes its window as Alt+F4 does, so Apple Music's window comes back as after a
  click on its cross. Ampify's tray icon has the same toggle.
- `miniPlayer.set({ opacity, favorite, motion })` asks for the mini player's window: `opacity` from 0.4 to 1 (lower is
  raised to 0.4, so the window stays findable), `favorite: true` for a star that marks the song playing as a
  favourite, and `motion` the address of the album's animated cover, an MP4 file on `https://mvod.itunes.apple.com/`
  (the H.264 file a variant of `editorialVideo.motionDetailSquare.video` names in Apple's catalog; any other address
  throws). Ampify downloads it once, keeps the last six, and plays it muted and looping over the artwork, which Apple
  grows into the album view on a click; nothing plays while Windows' animation effects are off. The last element to
  ask wins; `set(null)`, or stopping the element, gives Apple's window back.
- An app page takes the place of Apple Music's page, with a Back button; Apple Music's own navigation closes it.
- A `lyrics` node draws itself from Apple's player at the screen's pace: the element renders it once, with no timer of
  its own. Give it room, as a growing child of a `stack` of fixed `height`. An `image` with `src: "artwork"` shows
  the cover loaded large by Apple's own loader, not the play bar's small one.
- `ReactDOM` unmounting a `panel:` target removes the panel, as `panel.remove` does.
- A panel takes the corners of the theme's panels (its `--panel-radius`), square ones included, and nothing drawn
  inside it gets a larger radius. A theme can also give the panels a column of their own
  ([format](../themes/format.md#a-column-for-the-panels)). The element does nothing for either.
- Extensions cannot inject raw XAML. Themes are the only elements that ship XAML.

### 5.6 Services [preview]

```js
Ampify.notify({ title, text })                      // permission notifications
Ampify.keyboard.bind("Ctrl+Alt+L", fn)              // permission ui.keyboard
Ampify.keyboard.unbind("Ctrl+Alt+L")                // gives it back; false when this element had not bound it
Ampify.clipboard.copy(text) -> boolean              // permission clipboard.write, 1 MB at most
Ampify.clipboard.paste() -> string | null           // permission clipboard.read: null when it holds no text
Ampify.http.get(url, { headers }) -> Promise<{ status, text }>   // [stable] permission network:<host>
Ampify.http.post(url, body, { headers }) -> Promise<{ status, text }>
Ampify.http.put(url, body, { headers }) / Ampify.http.patch(url, body, { headers }) / Ampify.http.del(url, { headers })
Ampify.http.postForm(url, fields) -> Promise<{ status, text }>   // multipart/form-data
Ampify.http.gifFromVideo(url, { side, fps, seconds }) -> Promise<{ type, base64 }>   // an MP4 made a looping GIF
Ampify.http.open(url) -> boolean                    // opens the page in the browser, an Apple Music link in Apple Music
Ampify.crypto.md5(text) -> hexadecimal string
Ampify.uri.parse(text) / Ampify.uri.build(...)      // [stable] https, music:, itms: and itmss: links of music.apple.com
Ampify.uri.isSong(link) / isAlbum / isPlaylist / isArtist / isStation   // a link, or what parse gave
Ampify.icons.get(name) -> SVG string
Ampify.icons.names() -> string[]
Ampify.color.extract(pixels, width, height)         // RGBA pixels -> PROMINENT, VIBRANT, DARK_VIBRANT, LIGHT_VIBRANT,
                                                    //   DESATURATED, VIBRANT_NON_ALARMING, palette
Ampify.color.fromArtwork(url) -> Promise<colors>    // an https://*.mzstatic.com artwork, same keys
Ampify.color.playing() -> Promise<colors | null>    // the cover playing now, library songs too (player.read)
Ampify.music.analysis(catalogIds) -> Promise<{ [id]: analysis }>   // Apple's audio analysis of songs (player.read, 1.6)
Ampify.music.mood(analysis, part?) -> "happy" | "calm" | "sad" | "tense" | null
Ampify.fonts.roles() -> { [role]: { family, weight, size, fromTheme } }
Ampify.fonts.style(name) -> { role, family, weight, size, fromTheme } | null
Ampify.lyrics.current() -> Promise<lyrics | null>   // player.lyrics() as a promise, permission player.read
Ampify.library.tracks() -> Promise<track[]>         // permission library.read
//   track: { persistentId, title, artist, album, albumArtist, genre, composer, year, trackNumber, discNumber,
//            albumId, playCount, skipCount, lastPlayed, dateAdded, durationMs }
//            // text fields and albumId "" (the album's persistent id), numbers 0 when the library does not say
```

- `http` calls go over https to declared hosts only. A string body is sent as a form, an object as JSON. Allowed
  headers, on `get`, `post`, `put`, `patch` and `del`: `content-type`, `authorization`, `accept`, `origin`. Bodies up
  to 64 KB (8 MB for `postForm`), replies up to 8 MB (cut there), 4 requests in flight per element. An error resolves
  with status 0.
- `postForm` fields are texts or files `{ filename, type, base64 }`.
- `gifFromVideo` downloads a short video (MP4, fragmented or not, up to 8 MB) from a declared host and makes it a GIF
  that loops, on the PC: `side` pixels at most (200, up to 480), `fps` frames a second (10, up to 25), its first
  `seconds` (30 at most). The answer is a file for `postForm`, for a service such as Discord that shows pictures but no
  video. Windows decodes H.264, not HEVC; a Windows N edition needs its Media Feature Pack. A failure rejects with the
  reason.
- `http.open` answers `false` within 10 seconds of the element's previous page.
- `uri.isSong` is true for a song link and for an album link that names one of its songs (`?i=`); `isAlbum` for an
  album link without one.
- `keyboard.unbind` frees the shortcut for a later `bind`, or for another program.
- The clipboard holds what the user copied last, from any program: `clipboard.read` is a permission of its own, shown
  apart in the Marketplace, so an element that only copies never reads it.
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
Ampify.hooks.useArtworkColor()                        // colors of the cover playing (color.playing), or null
Ampify.ReactDOM.render(element, target)
Ampify.ReactDOM.createRoot(target)                    // -> { render(element), unmount() }
Ampify.ReactDOM.unmountComponentAtNode(target)
Ampify.ready                                          // resolves once the element has started
```

- Types: a node name, a component function, `Fragment`, or a context's `Provider`/`Consumer`. A few HTML names map
  to nodes (`div` → `stack`, `span`/`p` → `text`, `h1` → title text, `img` → `image`, `select` → `dropdown`, `ul` →
  `list`, `hr` → `divider`). No class components.
- Props that are text, numbers or booleans become attributes; `style`, objects and other functions are ignored.
- Handlers get `{ type: "click", id }` or `{ type: "change", id, value, target: { value, checked } }`.
- State changes in one handler give one render. More than 50 renders in a row are stopped. An error in a later render
  is logged and the previous tree stays.
- Targets: `"panel:<name>"`, `"page:/<path>"`, `"popup:<name>"` (or `"popup"`), `"fullscreen"`. A page mounts again at
  each visit, so its state starts over; keep lasting values outside the component. A named popup the user closes
  unmounts; rendering it again under the same name opens it anew.
- An app that defines a global `render()` gets its result shown on the page `/<id>`.

| Component | Props |
|---|---|
| `Button` (`ButtonPrimary`, `ButtonSecondary`, `ButtonTertiary`) | `label` or a text child, `icon`, `disabled`, `variant`, `onClick` |
| `Toggle` | `label`, `value` or `checked`, `disabled`, `onChange(checked)` |
| `Checkbox` | The props of `Toggle`, drawn as Apple's check box |
| `Slider` | `label`, `min`, `max`, `step`, `value`, `unit`, `disabled`, `onChange(number)` |
| `TextField` | `label`, `value`, `placeholder`, `disabled`, `onChange(text)` |
| `Dropdown` | `label`, `value`, `options` (texts or `{ value, label }`), `disabled`, `onChange(value)` |
| `ProgressBar` | `value`, `max` (100 by default), `label`: filled to `value`; without `value` it moves on its own |
| `Spinner` | `size` (16 to 128, 32 by default), `label`: Apple's turning ring |
| `Divider` | No props of its own: a thin line in Apple's divider colour |
| `Grid` | `columns` (1 to 12), `gap` (0 to 1000), children |
| `Tabs` | `value`, `options` (texts or `{ value, label }`), `onChange(value)`: a tab per option, the one of `value` on |
| `Menu` | `label`, `disabled`, `items` (`{ label, disabled, onClick }` each): a flyout under a button |
| `Card` | `title`, `subtitle`, `onClick`, children, box style |
| `List` | children |
| `Text` (`TextComponent`) | `text` or children, `variant`, `size`, `weight`, `color`, `align`, `wrap`, `maxLines` |
| `Tooltip` (`TooltipWrapper`) | `label` or `content`, `placement`, `delay`, children |
| `ConfirmDialog` | `isOpen`, `titleText`, `descriptionText`, `confirmText`, `cancelText` (Ampify's "Confirm" and "Cancel" in the display language by default), `onConfirm`, `onClose`: render it into a popup; it closes when the element renders it with `isOpen: false` or unmounts it |

Each component draws the node of section 5.5 that has its name, so a tree written by hand and a React tree look the
same. Every component but `Divider` accepts `tooltip`, `tooltipPlacement` and `tooltipDelay`. Full example:
`examples/react-panel/`.

`useArtworkColor()` reads the cover again at each new track. Right after the change, Windows may still give the
previous cover for a second or two (`player.artwork`).

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
