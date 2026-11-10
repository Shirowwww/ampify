# The `ampify` command line

`ampify` manages Ampify from a terminal: themes, extensions, the Marketplace, settings, and tools for authors. The
installer puts it in your Path. Everything it changes lives in `%USERPROFILE%\Ampify`; it never writes into Apple
Music's package.

```
ampify install glass                     install a theme from the catalog (shows the permissions, asks first)
ampify theme set glass --scheme Light    use it (a theme must be installed first)
ampify install sleep-timer               install an extension the same way
ampify extension enable sleep-timer      switch it on
ampify doctor                            check that everything is in order
```

`ampify help <command>` (or `ampify <command> --help`) shows the lines of one command.

## Settings and state

| Command | What it does |
|---|---|
| `ampify config` | Prints every setting |
| `ampify config <key>` | Prints one setting (or its default, said on the error stream) |
| `ampify config <key> <value>` | Writes a setting. The key must be known and the value is checked |
| `ampify config extensions +id` / `-id` | Switches an element on or off (also `apps`, `snippets`) |
| `ampify config --dir` | Opens the Ampify folder in Explorer |
| `ampify settings <kind> <id>` | Shows what an extension, theme or snippet lets you set, and the values now |
| `ampify settings <kind> <id> <key> <value>` | Changes one of them, checked against the element's manifest |
| `ampify path [name]` | Prints the Ampify and Apple Music folders, or one of them: `kit`, `settings`, `extensions`, `themes`, `apps`, `snippets`, `logs`, `backups`, `app` (Apple Music package), `prefs` (Apple Music preferences) |
| `ampify list` | Every installed element with its kind, version and state |
| `ampify version` | Ampify's version |

## Elements

| Command | What it does |
|---|---|
| `ampify extension list` / `enable <id>` / `disable <id>` | Extensions, including the built-in `atmos-leveler`. A running Apple Music starts or stops it at once |
| `ampify snippet list` / `enable <id>` / `disable <id>` | Snippets |
| `ampify app list` / `enable <id>` / `disable <id>` | Apps |
| `ampify theme list` | Installed themes, usable or refused |
| `ampify theme set <id> [--scheme <name>]` | Uses a theme. Without `--scheme`, a new theme starts on its default scheme |
| `ampify theme clear` | Back to Apple Music's own look |
| `ampify theme check <folder> [--scheme <name>]` | Reads a theme as the kernel does and reports each refused rule with its line |
| `ampify theme watch <folder> [--scheme <name>]` | Checks the theme again at each save |
| `ampify theme names <inspector.json>...` | Checks the [stable names](themes/names.md) against saved interface trees. Exit code 1 when a name matches nothing |
| `ampify theme replay <folder\|none> --tree <inspector.json> --out <folder>` | Runs the theme engine on a saved tree, without Apple Music: a report and a wireframe ([offline bench](development/offline-bench.md)). Exit code 1 when it does not settle |
| `ampify theme diff <report\|folder> <report\|folder> [--out <file>]` | What changed between two replays. Exit code 1 when something did |
| `ampify theme scrub <inspector.json> <out.json>` | A copy of a saved tree without texts, free names or addresses |
| `ampify theme import <folder\|zip\|address> [id]` | Brings in a Spicetify theme ([details](themes/spicetify.md)) |
| `ampify new <extension\|theme\|snippet\|app> <id>` | Writes a working starter in the current folder |
| `ampify validate <folder>` | Checks an element's manifest and files with the catalog's rules (or a `catalog.json`) |

## Marketplace and updates

| Command | What it does |
|---|---|
| `ampify market` | Lists the catalog and what is installed (`--json` for the Marketplace app) |
| `ampify market readme <id>` | Prints an element's `README.md`: the installed copy's, or the one published next to the catalog's archive. Nothing when it has none |
| `ampify install <id> [--yes]` | Downloads, checks the SHA-256, validates and installs. The element starts off ([what is checked](extensions/catalog.md#what-install-checks)) |
| `ampify update <id> [--yes]` | Updates an element when the catalog has a newer version |
| `ampify update --elements` | Updates every installed element to its catalog version, then writes `active.txt` again. Ampify runs it by itself at the first start of a new release, after it replaced the retired elements; offline, the elements stay as they are and the next start tries again |
| `ampify remove <id>` | Removes an element, after a backup |
| `ampify update` | Downloads the latest Ampify release and checks it. `--check` only says whether one exists, `--apply` installs it (Apple Music closed). `--auto` follows `kit_update`, at most one look a day: the launcher runs it once at each sign-in, and every hour while Apple Music is closed where it stays in the background (Windows refused to start it with Apple Music), with its output in `logs\update.log` |
| `ampify install-kit <version> [--apply]` | Installs a given Ampify release, even an older one |
| `ampify versions [list]` | The tested Apple Music versions, and the installed one: every build of a tested family (same first two numbers, such as 1.1792) is accepted when its player library still exports what Ampify needs |
| `ampify versions update` | Downloads the list of tested versions published with the catalog into `versions.txt` |
| `ampify notice safe-mode` / `notice version <v>` / `notice exports <v>` | The Windows notification the launcher starts, once, when Ampify stays off (safe mode, an Apple Music version not tested yet, or a tested one whose player library no longer matches; only the first can say a new list just arrived). `--dry-run` prints its text instead |

## Applying

| Command | What it does |
|---|---|
| `ampify apply` | Writes what must load (`active.txt`), tells a running Apple Music, and makes Ampify load each time Apple Music starts |
| `ampify restore` | Ampify no longer loads with Apple Music. An open Apple Music keeps it until it closes |
| `ampify restore --backup <name>` | Puts back the settings and elements of a backup |
| `ampify uninstall` | Opens the uninstaller, the same one as Settings > Apps: the program, its start at sign-in, its loading in Apple Music and its Settings > Apps entry go, and Apple Music is back as it was. It asks whether to delete your themes, settings and extensions too (No by default). `--yes` removes it without a window and keeps them; add `--delete-data` to delete them |
| `ampify launch` (or `auto`) | Applies, then starts Apple Music with the kernel now |
| `ampify backup` / `ampify backup list` | Backs up the settings and elements, or lists the backups |
| `ampify watch [--interval ms]` | Applies again at each change of a theme, extension or setting |

Commands without arguments can follow each other: `ampify backup apply launch`. The chainable ones are `backup`,
`apply`, `launch`, `auto`, `restore`, `devtools`, `doctor` and `version`. The first failure stops the line.

## Authoring

| Command | What it does |
|---|---|
| `ampify dev <folder>` | Copies the element into Ampify, switches it on, and reloads it at every save; a `main.ts`, `main.tsx` or `main.jsx` is built into `main.js` first (needs esbuild) |
| `ampify test <folder>` | Runs `main.js` in the same JavaScript engine with a fake player and network. A call without its permission fails the test |
| `ampify build <folder> [file.zip]` | Builds a `main.ts`, `main.tsx` or `main.jsx` (needs esbuild), validates, writes a reproducible zip, and prints its SHA-256 and catalog entry |
| `ampify publish <folder> [--dry-run]` | Checks and tests the element, then opens a pull request on the [public catalog](extensions/catalog.md) with the GitHub CLI (`gh`), or prints the steps to follow in the browser |
| `ampify types [file]` | Writes `ampify.d.ts`, the types of the `Ampify` object for your editor (`--yes` to overwrite) |
| `ampify inspect <folder>` or `<kind> <id>` | Prints the manifest, permissions, state and each file with its SHA-256 |
| `ampify inspect --tree` | Prints the last interface tree written by the kernel (`logs\inspector.txt`) |
| `ampify devtools [on\|off]` | Asks the running kernel for the interface tree of the page on screen, and waits for it (up to 10 s) |
| `ampify devtools pick` | The element picker: after 4 seconds to put the pointer on an element of Apple Music, prints the element under it, the selector a theme can use, and the elements above it (`logs\picked.txt`). Windows only |

## Diagnostics and transfers

| Command | What it does |
|---|---|
| `ampify doctor` | Checks settings, elements, safe mode, logs, tools and Apple Music versions. On Windows it also gives the installed Apple Music version, warns about a library agent running without Apple Music, about programs known to crash Apple Music (3Dconnexion's driver, RivaTuner, which draws MSI Afterburner's on-screen display) with their fix, and about Apple Music crash dumps of the last 7 days. With the freeze log on (`apple.watchdog`), it gives the stalls each log holds, the longest, and the last lines. Lines start with `[OK]`, `[WARNING]` or `[ERROR]`; exit code 1 on an error |
| `ampify logs [count] [--follow]` | The last lines (50 by default) of each log; `--follow` keeps printing |
| `ampify report [file.zip] [--tree] [--dir]` | Writes an anonymised zip to attach to an issue (see below) |
| `ampify profile list` | Lists settings profiles |
| `ampify profile save\|use\|delete <name>` | Saves, loads (after a backup) or deletes a copy of `settings.txt` in `profiles\<name>\`. The remote's `remote.token` stays out of a profile, and loading one keeps the current token |
| `ampify export <file.zip>` | Exports the settings and installed elements, without `remote.token` (an import keeps the current one) |
| `ampify import <file.zip> [--dry-run] [--yes]` | Imports an export after validating every element. Writes nothing if one fails |

`ampify report` puts `system.txt` (versions, language, displays), `doctor.txt`, the settings, the logs, every line of the freeze log (`freezes.txt`) and, on Windows,
recent crashes of Apple Music or Ampify in one zip under `reports\`. The user and computer names, e-mail addresses, what
was played and secret-looking values are masked first. Nothing is sent: look through the zip, then attach it.

## Options

| Option | Effect |
|---|---|
| `--kit-dir <folder>` | Ampify folder (default `%USERPROFILE%\Ampify`) |
| `--catalog <source>` | Catalog: a local file, `file://` or `https://` |
| `--dry-run` | Shows what would happen and writes nothing: `apply`, `launch`, `restore`, `uninstall`, `import`, `publish`, `devtools`, `config --dir`. Other commands refuse it |
| `--yes`, `-y` | Answers yes to every question |
| `--delete-data` | With `uninstall`: deletes the Ampify folder (themes, settings, extensions) too |
| `--scheme <name>` | Color scheme, with `theme set`, `theme check` and `theme watch` |
| `--max-cycles <n>` | Stops `watch`, `theme watch`, `dev` or `logs --follow` after n passes |
| `--json` | `market`: prints the catalog, the installed elements and the active theme as JSON |
| `--tree` | `inspect`: the interface tree. `report`: adds it to the zip |
| `--dir` | `config`: opens the Ampify folder. `report`: opens the folder of the zip |
| `--release-url <address>`, `--install-dir <folder>` | `update`: another release address, another install folder |
| `--` | Everything after it is an argument |

Exit codes: `0` success, `1` failure, `2` wrong usage. Errors go to the error stream, prefixed with `ampify:`.

## Settings

`ampify config <key> <value>` writes these keys in `settings.txt`. An unknown key is refused with the list of known
ones. Services (`discord`, `tray`, `remote`, `midi`) are read when Apple Music starts: restart it after a change.

| Key | Value | Default |
|---|---|---|
| `extensions.<id>.enabled`, `apps.<id>.enabled`, `snippets.<id>.enabled` | `1` or `0` | `0` |
| `extensions.<id>.<key>`, `themes.<id>.<key>` | The settings an element declares in its manifest | the element's default |
| `theme.active` | Theme id, empty for none | empty |
| `theme.scheme` | A section of `colors.ini`, or `auto` to follow Windows' light or dark mode | the theme's default |
| `theme.schedule` | `HH:MM=Scheme,...` ([format](themes/format.md#automatic-scheme)) | empty |
| `theme.accent` | `RRGGBB` or `#RRGGBB`: the accent of every theme, and of Apple Music's own look with no theme. `artwork`: the accent follows the song playing. What Ampify derives from the accent follows it ([details](themes/format.md#chosen-accent)) | empty: the theme's own |
| `theme.glass` | `full`, `balanced` or `eco`: how much glass every theme may ask of the graphics card ([details](themes/format.md#glass-level)) | `full` |
| `theme.parts.colors`, `.resources`, `.assets`, `.script` | `1` or `0`: turn off a part of the theme | `1` |
| `theme.live_reload` | `1` or `0`: apply the theme again when its files change | `1` |
| `theme.inspect` | `1`: the kernel writes the interface tree once, then sets `0` | `0` |
| `xaml.watcher` | `1`: WinUI tells Ampify of each element it adds, and the theme reaches it at once. No flash measured, but the interface slows down over a long session (Apple Music keeps a record of every element): leave it off | `0` |
| `language` | `en`, `fr`, `de`, `es`, `pt`, `ru`, `ja`, `zh`, `it` or `ko`; empty follows the Windows display language | empty |
| `apple.notifications` | `apple`, `background` (only while Apple Music is not in front) or `never` | `apple` |
| `apple.close` | `quit`, or `tray` to keep playing behind a notification-area icon | `quit` |
| `apple.network` | `ipv4-first` tries a server's IPv4 address before its IPv6 one (songs stuck at 0:00 where IPv6 does not answer), `system` keeps Windows' order; applies at the next start | `ipv4-first` |
| `apple.proxy` | `no-autodetect` makes Windows' automatic proxy detection (WPAD, "Automatically detect settings") answer "nothing found" at once to Apple's network library, which otherwise waits for the network before it loads a page or a song; a proxy set by hand or by a script address still applies. `system` leaves Windows' setting. Turn it on only when the freeze log shows long proxy detections and no proxy is needed (a company network that relies on WPAD would lose its proxy). Applies at the next start | `system` |
| `apple.fullscreen` | Where Apple's full-screen player opens: `window`, `replace`, `work-area` or `screen` | `window` |
| `apple.playerControls` | Shuffle and repeat around the full-screen player's buttons: `shown` or `hidden` | `shown` |
| `apple.artwork` | The full-screen player's cover: `sharp` shows a sharper copy where Apple Music stretches a small one, `apple` leaves Apple's | `sharp` |
| `apple.marquee` | A text cut with "…": `hover` scrolls it while the pointer is on it, `off` leaves it cut | `hover` |
| `apple.sidebar` | Apple Music's sidebar: `open` leaves it as Apple Music shows it, `hover` folds it to its icons and opens it over the pages while the pointer is on it; the pin button left of "···" in the open sidebar's header pins it open for the session (it then shows an unpin icon, and a second click, or the hamburger, folds it again; the hamburger otherwise keeps its own role), beside the pages in a window wider than 1324 px and over them in a narrower one, and the Library and Playlists groups fold with it | `open` |
| `apple.watchdog` | `on` writes to `logs\AppleMusic.log` each freeze of Apple Music's window of 0.3 s or more, with what Ampify was doing, and a summary each minute. It also writes, in `AppleMusic.log` and `AMPLibraryAgent.log`, each name lookup and automatic proxy detection that took 0.3 s or more (20 lines at most per process, "on the interface thread" when the window waited for it), and the private memory of each process, once at start and again at each new step of 512 MB. `off` writes nothing. `ampify doctor` shows its last lines and `ampify report` keeps them all in `freezes.txt`. Applies at the next start of Apple Music | `on` |
| `apple.orphanAgent` | `on` ends a library agent left running by a killed Apple Music, so the library opens again (error 9039): when Apple Music is started from Ampify's launcher, after giving the agent 10 s to leave, and also 10 s after Apple Music is closed where the background launcher runs (Windows refused to start the launcher with Apple Music). Never while Apple Music runs; `off` leaves it. Applies the next time Apple Music starts | `on` |
| `apple.playbar` | Where the playback bar goes, for every theme and with none: `theme` leaves it where the theme plans it (its manifest's `playbar`, else at the top), `top` keeps Apple Music's place, `bottom` puts it along the bottom of the window, `centred` along the bottom with the buttons centred and the progress line under them. A theme that writes `@setting bar-position` lays the bar out itself; under any other, and with no theme, Ampify applies its own bottom bar. Replaces the Bottom playback bar snippet and the themes' own `bar-position` settings of 1.2, which Ampify moves here once | `theme` |
| `apple.lyricsBefore` | In the full-screen player, how many lyrics lines show above the line being sung: `all` (Apple's own), `0`, `1`, `2`, `3`, `4` or `5`. The lines faded out take no click, and the lines shown stay centred on the sung one | `all` |
| `apple.lyricsAfter` | The same below the line being sung: `all`, `0`, `1`, `2`, `3`, `4` or `5` | `all` |
| `apple.topbar` | `search` puts Apple Music's own search field, centred, and a back arrow in the empty strip at the top of the window, next to the window buttons, and hides the sidebar's field; `off` keeps Apple Music's layout. The strip shows only while the playback bar is at the bottom (a theme with a bottom bar) and the window is at least 612 pixels wide; otherwise Apple's layout comes back until it does. No forward arrow: Apple Music keeps a back history only | `off` |
| `ampify.entry` | Where the entry that opens the Marketplace stands: `sidebar`, or `header` next to Apple's "···" at the top of the open sidebar | `sidebar` |
| `catalog.url` | `https://...` or `file://...` | the public catalog |
| `app_launch_flags` | Arguments added when `ampify launch` starts Apple Music | empty |
| `check_kit_update` | `1`: some commands say when a new Ampify exists (checked once a day, nothing downloaded) | `0` |
| `kit_update` | New Ampify releases: `auto` installs them while Apple Music is closed, `ask` offers them in a notification (a click installs; left alone, it comes back a week later), `off` never looks | `auto` |
| `expose_api` | `0`: no JavaScript element starts | `1` |
| `discord.enabled` | `1`: elements with `ipc.discord` can show a Discord presence | `0` |
| `tray.enabled` | `1`: an Ampify icon in the notification area | `0` |
| `remote.enabled`, `remote.port`, `remote.token` | The local remote on `127.0.0.1` ([API](extensions/api.md#57-local-services-preview)); the port goes from 1024 to 65535 | `0`, `8974`, created when turned on |
| `midi.enabled`, `midi.device`, `midi.map` | A MIDI controller, e.g. `note:60=next;note:62=previous;cc:7=gain` | `0`, `0`, empty |
| `audio.maxGainDb` | Highest gain an extension may add, 0 to 12 dB | `6` |
| `player.apple` | `0`: read the player from Windows' media session only | `1` |
| `player.appleCommands` | `1`: send play, pause, next and previous to Apple's own player | `0` |

## Files

```
%USERPROFILE%\Ampify\
  settings.txt          settings, one key = value per line
  extensions\<id>\      apps\<id>\      themes\<id>\      snippets\<id>\
  storage\<id>.json     data of Ampify.storage
  output\<id>\          files written by Ampify.fs.writeText
  active.txt            what must load
  reload.txt            requests to a running Apple Music: start <id>, stop <id>, restart <id>, rescan
  versions.txt          extra tested Apple Music versions (ampify versions update)
  backups\  profiles\  reports\  logs\  cache\  downloads\  updates\
```

A folder whose name starts with a dot is ignored. An element's folder name is its id.

`ampify` changes a running Apple Music without a restart by appending a line to `reload.txt`, which the kernel reads
every 500 ms. An app is written `app:<id>`. Themes and snippets do not need it: the theme engine follows its own files.

Downloads use `curl.exe` (HTTPS only) and archives use `tar.exe`, both part of Windows 10 1803 and later.
