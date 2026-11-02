# Getting started

Ampify adds themes, extensions and apps to Apple Music for Windows. It never changes Apple Music's own files:
uninstalling it, or running `ampify restore`, gives you the original app back.

## What you need

- Windows 10 or 11 with Apple Music for Windows installed from the Microsoft Store.
- An Apple Music account, signed in. Ampify does not ask for your Apple ID or password.
- A tested Apple Music version. If your version is new to Ampify, Apple Music opens normally and Ampify stays inactive
  (see [Safe mode and unknown versions](#safe-mode-and-unknown-versions)).

## Install

Close Apple Music, open PowerShell (Start menu, type "PowerShell") and paste:

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
```

The line downloads the latest release from GitHub, checks the SHA-256 of the download and runs the installer it
contains. To install from the zip instead: download it from the
[Releases](https://github.com/Shirowwww/ampify/releases) page, right-click it, "Extract all", then double-click
`ampify_setup.exe` in the extracted folder. If Windows shows "Windows protected your PC", click "More info", then
"Run anyway": the files are not code-signed yet.

The installer copies four files to `%LOCALAPPDATA%\Programs\Ampify` (`ampify_launcher.exe`, `ampify_kernel.dll`,
`ampify.exe` and the uninstaller), puts the Marketplace and Now Playing in `%USERPROFILE%\Ampify`, and adds:

- a small background launcher, started now and at each Windows sign-in, which loads Ampify into Apple Music when it
  opens (Task Manager > Startup apps lists it as "Ampify launcher");
- the install folder to your Path, so `ampify` works in any new terminal;
- "Ampify for Apple Music" in Settings > Apps > Installed apps.

## Open Apple Music

Open Apple Music from its usual icon. It opens as usual, maybe a second slower, with Ampify inside:

- an "Ampify" entry at the bottom of the sidebar, with the puzzle-piece logo, which opens the Marketplace;
- a card called "Ampify" at the bottom of Settings > Playback, with one switch per extension and a button to open the
  Marketplace;
- the Now Playing button at the right of the playback bar.

## Pick a theme in the Marketplace

1. Click "Ampify" at the bottom of the sidebar. The Marketplace page opens.
2. Under "Themes", click "Use this theme" below the one you want, for example "Glass" or "Artwork".
3. Pick a color scheme if the theme has several, for example Night or Day.

The change shows right away, without restarting Apple Music. "Turn off" on the theme in use, or "Restore Apple Music's
look" at the top, goes back to Apple Music's own look. "Uninstall" deletes a theme you do not use.

| Theme | What it does |
|---|---|
| Greenroom | The playback bar along the bottom, the song on the left, rounded dark panels, a green accent |
| Glass | Gradient background, frosted sidebar and playback bar, rounded content card, buttons that grow on hover |
| Artwork | The artwork of the playing song, blurred behind floating glass panels |
| Midnight | Night-blue colors |
| Hide chrome | Hides the buttons and banners most people never use |
| High contrast, Night blue, Light paper, Rose gold | Color palettes, each with a dark and a light scheme |

The Marketplace reads a public catalog on GitHub ([ampify-catalog](https://github.com/Shirowwww/ampify-catalog)), so
installing needs an internet connection.

## Switch extensions on and off

Open Apple Music's Settings and click "Ampify", under Advanced: it leads to the "Ampify" card, at the bottom of Settings >
Playback, with one switch per extension and the settings of each. The change takes effect at once. Extensions you
install from the Marketplace start switched off, and you see the permissions an extension asks for before you switch
it on. The extensions marked "(example)" are there to learn from; they work, but they are simple on purpose. The others
are small and commented too, so their code is worth reading.

Two extensions ship with Ampify:

- Now Playing: its button, at the right of the playback bar, opens Apple Music's own full-screen player over the
  window, with the animated artwork and the lyrics. Escape closes it.
- The Atmos leveler brings Dolby Atmos songs to the loudness of stereo songs without moving Apple Music's volume
  slider. It is off until you switch it on, in the card or in a terminal: `ampify extension enable atmos-leveler`.

## Use the command line

The installer puts `ampify` in your Path. Open a new terminal and type:

```
ampify help
ampify theme list
ampify doctor
```

Everything the Marketplace does, `ampify` does too. See [cli.md](cli.md).

## Turn Ampify off and on

```
ampify restore     # Apple Music starts without Ampify from now on
ampify apply       # Ampify loads with Apple Music again
```

`ampify restore` stops the background launcher and removes it from Windows sign-in. An Apple Music already open keeps
Ampify until you close it. Nothing else is removed: your themes, extensions and settings stay for when you turn it
back on.

## Safe mode and unknown versions

- If Apple Music crashes or freezes at start twice in a row with Ampify, Ampify turns itself off for the next start.
  Apple Music then opens with no changes. After Apple Music runs for 20 seconds, Ampify tries again at the following
  start.
- Ampify only loads on Apple Music versions it has been tested with, and only if the player library still exports the
  functions it needs. After an Apple Music update, Ampify stays inactive until the new version is tested.
  `ampify versions` shows the accepted versions and `ampify versions update` fetches the list published with the
  catalog.

If something looks wrong, run `ampify doctor`. It reports the state of Ampify in one list. The logs are in
`%USERPROFILE%\Ampify\logs\`: `background.log` for the launcher, `AppleMusic.log` and `AMPLibraryAgent.log` for the
kernel.

## Update

Run the install line again, or `ampify update --apply` with Apple Music closed. Both keep your settings.

## Uninstall

1. Close Apple Music.
2. Open Settings > Apps > Installed apps, find "Ampify for Apple Music", click the three dots, then Uninstall.
3. Answer Yes. When the uninstaller asks whether to keep your settings, logs and reports, answer Yes to keep them in
   `%USERPROFILE%\Ampify\`, or No to delete them.

The uninstaller stops the background launcher and removes it from sign-in and from your Path. Apple Music then opens
and plays as before. If the uninstaller says Apple Music is open, close it, wait 10 seconds and try again.

If an antivirus blocks a file, note the name of the threat and allow nothing until you know what it is.
