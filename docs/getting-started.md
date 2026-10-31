# Getting started

Ampify adds themes, extensions and apps to Apple Music for Windows. It never changes Apple Music's own files. You start
Apple Music through a separate shortcut, and removing Ampify puts everything back as it was.

## What you need

- Windows 11 (the version Ampify is tested on) with Apple Music for Windows installed from the Microsoft Store.
- An Apple Music account, signed in. Ampify does not ask for your Apple ID or password.
- A tested Apple Music version. If your version is new to Ampify, Apple Music opens normally and Ampify stays inactive
  (see [Safe mode and unknown versions](#safe-mode-and-unknown-versions)).

## Install

1. Download the zip of the latest release and extract it. Right-click the zip, then "Extract all". Always work from the
   extracted folder, never from inside the zip.
2. Close Apple Music.
3. Double-click `ampify_setup.exe` and answer Yes.
4. If Windows shows "Windows protected your PC", click "More info", then "Run anyway". The binaries are not code-signed
   yet.

The installer copies four files to `%LOCALAPPDATA%\Programs\Ampify` (`ampify_launcher.exe`, `ampify_kernel.dll`,
`ampify.exe` and the uninstaller). It adds a Start menu shortcut named "Apple Music with Ampify" and an entry named
"Ampify for Apple Music" in Settings > Apps > Installed apps.

## Open Apple Music with Ampify

Open the Start menu, search for "Apple Music with Ampify" and start it. Apple Music opens as usual, maybe one or two
seconds slower.

The normal Apple Music icon still starts Apple Music with no changes at all. Use it any time you want the original app.

You will see these additions:

- An "Ampify" entry at the bottom of the sidebar, above your account. It opens the Marketplace.
- A card called "Ampify" at the bottom of Settings > Playback. It lists your extensions with one switch each, and a
  button to open the Marketplace.

## Pick a theme in the Marketplace

1. Click "Ampify" at the bottom of the sidebar. The Marketplace page opens.
2. Under "Themes", click "Use this theme" below the theme you want, for example "Glass" or "Artwork".
3. Pick a color scheme if the theme has several, for example Night or Day.
4. The change shows right away, without restarting Apple Music. "Turn off" on the theme in use, or "Restore
   Apple Music's look" at the top, goes back to Apple Music's own look. "Uninstall" deletes a theme you do not use.

To go back, click "Restore Apple Music's look". The original colors return. "Back" closes the page.

The example themes that ship with Ampify:

| Theme | What it does |
|---|---|
| `greenroom` | The playback bar along the bottom, the song on the left, rounded dark panels, a green accent |
| `midnight` | Night-blue colors |
| `glass` | Gradient background, frosted sidebar and playback bar, rounded content card, buttons that grow on hover |
| `artwork` | The artwork of the playing song, blurred behind floating glass panels |
| `large-text` | Larger text |
| `rounded-buttons` | Rounded buttons |
| `hide-chrome` | Hides the buttons and banners most people never use |

`square-corners` is a snippet: a small style that you switch on or off on its own, on top of any theme.

The Marketplace reads a public catalog. Installing from it needs the catalog address to be reachable (see
[extensions/catalog.md](extensions/catalog.md)).

## Switch extensions on and off

In Settings > Playback, the "Ampify" card has one switch per extension. The change takes effect at once. Extensions you
install from the catalog start switched off. You see the permissions an extension asks for before you switch it on.

Now Playing ships with Ampify too. Its button sits at the right of the playback bar, beside lyrics and Up Next. It
shows the song over the whole window, with its lyrics in time with the song: Apple Music's own once its lyrics panel
has been open for that song, otherwise those of LRCLIB, a free public library of lyrics. Escape or the Close button at
the top left closes it.

The Atmos leveler is an extension that ships with Ampify. It brings Dolby Atmos songs to the loudness of stereo songs
without moving Apple Music's volume slider. It is off until you switch it on. In a terminal:

```
ampify extension enable atmos-leveler
```

## Use the command line (optional)

`ampify.exe` sits in the install folder. Open a terminal there, or type its full path:

```
%LOCALAPPDATA%\Programs\Ampify\ampify.exe path
```

Everything the Marketplace does, `ampify` does too. See [cli.md](cli.md).

## Safe mode and unknown versions

- If Apple Music crashes or freezes at start twice in a row with Ampify, Ampify turns itself off for the next start.
  Apple Music then opens with no changes. After Apple Music runs for 20 seconds, Ampify tries again at the following
  start.
- Ampify only loads on Apple Music versions it has been tested with, and only if the player library still exports the
  functions it needs. After an Apple Music update, Ampify stays inactive until the new version is tested.
  `ampify versions` shows the accepted versions and `ampify versions update` fetches the list published with the
  catalog.

If something looks wrong, run `ampify doctor`. It reports the state of Ampify in one list. The logs are in
`%USERPROFILE%\Ampify\logs\`.

## Uninstall

1. Close Apple Music.
2. Open Settings > Apps > Installed apps, find "Ampify for Apple Music", click the three dots, then Uninstall.
3. Answer Yes. When the uninstaller asks whether to keep your settings, logs and reports, answer Yes to keep them in
   `%USERPROFILE%\Ampify\`, or No to delete them.

Start Apple Music from its usual icon. It opens and plays as before, with no panel and no "Ampify" card. If the
uninstaller says Apple Music is open, close it, wait 10 seconds and try again.

If an antivirus blocks a file, note the name of the threat and allow nothing until you know what it is.
