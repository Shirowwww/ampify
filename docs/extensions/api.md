# Extension API

This document describes what an extension, theme, app or snippet can do, and how the kernel loads them. The format of
`manifest.json` is in [manifest.md](manifest.md). The community catalog is in [catalog.md](catalog.md). The comparison
with Spicetify is in [../spicetify-parity.md](../spicetify-parity.md).

Each item is tagged with its status:

- **[stable]**: relied upon. It will not change without a new major API version.
- **[preview]**: working, but it depends on a measurement or a feature still in progress. It can change before it
  becomes stable, and becomes stable once validated on a real Apple Music.

## 1. Principles

- **Community extensions are JavaScript in a sandbox.** They have no file system access, no native code calls, and no
  network access beyond the hosts they declared.
- **An extension only touches what the API exposes.** It cannot ask for a hook on a component that is not listed.
- **The API is versioned** (`kitApi`, see section 9).
- **Rules that do not change:**
  - no DRM circumvention: no hook on `CoreFP`, `CoreADI64` or the FairPlay key path;
  - sound is only modified after decoding;
  - the player volume is never modified, because Apple Music's slider would move;
  - the audio output is never recorded to disk.

## 2. Architecture

```
AppleMusic.exe or AMPLibraryAgent.exe
  └─ kernel (C++, folder core/): settings, log, safe mode, versions, element host
       ├─ built-in native extensions   e.g. atmos-leveler, compiled with the kernel
       └─ JavaScript runtime            one instance per element, global object Ampify
            └─ community extensions: main.js + manifest.json
```

- **The JavaScript runtime** is [quickjs-ng](https://github.com/quickjs-ng/quickjs), the maintained branch of QuickJS
  (MIT license, compiled as C with MSVC and MinGW for Windows, and on Linux for the tests). The vendored version and its
  hash are in `vendor/quickjs/UPSTREAM.txt`. QuickJS 2021-03-27 did not compile with MSVC.
- **Native extensions** (DLLs loaded at run time) are not open to the community. A DLL runs inside the Apple Music
  process with every right, and a public catalog cannot guarantee it. Native extensions stay built into the kernel, like
  `atmos-leveler`.
- **Why JavaScript.** It is Spicetify's model, so authors already know the shape. A script changes without
  recompiling. The sandbox limits the damage of a faulty or malicious script to the permissions it asked for. And the
  runtime can be tested on Linux.
- **One `main.js` can run in both processes.** `Ampify.process` is `"ui"` in `AppleMusic.exe` and `"engine"` in
  `AMPLibraryAgent.exe`. The `audio.*` permissions only exist in the engine process.
- **The manifest chooses the processes** with `processes` (see [manifest.md](manifest.md)). By default an element runs
  only in the interface (`ui`, AppleMusic.exe). An element that acts on sound declares `engine`
  (AMPLibraryAgent.exe). An element that writes its storage should declare one process only, otherwise two processes
  write the same file. [preview]

### Code layout (for contributors)

The global object is made of **modules** (`core`, `config`, `storage`, `player`, `audio`, `audioAnalysis`, `keyboard`,
`notify`, `http`, `icons`, `uri`, `color`, `timers`, `console`, `ui`, `react`, `link`, `crypto`, `i18n`, `discord`,
`tray`, `midi`, `remote`, `fs`, `kit`). Each module lives in its own files, so that several people can add modules at the
same time without editing the same lines:

| Part of a module `foo` | Where |
|---|---|
| JavaScript (the piece of the prelude that fills the object and, if it receives events, `dispatch.foo`) | `core/prelude/foo.cpp`, constant `kFoo`. It runs in its own block, so its constants stay private. It sees `native`, the global object under construction, `dispatch` and the helpers of `core/prelude/frame.cpp` (`text`, `describe`, `runGuarded`). The frame does the final freeze, not the module |
| Native functions (the C++ called by `native.xxx`) | `core/natives/foo.cpp`, function `DefineFoo`, plus `StartFoo` or `StopFoo` if the module must load or give something back. Shared helpers and the permission check are in `core/api/bridge.hpp` (`Require`, `HostOf`, `State<T>` for per-element state) |
| Registration | One line in the `kModules` table of `core/api/modules.cpp` (and the declaration of `kFoo` and `DefineFoo` just above). The order of the table is the order of the prelude |
| Events from the kernel | `ExtensionLoader::DeliverEvent("foo", payload)` calls `dispatch.foo(payload)` in every active element, with no new method in the shared files |
| Host service | An optional capability, declared in the module header (`struct FooHost { static constexpr std::string_view kCapabilityName = "foo"; ... }`), read with `host.Capability<FooHost>()`, null on hosts that do not offer it. Only hosts that support it implement it (see `core/host.hpp`). The existing pure virtual methods of `Host` do not move |
| Build | A new file `cmake/sources/<package>.cmake` with `list(APPEND AMPIFY_CORE_SOURCES ...)` and, for tests, `list(APPEND AMPIFY_TESTS_SOURCES ...)` (convention at the top of `CMakeLists.txt`) |
| Tests | `tests/foo_tests.cpp`, listed in the package's `.cmake` file. `tests/api_module_tests.cpp` freezes the existing members of the global object one by one: a new member does not touch it, a change to an existing member makes it fail |
| Types and documentation | Section 5 of this document, `cli/sdk_types.cpp` (`ampify types`, checked by `tools/test_cli.py`) and, if the examples use it, the fake object of `tools/test_examples.mjs` |

## 3. Life cycle

### 3.1 Location

Installed elements live in the user's data, never in the MSIX package, which is read-only:

```
%USERPROFILE%\Ampify\
  settings.txt
  extensions\<id>\manifest.json, main.js, ...
  apps\<id>\...
  themes\<id>\...
  snippets\<id>\...
  storage\<id>.json            data of Ampify.storage
```

The path is `%USERPROFILE%\Ampify`, not `%LOCALAPPDATA%`. MSIX redirects Apple Music's writes under AppData, so the
launcher and the kernel would not see the same files. Both processes load JavaScript elements, and `Ampify.process` lets
an element tell them apart.

### 3.2 Steps

1. **Discovery.** At kernel start, each subfolder of `extensions`, `apps` and `themes` is examined. A folder without
   `manifest.json` is ignored, like a folder whose name starts with a dot (work in progress from `ampify`). Each kind has
   its own folder: an app placed in `extensions` (or the reverse) is refused. A snippet has no script, so the kernel does
   not load it.
2. **Validation.** The manifest must follow the schema, `id` must be the folder name, `kitApi` must have a supported
   major version, and the declared files must exist. On failure, the element gets the state `Failed` and a line goes to
   the log. The other elements continue.
3. **Activation.** An element only starts if it is switched on in the settings (section 3.3). A community element is off
   by default: the manifest cannot change that.
4. **Start.** The kernel evaluates `main` (for an extension or an app) in its runtime, then calls the registered
   functions. An exception gives the state `Failed`, without stopping the others.
5. **Stop.** The functions passed to `Ampify.onStop` are called in reverse order of registration. A stop that fails is
   logged and ignored.
6. **Reload.** An extension or an app reloads by itself: stop, read the folder again (manifest and code), then start.
   Apple Music does not restart. `ampify` asks for it through `reload.txt`: `start`, `stop`, `restart` and `rescan` (new
   and removed folders). Details: [../cli.md](../cli.md), "Immediate effect".

Inside the kernel, an app carries the id `app:<id>` (log, `reload.txt`), because an app and an extension can share an id.
They are then linked (section 5.8).

### 3.3 Activation settings

| Element | Key in `settings.txt` | Default |
|---|---|---|
| Extension | `extensions.<id>.enabled` | `false` for community elements |
| App | `apps.<id>.enabled` | `false` |
| Snippet | `snippets.<id>.enabled` | `false` |
| Theme | `theme.active`, `theme.scheme` | no theme |
| Theme parts | `theme.parts.colors`, `theme.parts.resources`, `theme.parts.assets`, `theme.parts.script` | `true` |

### 3.4 States

These are the states of `core/extension.hpp`:

- `Off`: switched off in the settings.
- `Running`: started.
- `Failed`: switched on, but the start failed or threw an exception.
- `Blocked`: switched on, but the kernel is inactive, either in safe mode or on an unknown Apple Music version.

### 3.5 Safe mode and unknown versions

- After two launches where the app was never "healthy", the next launch loads no element: no extension, theme, snippet
  or app. This is the safe mode of `core/safe_mode.*`.
- On an Apple Music version missing from the tested list, nothing starts. The app runs untouched. This is the rule of
  `core/version_gate.*`.

### 3.6 Several files: ES modules [preview]

An element can split its code into several files, like Spicetify's `subfiles`. With `"type": "module"` in the manifest,
`main.js` is an ES module:

```js
// main.js
import { format } from "./lib/text.js";
import settings from "./settings.mjs";
Ampify.log(format(settings.title));
```

Rules of the module loader (`core/js_runtime.cpp`, `ResolveModuleName` and `ReadModuleFile`):

- only a path relative to the importing file is accepted: `./x.js`, `./lib/x.js`, `../x.js`;
- the path never leaves the element's folder: `../` above the folder is refused;
- no bare name (`import "lodash"`), no absolute path (`/x.js`, `C:/x.js`), no `\`, no `:`;
- file and folder names are made of letters, digits, `.`, `_` and `-`, with no leading dot;
- only `.js` and `.mjs` files can be imported, 1 MB at most each;
- a symbolic link (or a Windows junction) on the path is refused.

A refused import throws a `ReferenceError` that starts with "import refused". The element then goes to the state
`Failed`, with the reason in the log. Without `"type": "module"`, `main.js` stays a classic script. It can still load a
module with `import("./x.js")`, which returns a `Promise`, under the same rules. A module shares the `Ampify` object and
the permissions of the element: importing a file gives no extra right.

## 4. Permissions

An element declares the permissions it needs in `manifest.json`. The catalog shows them, and the user sees them before
switching the element on. A function protected by an undeclared permission throws an error and does nothing.

| Permission | What it gives | Status |
|---|---|---|
| (none) | `Ampify.log`, `Ampify.onStop`, `Ampify.config`, `Ampify.uri`, `Ampify.locale` | [stable] |
| (none) | `Ampify.icons`, `Ampify.i18n`, `Ampify.crypto`, `Ampify.remote` (setting `remote.enabled`) | [preview] |
| `storage` | `Ampify.storage`, one JSON file per element, in `storage\<id>.json` | [stable] |
| `notifications` | `Ampify.notify`, a Windows notification | [preview] |
| `ui.keyboard` | `Ampify.keyboard.bind`, global or window-only shortcuts | [preview] |
| `player.read` | `Ampify.player.state()`, `Ampify.player.lyrics()`, player events and `Ampify.player.queue.list()` | [preview] |
| `player.control` | `Ampify.player.play()`, `pause()`, `toggle()`, `next()`, `previous()`, `seek()`, `queue.add()`, `queue.remove()` | [preview] |
| `audio.analyze` | `Ampify.audio.onFrame`, `analyze`, `loudness`: spectrum, levels, tempo and loudness of the decoded sound, computed in memory | [preview] |
| `audio.gain` | `Ampify.audio.setGainDb`, a gain applied to the samples, capped by the kernel ceiling | [preview] |
| `audio.eq` | `Ampify.audio.eq`, a 10-band equalizer applied to the samples | [preview] |
| `ui.topbar`, `ui.playbar`, `ui.sidebar`, `ui.menu`, `ui.popup` | Adding interface elements at those places (`ui.sidebar`: an entry at the bottom of Apple Music's sidebar) | [preview] |
| `ui.panel`, `ui.window` | Side panel, secondary window | [preview] |
| `ui.fullscreen` | `Ampify.ui.fullscreen`, a view over the whole window (a "now playing" screen with lyrics), and `Ampify.ui.applePlayer`, Apple Music's own full-screen player | [preview] |
| `ui.page` | App pages and navigation entries | [preview] |
| `library.read` | `Ampify.library`, reading the library and playlists | [planned] |
| `library.write` | Writing playlists, after reading (the queue goes through `player.control`) | [planned] |
| `services.read` | `Ampify.services.call`, read calls to an allow-list of app services | [planned] |
| `ipc.discord` | `Ampify.discord`, a local pipe to the Discord client, for presence (setting `discord.enabled`) | [preview] |
| `midi.input` | `Ampify.midi`, inputs of a MIDI controller (setting `midi.enabled`) | [preview] |
| `ui.tray` | `Ampify.tray`, entries in the menu of the Ampify notification-area icon (setting `tray.enabled`) | [preview] |
| `fs.output` | `Ampify.fs.writeText`, a text file in the `output\<id>\` folder of Ampify, and nowhere else | [preview] |
| `network:<host>` | `Ampify.http.get` and `Ampify.http.post`, only to the declared host | [stable] for the mechanism |
| `kit.manage` | `Ampify.kit.run`, which runs `ampify.exe`. Reserved to the Marketplace app that ships with Ampify. The catalog refuses it for any other element | Internal |

[planned] means the permission exists in the manifest schema and the types, but the runtime does not implement the
functions yet.

[../spicetify-parity.md](../spicetify-parity.md) names the permission each Spicetify feature needs.

No function changes the volume slider or `AVCFPlayerSetVolume`. The internal gain (`audio.gain`) acts on samples only.
There is no permission to read the disk, pick a folder, or call native code, and none will be added. `fs.output` is the
only write allowed: a text file (`.txt`, `.json`, `.csv`, `.html`, `.xml`, 256 KB and 16 files at most) in
`%USERPROFILE%\Ampify\output\<id>\`, so that another program can read it (an OBS text source). The element can neither
read it back, nor delete it, nor leave that folder.

The Ampify services (Discord presence, notification-area icon, local remote, MIDI) are all **off by default**. Each needs
an Ampify setting (`discord.enabled`, `tray.enabled`, `remote.enabled`, `midi.enabled`), which only the user changes,
in addition to the element's permission. An element cannot switch them on.

## 5. The global `Ampify` object

Examples are in JavaScript. Asynchronous functions return `Promise` values.

### 5.1 Base [stable]

```js
Ampify.version              // "0.5.0": version of the kernel (the Ampify version)
Ampify.kitApi               // "1.0": version of the targeted API
Ampify.process              // "ui" or "engine" [preview], see section 2
Ampify.log(message)         // writes to the log, prefixed with the element id
Ampify.onStop(fn)           // fn is called when the element stops
Ampify.locale               // app language, e.g. "en-US" (read-only)
```

Texts in several languages [preview], no permission:

```js
Ampify.i18n.language                    // "en": the Windows display language as two letters
Ampify.i18n.define({ en: { hello: "Hello {name}" }, fr: { hello: "Bonjour {name}" } })
Ampify.i18n.t("hello", { name: "Lea" }) // text in the display language, else in English, else the key
Ampify.i18n.t("kit.tray.next")          // Ampify's own texts, prefix kit.
```

### 5.2 Settings and storage [stable]

```js
Ampify.config.get(key, default)     // reads extensions.<id>.<key> (or apps., snippets. depending on the kind)
Ampify.config.set(key, value)       // writes the same key, limited to the element
Ampify.storage.get(key, default)    // JSON value, stored in storage\<id>.json   [permission storage]
Ampify.storage.set(key, value)
Ampify.storage.remove(key)
```

An element neither reads nor writes the keys of another element.

### 5.3 Player [preview]

```js
Ampify.player.state()
// -> { playing, positionMs, durationMs,
//      track: { id, title, artist, album, variantId, atmos, lossless, queueItemId, artwork, artworkColor } | null,
//      source: "apple" | "smtc" | "" }
Ampify.player.on("trackchange" | "statechange" | "lyricschange", fn)
Ampify.player.lyrics()      // -> null | { source: "apple", synced, language,
//                                         lines: [{ start, end, text, words: [{ start, end, text }] }], text }
Ampify.player.play()        // true when the app accepted the command
Ampify.player.pause()
Ampify.player.toggle()      // play if paused, pause if playing (player.control is enough)
Ampify.player.next()
Ampify.player.previous()
Ampify.player.seek(ms)
Ampify.player.queue.list()  // -> { available, complete, source, reason, items: [{ id, title, artist, durationMs, section }] }
Ampify.player.queue.add(trackId, { position: "next" | "last" })   // [preview]
Ampify.player.queue.remove(itemId)                                // [preview]
```

- **`track.id`** is the same whatever the source: it is computed from the title, the artist and the album (`t-` followed
  by 16 characters). An element can use it to keep per-song data.
- **`track.queueItemId`** is Apple's queue entry: the same song played twice has two entries. `trackchange` fires when
  the song, its variant or its entry changes. Empty when the source does not give it.
- **`track.variantId`** is the audio format given by Apple: `"lossless"`, `"hi-res-lossless"`, `"dolby-atmos"`,
  `"spatial"`, `"surround"` or `"other"`. Empty with SMTC. `atmos` and `lossless` follow from it.
- **`track.artwork`** is the https address of the cover at about 1000 px, or `""` (SMTC, or Apple gave none). Only
  `https://` addresses are passed on. **`track.artworkColor`** is the dominant colour of the cover, `"#RRGGBB"`, or `""`.
  Both come from Apple's player only.
- **`lyrics()`** gives Apple Music's own lyrics of the song playing (permission `player.read`), or `null` when Apple Music
  has none for it, when it has not fetched them yet, or when the source is not Apple's player. Times are in
  milliseconds from the start of the song. With `synced: true`, each line has `start` and `end`, and `words` holds the
  timed words when Apple times them (otherwise `words` is empty). With `synced: false` the lines are plain text, without
  `start` and `end`. `text` is the whole text, one line per row. Apple may only have the lyrics a few seconds after the
  song starts: `lyricschange` fires when the lyrics of the current track become available (after `trackchange` if both
  happen together). An element can also just call `lyrics()` again after `trackchange`.
- **`source`** says who answered. On Windows, in `AppleMusic.exe`, the kernel reads Apple's player
  (`AMP.Services.Player`). Otherwise (in `AMPLibraryAgent.exe`, or if a new Apple Music version changes the interface) it
  reads the Windows media controls (SMTC). `""` means "state unknown", not "stopped".
- **Commands** go through SMTC, then through a media key. `play` and `pause` use the play/pause key only if the state is
  known, so they never do the opposite. `seek` only goes through SMTC. The setting `player.appleCommands = true` makes
  `play`, `pause`, `next` and `previous` go through Apple's player first. A command that is already done (`play` while
  playing) answers `true` and sends nothing.
- **The queue.** `list()` says what the kernel knows. `complete` is `false` when the kernel knows only part of the queue
  (on Windows, for now, only the current track), and `reason` says why. `available` is `false` on a host with no queue.
  `add()` and `remove()` return `false` when the host cannot change the queue, and the reason goes to the log. On
  Windows they answer `false` until the parameters of Apple's methods are read on a real PC.

The absolute volume is read with `Ampify.audio.volume()` (section 5.4) and never written. An element that wants to change
the sound sets an internal gain on the samples (section 5.4), never the slider.

### 5.4 Audio [preview]

```js
Ampify.audio.setGainDb(db)
// Gain applied to the samples of the current song, capped by the ceiling in the kernel config.
// Never touches the volume slider. Model: the Atmos leveler (extensions/atmos-leveler/).

Ampify.audio.onFrame(fn)                        // permission audio.analyze. Returns false with no sound in this process
// fn({ bands, rms, peak, time }), about 20 times per second at most. bands: 16 bands from 40 Hz to 16 kHz
// (log spaced), each from 0 (-80 dBFS or less) to 1 (0 dBFS). rms and peak: linear level, 1 = full scale.
// time: seconds since the analysis began. A slow script gets the latest frame, with no queue.

Ampify.audio.analyze()                          // permission audio.analyze
// -> { tempo, confidence, loudness: { momentary, shortTerm }, rms, peak, bands, frequencies }
// tempo in BPM (null during the first seconds), confidence from 0 to 1, frequencies: center of each band in Hz.

Ampify.audio.loudness()                         // permission audio.analyze
// -> { momentary, shortTerm } in LUFS (BS.1770: 400 ms and 3 s), null if nothing plays. Measured before the
// kernel gain and the equalizer, as if the slider were at maximum.

Ampify.audio.volume()                           // permission player.read
// -> player volume, from 0 to 1, as the slider shows it. Read-only. null until it has been seen.

Ampify.audio.eq.setBand(index, gainDb)          // permission audio.eq. 10 bands, 31 Hz to 16 kHz, +/- 12 dB
Ampify.audio.eq.preset(name)                    // "flat", "rock", "jazz", "bass-booster"... (list: presets())
Ampify.audio.eq.reset()                         // back to flat
Ampify.audio.eq.bands()                         // -> [{ frequency, gainDb, q, type }]
Ampify.audio.eq.presets()                       // -> names of the presets
```

Sound is analyzed and equalized in `AMPLibraryAgent.exe` (an element with `"processes": ["engine"]`). In the other
process these functions do not throw: `onFrame` returns `false`, reads give `null`, and the equalizer answers `false`.

- **Where:** on the samples that the kernel hooks already handle (the stereo `IAudioRenderClient` stream and the Dolby
  Atmos spatial objects), never on Apple's audio tap, which Apple skips for Atmos
  (`knowledge/atmos-loudness.md`). These hooks are those of the Atmos leveler, so it must be switched on.
- **Order:** kernel gain, then equalizer, then soft knee and ceiling. A flat equalizer touches no sample (the output is
  bit-identical). A band change is smoothed (12 dB in 0.1 s), with no click.
- **One equalizer per process.** The last element that sets it wins, and it goes back to flat when that element stops.
- **What does not leave:** the raw samples. The script only receives small frames of numbers. Nothing is written to disk.

### 5.5 Interface [preview]

An extension describes its interface in two ways that give the same result:

- directly, with a tree of primitives;
- with `Ampify.React`, a small engine that accepts React's style (basic hooks, fragments, props) and produces the same
  tree. Spotify's internal components cannot be reused: use `Ampify.components` instead.

In both cases the kernel renders the tree as XAML with real WinUI controls (`win/ui_renderer.cpp`). Each node carries the
name and the AutomationId `AMPIFY_<element>_<node>`. A new render only changes the nodes that changed (by position, or by
the `key` attribute). The style is copied from the native controls of the Settings > Playback page. The places (side
panel, dialog popups, bar buttons) are described in [../how-it-works.md](../how-it-works.md).

Primitives: `stack`, `row`, `list`, `layer` and `card` (which have children), `text`, `button`, `toggle`, `slider`,
`textfield`, `image`, `icon`, `dropdown` (its children are `option`) and `option` (only inside a `dropdown`). Each node is
an object `{ type, ...attributes, children }`. Attributes are simple values (text, number, boolean), never objects or
lists. The kernel checks the tree (`core/ui_model.cpp`): 8 levels, 300 nodes and 500 characters per text at most.

| Primitive | Attributes read by the renderer |
|---|---|
| `stack`, `row`, `list` | `gap` (number), `children` |
| `layer` | `children`, drawn on top of each other (the first one at the back) and stretched over the layer |
| `card` | `title`, `subtitle`, `onClick`, `children` |
| `text` | `text`, `variant` (`title`, `subtitle`, `body`, `caption`), and the text style below |
| `button` | `label`, `icon` (SVG), `disabled`, `variant` (`primary`, `secondary`, `tertiary`), `onClick` |
| `toggle` | `label`, `checked`, `disabled`, `onChange` |
| `slider` | `label`, `min`, `max`, `step`, `value`, `disabled`, `onChange` |
| `textfield` | `label`, `value`, `placeholder`, `disabled`, `onChange` |
| `dropdown` | `label`, `value`, `disabled`, `onChange`, `children` (some `option`) |
| `option` | `value`, `label` |
| `image` | `src` (an `https://` address or a file path), `label` (alternative text), `onClick`, `fit` (`cover`, `contain`, `fill`), `cornerRadius`, `width`, `height` |
| `icon` | `icon` (SVG, as `Ampify.icons.get`), `label`, `onClick` |
| all | `tooltip` (text), `tooltipPlacement` (`top`, `bottom`, `left`, `right`), `tooltipDelay` (0 to 10000 ms) |

Style attributes. The kernel refuses a bad value with a message that names the attribute. They are checked where the
renderer reads them; elsewhere they are ignored, as before.

| Where | Attribute | Values |
|---|---|---|
| any node | `opacity` | 0 to 1 |
| any node | `scale` | 0 to 10, around the centre of the node (a current lyrics line that grows) |
| any node | `transition` | `"opacity 400ms"`, `"scale 0.3s"`, `"translation 200ms"`, `"all 300ms"`, several separated by commas, or `"none"`; 10 s at most. A change of these properties is then animated by the compositor |
| any node | `align`, `valign` | `start`, `center`, `end`, `stretch`: the place of the node in its parent, across and along. On a `text`, `align` is the text alignment instead: `left`, `center`, `right` |
| any node | `grow` | `true`: the node takes the space left in its `row` (width) or `stack` (height) |
| `stack`, `row`, `layer`, `card` | `background` | `#RRGGBB`, `#AARRGGBB`, `acrylic` (the theme's), or `acrylic(#AARRGGBB, 0.6)`: an acrylic that blurs what is under it in the window, with its tint and tint opacity |
| `stack`, `row`, `layer`, `card` | `padding` | a number, or `"top,right,bottom,left"` (0 to 1000 each) |
| `stack`, `row`, `layer`, `card`, `image` | `cornerRadius` | 0 to 1000 |
| `stack`, `row`, `layer`, `card` (`width` and `height` on `image` too) | `width`, `height`, `minWidth`, `maxWidth` | pixels, 0 to 10000 |
| `text` | `size` | 8 to 120 pixels |
| `text` | `weight` | 100 to 900 (400 regular, 700 bold) |
| `text` | `color` | `#RRGGBB` or `#AARRGGBB` |
| `text` | `wrap` | `false` keeps the text on one line, cut with an ellipsis (`true` by default) |
| `text` | `maxLines` | 0 (no limit) to 100, the rest cut with an ellipsis |

The children of a `layer` fill it unless their `align` or `valign` says otherwise. An `image` without `fit` and `height`
stays at most 240 pixels high, as before. In a `row` of two children, the second goes to the right, as before; a child
with `grow` changes that row into columns that fit their content, the growing ones sharing what is left.

`onClick` and `onChange` hold a callback number (a positive integer) given by the kernel. The kernel refuses `onClick`
outside `button`, `card`, `image` and `icon`, and `onChange` outside `toggle`, `slider`, `textfield` and `dropdown`. The
host reports a click with `ScriptExtension::DeliverClick(number)` and a new value with
`DeliverEvent("change", {"id": number, "value": value})`. `Ampify.React` (section 5.9) fills in these numbers.

```js
Ampify.ui.topbar.addButton({ label, icon, onClick })
Ampify.ui.playbar.addButton({ label, icon, onClick })
Ampify.ui.sidebar.addItem({ label, icon, onClick })   // icon: a Segoe Fluent Icons glyph, such as ""
Ampify.ui.menu.addItem("context" | "app", { label, onClick })
Ampify.ui.popup.open(view)
Ampify.ui.fullscreen.open(view)        // permission ui.fullscreen; called again, it updates the view in place
Ampify.ui.fullscreen.close()
Ampify.ui.fullscreen.onClose(fn)       // returns a function that removes fn
Ampify.ui.applePlayer.open()           // permission ui.fullscreen; Apple Music's own full-screen player
Ampify.ui.panel.register(id, view)
Ampify.ui.title.set(text)
Ampify.ui.page.register(path, view)     // for an app
Ampify.ui.navigate(path)
```

Example:

```js
Ampify.ui.playbar.addButton({
  label: "Lyrics",
  icon: Ampify.icons.get("lyrics"),
  onClick: () => Ampify.ui.popup.open(lyricsView),
});
```

**Fullscreen view.** `Ampify.ui.fullscreen.open(view)` covers the whole Apple Music window, above everything it draws,
in its light or dark mode. Apple Music's navigation and the window's size changes leave it in place. A Close button (top
left, clear of the window's own buttons) and the Escape key close it; the focus then goes back to where it was. There is one fullscreen view at a time:
the last element that opens it wins. `onClose` functions run when the user closes it or when another element takes the
screen, not after `close()`. After that, calling `open` opens it again, so a view refreshed by a timer should stop it in
`onClose`. A "now playing" screen usually puts a `layer` at the root:

```js
Ampify.ui.fullscreen.open({ type: "layer", children: [
  { type: "image", src: artworkUrl, fit: "cover" },
  { type: "stack", background: "acrylic(#99000000, 0.6)", padding: 64, children: [
    { type: "text", text: title, size: 48, weight: 700, align: "center" },
    { type: "text", text: line, size: 32, opacity: 0.5, transition: "opacity 400ms, scale 300ms" },
  ] },
] });
```

**Apple's player.** `Ampify.ui.applePlayer.open()` opens the full-screen player of Apple Music, the one its mini player
opens, and lays its window over the main window: same place and size, no title bar, edges that resize, and a
background that moves the window when dragged. Apple draws everything in it (cover, lyrics, controls). When Apple Music
refuses (its player has no view model yet, or another version of Apple Music changed its interfaces), nothing opens and
the log says why. Now Playing (`extensions/now-playing`) is a button of the playback bar that calls it.

Menus are not displayed yet.

An extension cannot inject raw XAML in API 1.0. This stops a community element from touching parts of the interface that
have not been mapped. Themes are the only case where an element ships XAML (section 6). A later version may accept XAML
checked against a list of allowed types and properties (see section 11).

### 5.6 Services [preview]

```js
Ampify.notify({ title, text })                      // [preview] permission notifications
Ampify.keyboard.bind("Ctrl+Alt+L", fn)              // [preview] permission ui.keyboard
Ampify.http.get(url) -> Promise<{ status, text }>   // [stable] permission network:<host>
Ampify.http.post(url, body, { headers }) -> Promise<{ status, text }>  // [preview] same rules as get
Ampify.crypto.md5(text) -> hexadecimal string       // [preview] for services that sign this way (Last.fm)
Ampify.uri.parse(text) / Ampify.uri.build(...)      // [stable] music:// and https://music.apple.com links
Ampify.icons.get(name) -> SVG string                // [preview]
Ampify.color.extract(pixels, width, height)         // [preview] no permission
// pixels: RGBA, row by row (array, Uint8Array or ArrayBuffer). Returns the keys of Spicetify's colorExtractor:
// PROMINENT, VIBRANT, DARK_VIBRANT, LIGHT_VIBRANT, DESATURATED, VIBRANT_NON_ALARMING ("#rrggbb"),
// and palette: [{ color, population }]. Images by address are not read yet.
Ampify.color.fromArtwork(url) -> Promise<color>     // [planned]
Ampify.library.playlists() / Ampify.library.tracks()   // [planned] permission library.read
Ampify.services.call(service, method, args)         // [planned] permission services.read, allow-list
Ampify.lyrics.current() -> Promise<lyrics>          // [planned] permission player.read
```

`Ampify.http.get` is only for third-party services (Last.fm, ListenBrainz, lyrics providers). It cannot reach Apple's
internal APIs: the kernel refuses any host that is not in the permissions of the manifest. `Ampify.http.post` follows the
same rules (https, declared host). A text body is sent as a form (`application/x-www-form-urlencoded`), and an object is
sent as JSON. Only the headers `content-type`, `authorization` and `accept` are accepted, with no line break. The body is
64 KB at most. Both calls return at once: the request runs off the JavaScript thread, so a slow server does not freeze
the element's timers or events. An element may have 4 requests in flight; the next call rejects with a `RangeError`.

`Ampify.services.call` will not create a new connection: it will call the services the app already uses, and only those
on the allow-list published with Ampify.

### 5.7 Local services [preview]

Each service is off until the user sets its setting to 1 (`ampify config discord.enabled 1`...). When off, or absent
from the host, a call returns `false` and writes a line to the log, without throwing.

```js
// Discord presence, permission ipc.discord, setting discord.enabled
Ampify.discord.setActivity({ clientId, details, state, largeImage, largeText, smallImage, smallText, startMs, endMs })
Ampify.discord.clear()

// Menu of the Ampify icon in the notification area, permission ui.tray, setting tray.enabled (AppleMusic.exe)
Ampify.tray.setMenu([{ label, onClick, checked, enabled }, "-"])   // "-": separator, 20 entries at most
Ampify.tray.onClick(fn)                                            // left click on the icon

// MIDI controller, permission midi.input, setting midi.enabled (AMPLibraryAgent.exe)
Ampify.midi.on("note:60" | "cc:7" | "ch2:note:36" | "note:*" | "*", fn)   // fn({ type, channel, number, value })
Ampify.midi.devices() -> ["name", ...]

// Local remote, setting remote.enabled (AMPLibraryAgent.exe), no permission
Ampify.remote.setState(object)         // added to GET /status under extensions.<id>, 4 KB at most

// Output file, permission fs.output
Ampify.fs.writeText("now-playing.txt", text)   // -> output\<id>\now-playing.txt, replaced in one step
```

`clientId` is the id of an application created on the Discord developer portal (17 to 20 digits). The last element that
calls `setActivity` wins, and its presence disappears when it stops.

**Local remote.** An HTTP server bound to `127.0.0.1` only, on the port `remote.port` (8974 by default). The token
`remote.token` is created when `remote.enabled` is set to 1 (`ampify config remote.token` prints it). It goes in the
`X-Ampify-Token` header (or `Authorization: Bearer`), never in the address. A request that comes from a web page (an
`Origin` header, an `OPTIONS` preflight) or with a `Host` other than `127.0.0.1` or `localhost` is refused.

| Request | Effect |
|---|---|
| `GET /status` | `{ playing, positionMs, durationMs, track, gainDb, extensions }` |
| `POST /play`, `/pause`, `/next`, `/previous` | Player command, `409` if the app cannot do it now (on Windows, `play` and `pause` wait for the player state) |
| `POST /volume?db=-6` (or the body `{"db": -6}`) | Internal gain on the samples, capped by `audio.maxGainDb`. The volume slider does not move |

```sh
curl -H "X-Ampify-Token: <token>" http://127.0.0.1:8974/status
curl -X POST -H "X-Ampify-Token: <token>" http://127.0.0.1:8974/next
```

**MIDI.** `midi.device` picks the device (0 for the first). `midi.map` links keys to Ampify commands, with no element:
`note:60=next;note:62=previous;cc:20=play;cc:7=gain` (`gain`: a knob from -30 to 0 dB). There is no light feedback to
the controller.

### 5.8 Link between an app and its extension [preview]

An app and an extension with the same id are **linked** (Spicetify's `subfiles_extension`). Both run in the same process,
each in its own engine. They exchange JSON messages:

```js
// extensions/lyrics/main.js: runs at start
Ampify.link.on((message) => {
  if (message.type === "request") Ampify.link.post({ type: "lyrics", text: "..." });
});

// apps/lyrics/main.js: the page
Ampify.link.on((message) => Ampify.log("received: " + message.text));
Ampify.link.post({ type: "request" });
```

```js
Ampify.link.post(message)   // sends a JSON value (64 KB at most); true if the other element is running
Ampify.link.on(fn)          // fn(message) receives each message from the other; returns a function that removes fn
Ampify.link.connected()     // true if the other element is running
```

- A message is copied as JSON: a function or `undefined` throws a `TypeError`, and a message over 64 KB throws a
  `RangeError`.
- It arrives on the next turn of the JavaScript thread, never during the `post` call. Message order is kept.
- If the other element is stopped or still starting, `post` returns `false` and the message is lost. The simplest
  pattern: the one that starts second sends a first message, and the other answers.
- No permission is needed: nothing leaves the process, and only the two elements with the same id talk to each other. A
  theme script is linked to nothing.
- The app and the extension share the file `storage\<id>.json` (same id). Only one of them should write its storage. The
  other goes through `Ampify.link`.

### 5.9 React, components and `Ampify.ready` [preview]

Spicetify gives React to extensions. A native app cannot run real React, so Ampify provides a small engine that accepts
the same style, without JSX, and produces the primitive tree of section 5.5. It has no permission of its own: the target
asks for the permission of its place (`ui.panel`, `ui.page`, `ui.popup` or `ui.fullscreen`).

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
Ampify.React.memo(component)                          // returns the component as it is
Ampify.React.useState(initial)                        // also useReducer, useEffect, useLayoutEffect,
Ampify.React.useContext(context)                      //   useRef, useMemo, useCallback
Ampify.hooks.usePlayer()                              // player state, followed by its events [permission player.read]
Ampify.ReactDOM.render(element, target)
Ampify.ReactDOM.createRoot(target)                    // -> { render(element), unmount() }
Ampify.ReactDOM.unmountComponentAtNode(target)
Ampify.ready                                          // Promise resolved when the kernel has started the element
```

`Ampify.hooks` takes over the hooks of `Ampify.React`, plus `usePlayer`.

Rules:

- **Element types:** a primitive name (`"stack"`, `"text"`...), a component function, `Fragment`, or the `Provider` or
  `Consumer` of a context. A few HTML names are accepted: `div` and `section` give `stack`, `span`, `p` and `label` give
  `text`, `h1` gives a `text` with variant `title`, `h2` and `h3` the variant `subtitle`, `img` gives `image`, `select`
  gives `dropdown`, `ul` gives `list`. No classes (`React.Component`). There is no JSX in the kernel: a tool can compile
  JSX to `Ampify.React.createElement`.
- **Props:** a text, number or boolean becomes an attribute. `style`, objects, lists and other functions are ignored.
- **Text children:** in a container, a text becomes a `text` primitive. In `text`, `button`, `toggle` and `option`, the
  texts are joined and go in the attribute `text` or `label`.
- **Events:** `onClick` and `onChange` become a callback number that does not change from one render to the next. The
  handler receives `{ type: "click", id }` or `{ type: "change", id, value, target: { value, checked } }`.
- **Rendering:** a state change renders the whole target once, at the end of the current kernel call (click, timer,
  event). Several `setState` calls in one handler give one render. The tree goes back to the kernel only if it changed.
  More than 50 renders in a row are stopped, with a line in the log.
- **Effects:** they run after the tree is sent, those of children before those of parents. Their cleanup runs before the
  next effect, when the component unmounts, and when the element stops.
- **Targets:** `"panel:<name>"`, `"page:/<path>"` (for an app), `"popup:<name>"`, or `"popup"` for `"popup:main"`, and
  `"fullscreen:"` (or `"fullscreen"`) for the fullscreen view. The objects `{ panel }`, `{ page }`, `{ popup }` and
  `{ fullscreen: true }` work too. A named popup and the fullscreen view are updated in place at each render, and
  `unmount` closes them. When the user closes the fullscreen view, its root unmounts by itself (effect cleanups run).
  Unmounting a panel or a page leaves an empty stack there. Each `Ampify.ui.navigate` to a page mounts it anew, as
  Spotify does with a Spicetify app the user comes back to: its state starts over and its effects run again, so an
  effect with `[]` that reads data shows fresh data at each visit. A value to keep between visits goes in a variable
  outside the component.
- **Errors:** an error in `ReactDOM.render` is thrown, so `main.js` fails with a clear message. An error in a later
  render goes to the log, and the previous tree stays displayed.
- **Apps:** an app (`kind: app`) that defines a global function `render()`, like a Spicetify app, has what it returns
  displayed on the page `/<id>` at start, before `Ampify.ready` resolves (permission `ui.page`).
- **`Ampify.ready`:** resolved on the first turn of the kernel loop after `main.js`. The element is then started, and its
  clicks and events arrive. It is the equivalent of waiting for `Spicetify.Player` or `Spicetify.Platform`.

Components of `Ampify.components`, in place of `Spicetify.ReactComponent`:

| Component | Props | Primitive |
|---|---|---|
| `Button` (and `ButtonPrimary`, `ButtonSecondary`, `ButtonTertiary`) | `label` (or a text child), `icon`, `disabled`, `variant`, `onClick` | `button` |
| `Toggle` | `label`, `value` or `checked`, `disabled`, `onChange(checked)` | `toggle` |
| `Slider` | `label`, `min`, `max`, `step`, `value`, `disabled`, `onChange(number)` | `slider` |
| `TextField` | `label`, `value`, `placeholder`, `disabled`, `onChange(text)` | `textfield` |
| `Dropdown` | `label`, `value`, `options` (texts or `{ value, label }`), `disabled`, `onChange(value)` | `dropdown` and `option` |
| `Card` | `title`, `subtitle`, `onClick`, children, the box style (`background`, `padding`, `cornerRadius`, sizes) | `card` |
| `List` | children | `list` |
| `Text` (and `TextComponent`) | `text` or children, `variant`, `size`, `weight`, `color`, `align`, `wrap`, `maxLines` | `text` |
| `Tooltip` (and `TooltipWrapper`) | `label` or `content`, `placement`, `delay` or `showDelay`, children | adds `tooltip`, `tooltipPlacement` and `tooltipDelay` to each child |

Each component also accepts `tooltip`, `tooltipPlacement` and `tooltipDelay`. `Button`, `Card` and `Text` also pass
`opacity`, `scale`, `transition`, `align`, `valign` and `grow` through. Other primitives (`layer`, `image`, `stack`) take
their style attributes directly: `h("layer", null, ...)`. `Tooltip` wraps a primitive or a component
of `Ampify.components`. A component written by the extension must pass these three props to its primitive. The second
argument of `onChange` is the full event. Full example: `examples/react-panel/`.

## 6. Themes, apps and snippets

### 6.1 Themes [preview]

A theme is a folder with `colors.ini`, `resources.xaml`, or both, and optionally a `main.js`. The full format is in
[../themes/format.md](../themes/format.md).

- `colors.ini` has one section per scheme (`[Base]`, `[Dark]`...). Each key is a hexadecimal color without `#`, as in
  Spicetify. Each key replaces a XAML brush of the app.
- `resources.xaml` is a resource dictionary. It can only define keys on the allowed list (`resource-keys.txt`). A key
  outside the list makes the theme fail, with a line in the log.
- `main.js` follows the manifest permissions like an extension. It runs while the theme is the active one and
  `theme.parts.script` is on.

### 6.2 Apps [preview]

An app is an extension that adds a navigation page (`Ampify.ui.page.register`), with an SVG icon in two states (`icon`,
and `active-icon` planned). It lives in `apps\<id>` and is switched on with `apps.<id>.enabled`
(`ampify app enable <id>`). In this version its code runs as soon as it is switched on, like an extension. The icon does
not make a sidebar entry yet: the app adds one (`Ampify.ui.sidebar.addItem`) that opens its page with
`Ampify.ui.navigate("/<path>")`. The page then takes the place of Apple Music's page, on the theme's page panel, with a
Back button; Apple Music's own navigation (sidebar, back arrow, search) closes it. An app can have an extension with
the same id, which runs at start: the two talk through `Ampify.link` (section 5.8). Its code can be split into modules
(section 3.6).

### 6.3 Snippets [preview]

A snippet is a fragment of `resources.xaml`, switched on alone. It follows the same list of allowed keys as themes. It
can also have a `user.css`, applied after the active theme. Details: [../themes/format.md](../themes/format.md), section
"Snippets".

## 7. Command line

The `ampify` command line replaces Spicetify's. The full list is in [../cli.md](../cli.md). The main commands for
extension authors:

| Command | Purpose | Spicetify equivalent |
|---|---|---|
| `ampify config [key] [value]` | Read or write a setting | `spicetify config` |
| `ampify extension list` | List extensions, their state and their permissions | `config extensions` |
| `ampify extension enable <id>` / `disable <id>` | Switch an extension on or off | `config extensions <id>` and `-` |
| `ampify install <id>` | Install from the catalog, SHA-256 checked | Marketplace |
| `ampify theme set <id> [--scheme <name>]` | Pick a theme and a scheme | `config current_theme` |
| `ampify watch` | Reload the theme at each save | `spicetify watch` |
| `ampify launch` | Start Apple Music with the kernel | `spicetify auto` |
| `ampify restore` | Start Apple Music with no injection. There is nothing to restore in the package | `spicetify restore` |
| `ampify path` | Print the paths of Ampify and Apple Music | `spicetify path` |
| `ampify update` | Update Ampify from a release | `spicetify upgrade` |
| `ampify new`, `dev`, `test`, `build`, `types` | Author tools | Creator |

## 8. Log and errors

- Each log line names the element, for example `Extension hello-extension started`.
- An error in `main.js` is logged with its message, without stopping other elements.
- A function called without permission throws a `PermissionDenied` error, with the name of the missing permission.
- Repeated calls to `Ampify.log` are not limited in API 1.0. A limit may come later.

## 9. Versions

- `kitApi` has the format `MAJOR.MINOR`.
- A major version changes when a stable function is removed or changed in an incompatible way.
- A minor version adds functions without breaking anything.
- A removed function stays available for one minor version, with a warning in the log.
- An element declares the version it targets. The kernel accepts any minor version of the same major.

## 10. Open points

These points must be confirmed before items move to [stable]:

- the size of quickjs-ng and its behavior under MinGW and MSVC;
- the shape of the data of `Ampify.player` and its events, on the current Apple Music version (the player interfaces
  changed in 1.1792: the log says "IPlayer absent" and the song is not seen through Apple's player);
- the shape of the frames of `Ampify.audio.onFrame` and the number of bands (to confirm on a PC);
- the places of interface elements in the XAML tree;
- the list of allowed XAML keys for themes;
- the time and memory limits of an element (to set from measurements);
- the paths that depend on the real app: navigation, the sidebar, the places of the top bar and the playback bar, the
  queue, the telemetry hosts.

## 11. Outside API 1.0, with their path

Nothing is ruled out for good. Each point stays outside API 1.0 for a specific reason.

- **Community native DLLs.** They would run with every right inside the Apple Music process. Path: a later phase, with a
  publisher signature and a code review. The catalog would list them separately, with their own trust mode.
- **Raw XAML in extensions.** Path: XAML checked by the kernel against a published list of types and properties, before
  rendering.
- **Absolute volume.** It follows the slider without ever writing it. The internal gain stays the only way to change the
  sound.
- **Apple internal services.** An allow-list of read calls. Apple's public MusicKit API needs an Apple Developer account:
  an option to open later.
- **Telemetry.** An option `telemetry.block`, off by default, that never touches license or DRM hosts.
- **Per-version patches.** A table of byte patterns, with no fixed address, for each tested version.

## 12. Implementation status

| Area | State | Verified by |
|---|---|---|
| `version`, `kitApi`, `process`, `locale`, `log`, `onStop` | Done | C++ tests and Wine |
| `config.get/set`: one key per element, never `enabled` | Done. `get` returns a boolean, number or text according to the default value | C++ tests |
| `storage.get/set/remove`: a JSON file per element, 1 MB quota | Done. Written through a temporary file then replaced. By default an element runs only in the interface, so one writer at a time | C++ tests and Wine |
| `processes`: `ui`, `engine`, or both | Done. An element of another process is ignored, with a log line | C++ tests |
| `player.state`, `on`, `play`, `pause`, `toggle`, `next`, `previous`, `seek` | Done. On Windows: Apple's player (`AMP.Services.Player`, in AppleMusic.exe), else SMTC, else media keys (`win/player_bridge.*`). Commands go through SMTC, then the key; through Apple's player with `player.appleCommands` | C++ tests (slots checked against `knowledge/winmd`); fake WinRT objects under Wine; real playback to check on a PC |
| `player.queue.list`, `add`, `remove` | Runtime done (capability `QueueHost`, `core/player_queue.hpp`). On Windows `list` gives the current track (`complete: false`), and `add` and `remove` answer `false` with the reason in the log. A read-only probe describes `AMP.Services.PlayQueue` in the log | C++ tests; probe under Wine with fake objects; the real queue is still to be read on a PC |
| `audio.setGainDb`: capped by `audio.maxGainDb` (6 dB by default, 12 at most), back to 0 at stop | Done | C++ tests |
| `notify` (title 120 characters, text 500) | Done. On Windows: a balloon notification (temporary icon, 8 s) and a log line | C++ tests; balloon to check on a PC |
| `keyboard.bind` (normalized form, an F key, a media key or a modifier is required) | Done. On Windows: `RegisterHotKey`, handed back to the JavaScript thread | C++ tests; simulated press under Wine |
| `http.get`, `http.post` (https, declared host, 1 MB at most for the reply; 64 KB for a body) | Done. On Windows: WinHTTP, timeout of 5 to 10 s, an error returns status 0 instead of throwing. The request runs on a worker thread of the element and the promise resolves on a later pump; 4 requests in flight at most per element, answers that come after a stop are dropped | C++ tests; failure tested under Wine, success to check on a PC |
| `crypto.md5`, `uri.parse`, `uri.build`, `icons.get`, `setTimeout`, `setInterval`, `queueMicrotask`, `console` | Done | C++ tests |
| ES modules (`"type": "module"`, relative `import` limited to the element's folder, `import()`) | Done | C++ tests, `tools/test_validate.py` |
| `link.post`, `link.on`, `link.connected`: app and extension with the same id | Done | C++ tests |
| Permissions: `PermissionDenied` at run time, refusal by the manifest reader | Done | C++ tests |
| Folder discovery (`extensions`, `apps`, theme scripts), activation, reload (`Restart`), failure isolation | Done | C++ tests and Wine |
| Immediate effect through `reload.txt`: `start`, `stop`, `restart` (manifest re-read), `rescan` | Done in the kernel | C++ tests, CLI tests |
| `audio.onFrame`, `analyze`, `loudness`, `volume`, `eq` | Done. On Windows: wired to the Atmos leveler hooks (stereo stream and spatial objects), which must be switched on | C++ tests (synthetic signals); equalizer and analysis seen on the stereo stream under Wine; listening to do on a PC |
| `color.extract` (pixels supplied by the script) | Done | C++ tests |
| `ui.*`, `React`, `ReactDOM`, `hooks`, `components`, `ready` | Done: native rendering (panels, popups, pages, bar buttons) and the "Ampify" card in Settings > Playback | C++ tests (tree, diff, markers on a fake tree); the interface case of the Windows bench |
| `i18n` (`language`, `define`, `t`) | Done | C++ tests and `tools/test_cli.py` |
| `discord.setActivity`, `clear` | Done. On Windows: pipe `\\.\pipe\discord-ipc-0` to `9`, on its own thread, retry every 15 s | C++ tests; fake Discord server under Wine; real Discord to check on a PC |
| `tray.setMenu`, `onClick` | Done. On Windows: `Shell_NotifyIcon`, a message window on its own thread, in AppleMusic.exe | C++ tests; Wine refuses the icon (test skipped); to check on a PC |
| `midi.on`, `devices`, and `midi.map` | Done. On Windows: `midiInOpen` (winmm), in AMPLibraryAgent.exe | C++ tests; bindings tested under Wine with no device; a real controller to check on a PC |
| `remote.setState` and the local remote | Done. On Windows: Winsock on `127.0.0.1` only, in AMPLibraryAgent.exe | C++ tests; local client and refusal of other addresses under Wine |
| `fs.writeText` | Done. `output\<id>\` of Ampify only | C++ tests and Wine |
| `kit.run` | Done, Marketplace only | C++ tests |
| `library`, `services`, `lyrics`, `color.fromArtwork` | Planned. Typed in `ampify.d.ts`, not implemented | |

Known limit: an element that runs in both processes and writes its storage can lose an update, which is why
`processes` exists.
