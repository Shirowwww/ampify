# Getting started

Ampify adds themes, extensions and apps to Apple Music for Windows. It never changes Apple Music's own files:
uninstalling it gives you the original app back.

## What you need

- Windows 10 or 11 (x64), with Apple Music installed from the Microsoft Store and signed in. Ampify never asks for
  your Apple ID.
- A tested Apple Music version (`ampify versions` lists them). On another version, Apple Music opens normally and
  Ampify stays off (see [Safe mode and new Apple Music versions](#safe-mode-and-new-apple-music-versions)).

## Install

Close Apple Music, open PowerShell (Start menu, type "PowerShell") and paste:

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
```

The line downloads the latest release, checks its SHA-256 and runs the installer. To install from the zip instead,
download it from the [Releases](https://github.com/Shirowwww/ampify/releases) page, extract it and run
`ampify_setup.exe`. If Windows shows "Windows protected your PC", click "More info", then "Run anyway": the files are
not code-signed yet. If Apple Music is open, the installer offers to close it.

The installer puts Ampify in `%LOCALAPPDATA%\Programs\Ampify`, your themes and settings in `%USERPROFILE%\Ampify`, and
adds:

- a small background launcher, started now and at each sign-in, that loads Ampify into Apple Music when it opens
  (Task Manager > Startup apps lists it as "Ampify launcher");
- `ampify` to your Path, for any new terminal;
- "Ampify for Apple Music" in Settings > Apps > Installed apps.

## Open Apple Music

Open Apple Music from its usual icon. With Ampify inside you see:

- an **Ampify** entry at the bottom of the sidebar, which opens the Marketplace;
- an **Ampify** row in Apple Music's Settings, under Advanced, which opens Ampify's settings;
- the **Now Playing** button at the right of the playback bar, which opens Apple Music's full-screen player with
  lyrics (Escape closes it).

## Pick a theme

1. Click **Ampify** at the bottom of the sidebar. The Marketplace opens.
2. Under **Themes**, click **Use this theme** on the one you want, for example Glass or Artwork.
3. Pick a color scheme if the theme has several, for example Night or Day.

The change shows at once. **Restore Apple Music's look** goes back to the original look, and **Uninstall** deletes a
theme you do not use. The Marketplace reads a [public catalog](https://github.com/Shirowwww/ampify-catalog), so it
needs an internet connection.

## Extensions

Open Apple Music's Settings and click **Ampify**, or click **Settings** in the Marketplace. Ampify's settings page has
one switch per extension with its settings, a **Sound** section, the active theme's settings, and a few choices for
Apple Music itself (where the full-screen player opens, what closing the window does, song notifications). Changes
take effect at once.

Extensions you install start switched off, and you see the permissions an extension asks for before you switch it on.
Three ship with Ampify: Now Playing, the playback guard (restarts a song Apple Music leaves stuck, off until you
switch it on) and the Atmos leveler, under **Sound**: Dolby Atmos songs often play quieter than stereo ones, and the
leveler brings them to the same loudness so you do not reach for the volume between songs. It is off until you switch
it on.

## Languages

Ampify speaks English, French, German, Spanish, Portuguese (Brazilian), Russian, Japanese and Simplified Chinese. It
follows the language of Windows' regional format (Settings > Time & language > Language & region). Traditional
Chinese, and any text not translated yet, show in English. To force a language:

```
ampify config language de
```

Use `en`, `fr`, `de`, `es`, `pt`, `ru`, `ja` or `zh`, then restart Apple Music. `ampify config language ""` follows
Windows again.

## Turn Ampify off and on

```
ampify restore     # Apple Music starts without Ampify from now on
ampify apply       # Ampify loads with Apple Music again
```

An Apple Music already open keeps Ampify until you close it. Your themes, extensions and settings stay.

## Safe mode and new Apple Music versions

- If Apple Music crashes or freezes at start twice in a row, Ampify turns itself off for the next start. Once Apple
  Music has run for 20 seconds, Ampify tries again at the following start.
- Ampify only loads on Apple Music versions it was tested with. After an Apple Music update, it stays off until the
  new version is tested. `ampify versions update` fetches the latest list of tested versions.

## Update

Run the install line again, or `ampify update --apply` with Apple Music closed. Both keep your settings.

## Uninstall

Close Apple Music, open Settings > Apps > Installed apps, find "Ampify for Apple Music", click the three dots, then
Uninstall. The uninstaller asks whether to keep your themes, extensions and settings in `%USERPROFILE%\Ampify`.
Apple Music then opens and plays as before.

## Something is wrong

1. Run `ampify doctor`: it checks Ampify and lists each problem.
2. Run `ampify report`: it writes an anonymised zip of the state and the logs in `%USERPROFILE%\Ampify\reports`
   (the **Export a report** button of Ampify's settings page does the same). Look through it, then attach it to an
   [issue](https://github.com/Shirowwww/ampify/issues).

The logs are in `%USERPROFILE%\Ampify\logs\`. If an antivirus blocks a file, note the threat name and allow nothing
until you know what it is.
