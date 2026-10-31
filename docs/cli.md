# The `ampify` command line

`ampify` is Ampify's command line tool. For Apple Music it does what `spicetify` does for Spotify: it starts the app with
the kernel, manages extensions, themes and snippets, installs from the catalog, and can go back. It never changes the
Apple Music package. Everything happens in `%USERPROFILE%\Ampify`.

The code is in [cli/](../cli/). The line-by-line comparison with Spicetify is in
[spicetify-parity.md](spicetify-parity.md).

## Get started

`ampify.exe` sits next to `ampify_launcher.exe` (in `%LOCALAPPDATA%\Programs\Ampify` after the install). Open a terminal
in that folder, or type `ampify` with its full path.

```
ampify path                          where the files are
ampify market                        what the catalog offers
ampify install demo                  installs (asks for confirmation, shows the permissions)
ampify extension enable demo         switches it on
ampify launch                        applies and starts Apple Music with the kernel
```

## Commands

The "Spicetify" column gives the equivalent.

### Launch and settings

| Command | Purpose | Spicetify |
|---|---|---|
| `ampify launch` (or `auto`) | Writes what must load, then starts `ampify_launcher.exe`. On Linux, `--dry-run` prints the command | `auto` |
| `ampify restore` | Starts Apple Music with no kernel. Nothing has to be put back in the package | `restore` |
| `ampify restore --backup <name>` | Puts back the settings and elements of a backup | `restore` (variant) |
| `ampify apply` | Writes `active.txt` and `reload.txt`, prints what loads and what does not | `apply` |
| `ampify watch [--interval ms]` | Applies at each change of a theme, extension or setting | `watch` |
| `ampify config` | Prints all settings | `config` |
| `ampify config <key>` | Prints one setting | `config <key>` |
| `ampify config <key> <value>` | Writes a setting, value checked | `config <key> <value>` |
| `ampify config extensions +id` / `-id` | Switches an element on or off (also `apps`, `snippets`) | `config extensions +id` |
| `ampify path` | Prints the Ampify folders, the Apple Music folder and its preferences | `path` |
| `ampify path <name>` | One path on one line: `kit`, `settings`, `extensions`, `themes`, `apps`, `snippets`, `logs`, `backups`, `app` (Apple Music package), `prefs` (Apple Music preferences). `app` and `prefs` print "not found" outside Windows | `path` |
| `ampify config --dir` | Opens the Ampify folder in Explorer (`--dry-run` only prints the folder) | `config-dir` |
| `ampify backup` / `ampify backup list` | Backs up the settings and elements | `backup` |
| `ampify devtools [on\|off]` | Asks the kernel to write the interface tree to `logs\inspector.txt` (setting `theme.inspect`) | `enable-devtools` |
| `ampify versions [list]` | Apple Music versions the kernel accepts, and the installed one | none |
| `ampify versions update` | Downloads the list of tested versions published with the catalog and writes `versions.txt` | restore after an update |
| `ampify install-kit <version>` | Installs a specific Ampify version, even an older one (SHA-256 checked) | old versions |
| `ampify version`, `ampify help` | | `--version`, `help` |
| `ampify backup apply launch` | Several commands in a row, see "Chaining commands" | `spicetify backup apply` |

### Elements: extensions, snippets, apps, themes

| Command | Purpose |
|---|---|
| `ampify list` | All installed elements, their kind, version and state |
| `ampify extension list` / `enable <id>` / `disable <id>` | Extensions. The Atmos leveler (`atmos-leveler`) is built in and switches on the same way. If Apple Music runs with the kernel, the extension starts or stops at once (see "Immediate effect") |
| `ampify snippet enable <id>` / `disable <id>` | Snippets (small style fragments) |
| `ampify app enable <id>` / `disable <id>` | Apps (interface pages). The kernel loads `apps\<id>` and starts the app at once, like an extension |
| `ampify theme list` | Installed themes |
| `ampify theme set <id> [--scheme <name>]` | Picks the theme and its scheme. Without `--scheme`, another theme starts on its own default scheme; the same theme set again keeps its scheme, and `auto` stays for every theme |
| `ampify theme import <folder\|zip\|address> [id]` | Reuses a Spicetify theme: `color.ini` as it is, Spotify's `user.css` kept aside (see [themes/spicetify.md](themes/spicetify.md)) |
| `ampify theme check <folder>` | Reads a theme like the kernel does and reports each refused rule with its line |
| `ampify theme clear` | No theme, and no scheme left for the next one |
| `ampify new <kind> <id>` | Creates a starter element (extension, theme, snippet or app) in the current folder |
| `ampify validate <folder>` | Checks the manifest and files of an element, with the same rules as `tools/validate.py` |
| `ampify validate <catalog.json>` | Checks a catalog |

### Marketplace

| Command | Purpose |
|---|---|
| `ampify market` (or `catalog`) | Lists the catalog and what is already installed |
| `ampify install <id> [--yes]` | Downloads, checks the SHA-256, validates, installs. Off by default. A running kernel sees the new folder at once |
| `ampify update <id> [--yes]` | Updates an element from the catalog, if the version is newer. A running kernel restarts it with the new version |
| `ampify remove <id>` | Removes an element. A backup is made first. A running kernel stops it |
| `ampify update` | Checks the latest Ampify release, downloads it, checks the SHA-256 |
| `ampify update --check` | Only says whether a newer version exists |
| `ampify update --apply` | Installs the downloaded version. Apple Music must be closed |

`ampify inspect` shows the last interface tree written by the kernel. A live tree of the interface (`Ampify.React`) is not
available yet.

## Options

| Option | Effect |
|---|---|
| `--kit-dir <folder>` | Ampify folder. Default `%USERPROFILE%\Ampify` |
| `--catalog <source>` | Catalog: a local file, `file://` or `https://`. Default is the repository's catalog |
| `--dry-run` | Prints the command that would run, without running it |
| `--yes`, `-y` | Answers yes to questions (install, update) |
| `--check` | With `update`: only checks |
| `--apply` | With `update`: installs the downloaded update |
| `--release-url <address>` | With `update`: another release address (tests, mirror) |
| `--install-dir <folder>` | With `update --apply`: folder to copy the files to. Default is the folder of `ampify.exe` |
| `--scheme <name>` | With `theme set`: color scheme |
| `--backup <name>` | With `restore`: backup to put back |
| `--interval <ms>`, `--max-cycles <n>` | With `watch`: watch period, and number of passes before stopping |
| `--tree` | With `inspect`: prints the last interface tree (`logs\inspector.txt`) |
| `--dir` | With `config`: opens the Ampify folder |
| `--` | Everything after it is an argument, even if it starts with `--` |

Exit codes: `0` success, `1` failure, `2` wrong usage. `apply` returns `1` if a switched-on element cannot load. Error
messages go to the error output, prefixed with `ampify:`.

## Ampify files

```
%USERPROFILE%\Ampify\
  settings.txt              settings, one key = value per line (format of core/settings.*)
  extensions\<id>\          manifest.json, main.js, ...
  themes\<id>\              manifest.json, colors.ini, resources.xaml
  apps\<id>\                manifest.json, main.js, icon.svg
  snippets\<id>\            manifest.json, resources.xaml
  active.txt                what must load: "extension demo 1.0.0", "theme night 2.0.0 scheme=Dark"
  reload.txt                requests to the running kernel: "start <id>", "stop <id>", "restart <id>", "rescan" (see "Immediate effect")
  backups\<yyyymmdd-hhmmss>\  copy of settings.txt and the four element folders
  versions.txt              Apple Music versions tested in addition to the kernel's (written by ampify versions update)
  logs\                     logs of the kernel and the launcher, and inspector.txt (interface tree, see devtools)
  reports\                  test kit reports
  downloads\, updates\, cache\  temporary files, the last catalog read, and cache\kit-update.txt (check_kit_update)
```

Settings known to `ampify config`:

| Key | Value | Default |
|---|---|---|
| `extensions.<id>.enabled`, `apps.<id>.enabled`, `snippets.<id>.enabled` | `1` or `0` | `0` (a community element is off) |
| `theme.active` | Id of a theme, empty for none | empty |
| `theme.scheme` | Name of a section of `colors.ini`, `auto` to follow Windows | theme default |
| `theme.schedule` | `HH:MM=Scheme` list, see [themes/format.md](themes/format.md) | empty |
| `theme.parts.colors`, `theme.parts.resources`, `theme.parts.assets`, `theme.parts.script` | `1` or `0`: colors, XAML resources, images and fonts, theme script | `1` |
| `theme.live_reload` | `1` or `0`: apply the theme again when its files change | `1` |
| `theme.inspect` | `1` or `0`: the kernel writes the interface tree on its next pass, then sets `0` (see `ampify devtools`) | `0` |
| `catalog.url` | `https://...` or `file://...` | the repository's catalog |
| `app_path` | Full path of the Apple Music folder, empty to find it automatically | empty (found by Windows) |
| `prefs_path` | Full path of the Apple Music preferences file, empty for the usual place | empty (`%LOCALAPPDATA%\Packages\AppleInc.AppleMusicWin_nzyj5cx40ttqa\LocalCache\Roaming\Apple Computer\Preferences\AMPLibraryAgent.exe.plist`) |
| `app_launch_flags` | Arguments added when Apple Music starts, separated by spaces (quotes to keep a space) | empty |
| `check_kit_update` | `1` or `0`: `ampify` reports a new Ampify version | `0` |
| `telemetry.block` | `1` or `0`: block Apple Music's data sends. **Not active yet**: the key is accepted, the kernel does not read it | `0` |
| `expose_api` | `1` or `0`: at `0`, no JavaScript element (extension, app, theme script) starts | `1` |
| `discord.enabled` | `1` or `0`: elements with `ipc.discord` can show Discord presence | `0` |
| `tray.enabled` | `1` or `0`: Ampify icon in the notification area, with its menu | `0` |
| `remote.enabled` | `1` or `0`: local remote on `127.0.0.1` (token required, created when set to `1`) | `0` |
| `remote.port` | Port of the remote, 1024 to 65535 | `8974` |
| `remote.token` | Token of the remote, 32 to 128 characters `A-Z a-z 0-9 - _`. Empty: a new one is created | created by Ampify |
| `midi.enabled` | `1` or `0`: read a MIDI controller | `0` |
| `midi.device` | Number of the MIDI device, 0 to 255 | `0` |
| `midi.map` | Keys linked to Ampify commands, e.g. `note:60=next;note:62=previous;cc:7=gain` | empty |

The services (`discord`, `tray`, `remote`, `midi`) are all off by default. They are described in
[extensions/api.md](extensions/api.md), section 5.7. The kernel reads them when Apple Music starts: restart the app after
a change.

These keys match Spicetify's: `app_path` to `spotify_path`, `prefs_path` to `prefs_path`, `app_launch_flags` to
`spotify_launch_flags`, `check_kit_update` to `check_spicetify_update`, `telemetry.block` to `disable_sentry` and
`disable_ui_logging`, `expose_api` to `expose_apis`.

`app_launch_flags` is read by the launcher, which passes these arguments to Apple Music when it starts it (Apple Music may
ignore them). `ampify launch --dry-run` prints the arguments that will be sent. For a value that starts with `--`, type it
directly: `ampify config app_launch_flags --my-option`, or in quotes when there are several:
`ampify config app_launch_flags "--a --b"`. An invalid value (unclosed quote, control character) is refused.

An element is in `extensions\<id>` only if its folder name is its id. Folders that start with a dot are work in progress
and are ignored.

## Immediate effect (`reload.txt`)

When Apple Music already runs with the kernel, it does not need a restart for an extension or an app. `ampify` appends a
line to `reload.txt`. The kernel reads new lines every 500 ms, re-reads `settings.txt`, then makes the request in its
JavaScript engine:

| Line | Written by | What the kernel does |
|---|---|---|
| `start <id>` | `extension enable`, `app enable`, `config extensions +id` | Loads the element if it does not know it yet (new folder), then starts it if it is switched on |
| `stop <id>` | `extension disable`, `app disable`, `config extensions -id`, `remove` | Stops the element |
| `restart <id>` | `dev` (at each copy), `update` | Stops the element, re-reads its folder (manifest and code), then restarts it if it is switched on |
| `rescan` | `install`, `remove` | Looks for new folders (starts those that are switched on) and forgets those that disappeared |

An app is written `app:<id>` (for example `start app:lyrics`), because an app and an extension can share an id (they are
then linked, see `Ampify.link` in [extensions/api.md](extensions/api.md)). Lines are appended at the end of the file: a
request is never erased by the next one. Past 64 KB, the file starts again from zero. The kernel does not replay lines
already present when Apple Music starts: at that moment it loads everything from the folders.

Themes and snippets do not use these lines: the theme engine follows its own files.

If Apple Music is not running, these lines do nothing and do no harm: the next `ampify launch` loads everything.

## Chaining commands

Like `spicetify backup apply`, several commands without arguments can follow each other on one line:

```
ampify backup apply launch           backs up, applies, then starts Apple Music
ampify apply devtools launch         same, and asks for the interface tree
```

The commands that chain: `backup`, `apply`, `launch` (or `auto`), `restore`, `devtools`, `doctor`, `version`. They run in
order. The first one that fails stops the line: the next ones do not run, and the exit code is its own. The options
(`--dry-run`, `--backup <name>`, `--kit-dir`) apply to the whole line. As soon as a word is not one of these commands, the
line is a single command: `ampify backup list` lists the backups, `ampify devtools off` turns the inspector off.

## Apple Music versions and broken updates

The kernel activates only on a tested Apple Music version, and only if `AVFoundationCF.dll` still exports the functions
Ampify needs. Otherwise Apple Music starts with no mods: nothing is guessed. After an Apple Music update, Ampify therefore
stays inactive until the new version is tested.

Versions tested after a release are published in `catalog/versions.json`, next to the catalog. For each version it lists
the verified exports. `catalog.json` pins the address and the SHA-256 of this file.

- `ampify versions` prints the accepted versions (the kernel's and those of `versions.txt`), the required exports, and says
  whether the installed version is tested.
- `ampify versions update` reads the catalog (same source as `ampify market`), downloads `versions.json`, checks its
  SHA-256, then writes `versions.txt` in the Ampify folder. A version whose verified exports do not cover the ones this
  build needs is ignored.
- At the next launch, the launcher and the kernel accept these versions in addition to their own. The export check is
  still done at each launch. A damaged `versions.txt` is ignored as a whole.

## Go back to an Ampify version

`ampify install-kit <version>` (for example `ampify install-kit 0.1.0`) works like `ampify update`, but for the release
`v<version>` of the repository, even if older than the installed one. It downloads the archive, checks its SHA-256 with
`SHA256SUMS.txt`, extracts it and checks its content. Without `--apply`, it stops there and says which command to run once
Apple Music is closed: `ampify install-kit <version> --apply`. `--check` only says whether this version exists. Settings
and elements are not touched.

## New Ampify version

With `ampify config check_kit_update 1`, the commands `launch`, `auto`, `apply`, `list`, `doctor` and `market` print a line
when a newer Ampify version exists. The check reads the latest GitHub release at most once a day (the reply is kept in
`cache\kit-update.txt`), gives up after 10 seconds, and downloads nothing: `ampify update` does the install. Commands whose
output a script reads (`path`, `config`) never print it. It is off by default.

## Install: what is checked, in order

See [extensions/catalog.md](extensions/catalog.md), "What install checks". In short: valid id in the catalog, `https://`
address, permissions shown, SHA-256 equal to the catalog's, safe file list, no symbolic link, manifest matching the
catalog, and only then the folder is put in place.

## Update Ampify

`ampify update` reads the latest GitHub release (`releases/latest`). It needs two files in that release:

- `ampify-<version>-windows-x64.zip`, with `ampify.exe`, `ampify_launcher.exe` and `ampify_kernel.dll` at the root;
- `SHA256SUMS.txt`, in the format of `sha256sum` (`<hash>  <name>` per line).

The zip must match its hash. Both files come from the same release, so the check protects against a corrupted or truncated
download, not against a release published with wrong files. Code signing is an open question. `ampify update --apply` does
not close Apple Music: it refuses to copy while the app runs. Old files are renamed to `*.old` before being replaced,
because Windows does not replace a running file.

## Tools used on Windows

`ampify` ships neither a zip library nor an HTTP client. It calls two tools that come with Windows 10 (version 1803) and
Windows 11:

- `tar.exe` (bsdtar) to read and extract zips;
- `curl.exe` for downloads, with `--proto =https` (no fallback to HTTP, even on a redirect).

Programs are started without going through `cmd.exe`. An address or a path that contains quotes or `&` therefore cannot run
anything else.

## Authoring, diagnosis and transfers

`build <folder> [file.zip]` validates the element, writes a zip, reads it back with the same tool as `install`, then prints
the SHA-256 and the catalog entry to complete (the address is a value to replace). The zip is not compressed and its dates
are fixed: the same folder always gives the same file. It refuses to overwrite an existing file. Files and folders whose
name starts with a dot are not included. **`ampify build` is the reference tool to publish**: an author only needs
`ampify`, with no Python. `tools/pack.py` stays for the repository's CI. Its (compressed) zip has a different SHA-256, so
publish with one of the two tools only.

`inspect <folder>` or `inspect <kind> <id>` prints the manifest, the permissions, the activation state (for an installed
element), and each file with its size and SHA-256. `inspect --tree` prints the last interface tree written by the kernel
(`logs\inspector.txt`). To ask for a new one, run `ampify devtools`, then start Apple Music with `ampify launch`. The kernel
writes the tree on its next pass and sets `theme.inspect` back to `0`. This needs a kernel built with the XAML theme engine
(the MSVC build). Without it, no file is written.

`dev <folder>` validates the element, backs up Ampify if the element is already in it, switches it on, then copies it to
Ampify at each change of the folder. Each copy writes `active.txt` and a line `restart <id>` in `reload.txt` (`restart
app:<id>` for an app). The kernel re-reads this file every 500 ms, re-reads `settings.txt`, and restarts the element in its
JavaScript engine: the folder is re-read from Ampify (manifest, `main.js` and the modules it imports), and the element
restarts with its storage. An invalid change is reported, and the last version that worked stays in Ampify. The author's
folder stays the source: `install`, `update` or `remove` replace the copy in Ampify. A change to `manifest.json`
(permissions, processes, `type`) counts from the element's restart: the kernel re-reads the manifest at each `restart`.

`test <folder>` loads `main.js` in the same JavaScript engine (QuickJS) as the kernel, with a fake host: settings and
storage stay in memory, and the player and the network answer a fixed value. It prints the element's log, each call made
to the host, then the result. An API used without the declared permission fails the test (code 1). Nothing is written to
Ampify. A passing test does not say that the element works in Apple Music: check that on a PC.

`types [file]` writes `ampify.d.ts` (by default in the current folder): the types of the `Ampify` object of section 5 of
[extensions/api.md](extensions/api.md). The file is written by hand, in `cli/sdk_types.cpp`. The test `tools/test_cli.py`
checks that every member of section 5 is in it. It is not generated from the text. It refuses to overwrite an existing
`ampify.d.ts`, except with `--yes`.

`doctor` writes a report on Ampify. Each line starts with `[OK]`, `[WARNING]` or `[ERROR]`. It checks the settings, the
installed elements, the switched-on elements that will not load, `active.txt`, safe mode, leftovers of an interrupted
install, the last lines of the logs, the presence of `tar` (or `tar.exe`) and `curl`, and the tested Apple Music versions.
It exits with code 1 only if there is an error.

`logs [count] [--follow]` prints the last lines (50 by default) of each log in the `logs\` folder. With `--follow` it keeps
printing new lines. `--max-cycles <n>` stops it after n passes.

`profile list`, `profile save <name>`, `profile use <name>` and `profile delete <name>` manage settings profiles: a copy of
`settings.txt` stored in `profiles\<name>\`. A profile does not contain the elements. `profile use` backs up the current
state before replacing it: `restore --backup <name>` puts it back.

`export <file.zip>` writes the settings and the installed elements (`extensions`, `themes`, `apps`, `snippets`). It
includes neither backups, nor logs, nor profiles, nor cache, nor any file of the app. The first entry of the archive is
`Ampify-EXPORT`.

`import <file.zip> [--dry-run] [--yes]` refuses an archive without `Ampify-EXPORT`, and any entry outside the expected
folders. The archive is unpacked into a temporary folder, then each element is validated: if one fails, nothing is
written. Otherwise the command prints the content and asks for confirmation (except with `--yes`). It then backs up
Ampify, replaces the elements and `settings.txt`, and leaves `apply` to load them. `--dry-run` prints the content and
writes nothing.

Still missing compared with Spicetify: the live interface tree in `inspect`, and the kernel reading `telemetry.block`.
The kernel side of the immediate effect (reading `reload.txt`, start, stop, reload, rescanning folders) is tested on Linux
with a fake host and compiled for Windows. The CLI tests check what is written to `reload.txt`.

## Tests

```bash
cmake -S . -B build && cmake --build build
./build/ampify_tests                                   # unit tests: JSON, SHA-256, manifests, catalog, archives, zip, backups, versions, arguments
AMPIFY_BIN=$PWD/build/ampify python3 -m unittest discover -s tools -v   # validator, pack.py, end-to-end tests of the binary (including ampify test)
node --test tools/test_examples.mjs                    # example extensions, against a fake Ampify
```

Under Wine, `AMPIFY_WINE=1 AMPIFY_BIN=build-win/ampify.exe` runs the end-to-end tests that do not need `tar.exe` or
`curl.exe`, which Wine does not have. A real install is tested on Windows 10 or 11 (the Windows bench), and on a PC for
Apple Music itself.

Not tested here: everything that touches the real Apple Music (launch, injection, reaction of the kernel). See
[development/testing-on-a-pc.md](development/testing-on-a-pc.md).
