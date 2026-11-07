# Getting started

Ampify adds themes, extensions and apps to Apple Music for Windows. It never changes Apple Music's own files:
uninstalling it gives you the original app back.

## What you need

- Windows 10 or 11 (x64), with Apple Music installed from the Microsoft Store and signed in. Ampify never asks for
  your Apple ID.
- A tested Apple Music version (`ampify versions` lists them). On another version, Apple Music opens normally and
  Ampify stays off (see [Safe mode and new Apple Music versions](#safe-mode-and-new-apple-music-versions)).

## Install

Close Apple Music first, also from the notification area next to the clock, where it can stay hidden.

**From the zip.** Download `ampify-<version>-windows-x64.zip` from the [Releases](https://github.com/Shirowwww/ampify/releases)
page, extract the whole zip (right-click, Extract All) and double-click `ampify_setup.exe` in the new folder. Run from
inside the zip, the installer reports a missing file. If Windows shows "Windows protected your PC", click "More info",
then "Run anyway": the files are not code-signed yet. If Apple Music is open, the installer offers to close it.

**From PowerShell.** Open it (Start menu, type "PowerShell") and paste:

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
```

The line downloads the latest release, checks its SHA-256 and runs the installer. To read the script before it runs,
save it, open it, then start it yourself:

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 -OutFile install.ps1
notepad install.ps1
powershell -ExecutionPolicy Bypass -File install.ps1
```

`-ExecutionPolicy Bypass` lets this one script run without changing Windows' setting.

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

Apple Music looks the same right after the install: Ampify ships no theme, so the first one comes from the Marketplace.

1. Click **Ampify** at the bottom of the sidebar. The Marketplace opens.
2. Under **Themes**, click **Install** on the one you want, for example Glass or Artwork, then **Use**.
3. Pick a color scheme in the menu if the theme has several, for example Dark or Light.

The change shows at once. **Turn off** (on the theme in use) and **Use Apple Music's look** (at the bottom of the
themes list) both go back to the original look, and **Uninstall** deletes a theme you do not use. The Marketplace
reads a [public catalog](https://github.com/Shirowwww/ampify-catalog), so it needs an internet connection; a theme
already installed keeps working without one.

The words used there: a **theme** changes how Apple Music looks, an **extension** adds a feature, an **app** is a page
of its own with an entry in the sidebar, and a **snippet** is a few lines of style turned on besides a theme.
**Scheme** is the light or dark version a theme may offer.

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
follows the Windows display language (Settings > Time & language > Language & region). Traditional Chinese shows the
Simplified texts, and any text not translated yet shows in English. To force a language:

```
ampify config language de
```

Use `en`, `fr`, `de`, `es`, `pt`, `ru`, `ja` or `zh`, then restart Apple Music. `ampify config language ""` follows
Windows again.

## Go back to Apple Music

Three ways, from the lightest:

1. **Only the theme.** In the Marketplace, click **Turn off** on the theme in use, or **Use Apple Music's look**.
2. **Ampify off for a while**, nothing removed. There is no button yet: in a terminal run `ampify restore`, and
   `ampify apply` to turn it back on.
3. **Uninstall.** Settings > Apps > Installed apps > "Ampify for Apple Music" (see [Uninstall](#uninstall)).

```
ampify restore     # Apple Music starts without Ampify from now on
ampify apply       # Ampify loads with Apple Music again
```

An Apple Music already open keeps Ampify until you close it. Your themes, extensions and settings stay.

## Safe mode and new Apple Music versions

- If Apple Music crashes or freezes at start twice in a row, Ampify turns itself off for the next start. Once Apple
  Music has run for 20 seconds, Ampify tries again at the following start.
- Ampify only loads on Apple Music versions it was tested with. Apple Music updates itself through the Microsoft
  Store, so after an update Ampify can stay off until the new version is tested. Apple Music then opens as usual, without
  your theme, and nothing is broken.

Either way, Windows shows one notification the first time it happens ("Ampify is off for now", or "Ampify skipped this
start"), once for each new Apple Music version. When it says Ampify got a new list of tested versions, close Apple
Music completely (also from the notification area), then open it again. Otherwise Ampify comes back once the new version
is tested: a new Ampify release brings it (updates install themselves), and `ampify versions update` looks for the
list now. `ampify doctor` shows the same state at any time, and `ampify notice safe-mode --dry-run` prints what a
notification says.

## Update

Ampify updates itself. Once a day, while Apple Music is closed, it looks for a new release, checks its SHA-256,
installs it and shows a notification; it loads with the next Apple Music start. If you keep Apple Music open in the
notification area, the update waits until you quit it or restart the PC. In Apple Music's **Settings > Ampify >
Ampify updates**, choose to have it ask first, or never update (`ampify config kit_update ask` or `off`).

To update by hand, run the install line again, or `ampify update --apply` with Apple Music closed. Every way keeps your
settings.

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
