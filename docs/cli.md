# The `ampify` command line

`ampify` manages Ampify from a terminal: themes, extensions, the Marketplace, settings, and tools for authors. The
installer puts it in your Path. Everything it changes lives in `%USERPROFILE%\Ampify`; it never writes into Apple
Music's package.

```
ampify theme set glass --scheme Night    use a theme
ampify install sleep-timer               install from the catalog (shows the permissions, asks first)
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
| `ampify theme import <folder\|zip\|address> [id]` | Brings in a Spicetify theme ([details](themes/spicetify.md)) |
| `ampify new <extension\|theme\|snippet\|app> <id>` | Writes a working starter in the current folder |
| `ampify validate <folder>` | Checks an element's manifest and files with the catalog's rules (or a `catalog.json`) |

## Marketplace and updates

| Command | What it does |
|---|---|
| `ampify market` | Lists the catalog and what is installed (`--json` for the Marketplace app) |
| `ampify install <id> [--yes]` | Downloads, checks the SHA-256, validates and installs. The element starts off ([what is checked](extensions/catalog.md#what-install-checks)) |
| `ampify update <id> [--yes]` | Updates an element when the catalog has a newer version |
| `ampify remove <id>` | Removes an element, after a backup |
| `ampify update` | Downloads the latest Ampify release and checks it. `--check` only says whether one exists, `--apply` installs it (Apple Music closed) |
| `ampify install-kit <version> [--apply]` | Installs a given Ampify release, even an older one |
| `ampify versions [list]` | The Apple Music versions Ampify accepts, and the installed one |
| `ampify versions update` | Downloads the list of tested versions published with the catalog into `versions.txt` |

## Applying

| Command | What it does |
|---|---|
| `ampify apply` | Writes what must load (`active.txt`), tells a running Apple Music, and makes Ampify load each time Apple Music starts |
| `ampify restore` | Ampify no longer loads with Apple Music. An open Apple Music keeps it until it closes |
| `ampify restore --backup <name>` | Puts back the settings and elements of a backup |
| `ampify launch` (or `auto`) | Applies, then starts Apple Music with the kernel now |
| `ampify backup` / `ampify backup list` | Backs up the settings and elements, or lists the backups |
| `ampify watch [--interval ms]` | Applies again at each change of a theme, extension or setting |

Commands without arguments can follow each other: `ampify backup apply launch`. The chainable ones are `backup`,
`apply`, `launch`, `auto`, `restore`, `devtools`, `doctor` and `version`. The first failure stops the line.

## Authoring

| Command | What it does |
|---|---|
| `ampify dev <folder>` | Copies the element into Ampify, switches it on, and reloads it at every save |
| `ampify test <folder>` | Runs `main.js` in the same JavaScript engine with a fake player and network. A call without its permission fails the test |
| `ampify build <folder> [file.zip]` | Validates, writes a reproducible zip, and prints its SHA-256 and catalog entry |
| `ampify publish <folder> [--dry-run]` | Checks and tests the element, then opens a pull request on the [public catalog](extensions/catalog.md) with the GitHub CLI (`gh`), or prints the steps to follow in the browser |
| `ampify types [file]` | Writes `ampify.d.ts`, the types of the `Ampify` object for your editor (`--yes` to overwrite) |
| `ampify inspect <folder>` or `<kind> <id>` | Prints the manifest, permissions, state and each file with its SHA-256 |
| `ampify inspect --tree` | Prints the last interface tree written by the kernel (`logs\inspector.txt`) |
| `ampify devtools [on\|off]` | Asks the running kernel for the interface tree of the page on screen, and waits for it (up to 10 s) |

## Diagnostics and transfers

| Command | What it does |
|---|---|
| `ampify doctor` | Checks settings, elements, safe mode, logs, tools and Apple Music versions. Lines start with `[OK]`, `[WARNING]` or `[ERROR]`; exit code 1 on an error |
| `ampify logs [count] [--follow]` | The last lines (50 by default) of each log; `--follow` keeps printing |
| `ampify report [file.zip] [--tree] [--dir]` | Writes an anonymised zip to attach to an issue (see below) |
| `ampify profile list` | Lists settings profiles |
| `ampify profile save\|use\|delete <name>` | Saves, loads (after a backup) or deletes a copy of `settings.txt` in `profiles\<name>\` |
| `ampify export <file.zip>` | Exports the settings and installed elements |
| `ampify import <file.zip> [--dry-run] [--yes]` | Imports an export after validating every element. Writes nothing if one fails |

`ampify report` puts `system.txt` (versions, language, displays), `doctor.txt`, the settings, the logs and, on Windows,
recent crashes of Apple Music or Ampify in one zip under `reports\`. The user and computer names, e-mail addresses, what
was played and secret-looking values are masked first. Nothing is sent: look through the zip, then attach it.

## Options

| Option | Effect |
|---|---|
| `--kit-dir <folder>` | Ampify folder (default `%USERPROFILE%\Ampify`) |
| `--catalog <source>` | Catalog: a local file, `file://` or `https://` |
| `--dry-run` | Shows what would happen and writes nothing: `apply`, `launch`, `restore`, `import`, `publish`, `devtools`, `config --dir`. Other commands refuse it |
| `--yes`, `-y` | Answers yes to every question |
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
| `theme.parts.colors`, `.resources`, `.assets`, `.script` | `1` or `0`: turn off a part of the theme | `1` |
| `theme.live_reload` | `1` or `0`: apply the theme again when its files change | `1` |
| `theme.inspect` | `1`: the kernel writes the interface tree once, then sets `0` | `0` |
| `xaml.watcher` | `1`: a trial in which WinUI tells Ampify of each element it adds, written to the log. The themes do not use it yet. Seen once making the screen flash black on the Home page: leave it off | `0` |
| `language` | `en`, `fr`, `de`, `es`, `pt`, `ru`, `ja` or `zh`; empty follows Windows' regional format | empty |
| `apple.notifications` | `apple`, `background` (only while Apple Music is not in front) or `never` | `apple` |
| `apple.close` | `quit`, or `tray` to keep playing behind a notification-area icon | `quit` |
| `apple.fullscreen` | Where Apple's full-screen player opens: `window`, `replace`, `work-area` or `screen` | `window` |
| `catalog.url` | `https://...` or `file://...` | the public catalog |
| `app_launch_flags` | Arguments added when `ampify launch` starts Apple Music | empty |
| `check_kit_update` | `1`: some commands say when a new Ampify exists (checked once a day, nothing downloaded) | `0` |
| `expose_api` | `0`: no JavaScript element starts | `1` |
| `discord.enabled` | `1`: elements with `ipc.discord` can show a Discord presence | `0` |
| `tray.enabled` | `1`: an Ampify icon in the notification area | `0` |
| `remote.enabled`, `remote.port`, `remote.token` | The local remote on `127.0.0.1` ([API](extensions/api.md#57-local-services-preview)) | `0`, `8974`, created when turned on |
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
