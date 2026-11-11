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
then "Run anyway": the files are not code-signed yet. The installer shows what it is about to do, then each step as it goes;
if Apple Music is open, it closes it first. On an Apple Music version not tested yet, it says so and installs anyway:
Ampify then stays off in Apple Music until that version is tested (see
[Safe mode and new Apple Music versions](#safe-mode-and-new-apple-music-versions)).

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

- a start-up entry (Task Manager > Startup apps) that runs the launcher at each sign-in. It tells Windows to start the
  launcher with Apple Music, so Ampify loads when Apple Music opens, checks once for an update, and ends: nothing of
  Ampify runs in the background. Where Windows refuses that setting, the launcher stays running and waits for Apple
  Music instead;
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
2. Under **Themes**, click **Install** on the one you want, for example Glass or Artwork: it is in use at once.
3. Pick a color scheme in the menu if the theme has several, for example Dark or Light.

The change shows at once. **Turn off** (on the theme in use) and **Use Apple Music's look** (at the bottom of the
themes list) both go back to the original look, and **Uninstall** deletes a theme you do not use. The Marketplace
reads a [public catalog](https://github.com/Shirowwww/ampify-catalog), so it needs an internet connection; a theme
already installed keeps working without one.

The words used there: a **theme** changes how Apple Music looks, an **extension** adds a feature, an **app** is a page
of its own with an entry in the sidebar, and a **snippet** is a few lines of style turned on besides a theme. The
**Developers** tab holds the examples for people who make extensions.
**Scheme** is the light or dark version a theme may offer.

The playback bar's place is one choice for every theme, in Settings > Ampify > Look > Playback bar, under the theme.
"As the theme plans" keeps each theme's own place; At the top, At the bottom and "At the bottom, progress under the
buttons" (the line runs under the buttons, between the elapsed time and the song's length) hold under every theme.

## Extensions

Open Apple Music's Settings and click **Ampify**, or click **Settings** in the Marketplace. Ampify's settings page has
five tabs: **Look** (theme, accent colour, glass, and the settings of the active theme and snippets), **Window**,
**Player**, **Sound and extensions** (one switch per extension) and **Advanced** (network, reliability, updates and
help). The settings of a theme, a snippet or an extension stay folded under its name until you open them. The search
box above the tabs looks through every tab at once, those folded settings included. Changes take effect at once.

Extensions you install start switched off, and you see the permissions an extension asks for before you switch it on.
Five ship with Ampify: Now Playing, the Song panel (a column about the song playing, opened from a button of the
playback bar), Time on hover (the time of the song under the pointer on the progress line, on from the start), the
playback guard (restarts a song Apple Music leaves stuck, on from the start) and the Atmos
leveler, under **Sound**: Dolby Atmos songs often play quieter than stereo ones, and the
leveler brings them to the same loudness so you do not reach for the volume between songs. It is off until you switch
it on.

## Languages

Ampify speaks English, French, German, Spanish, Portuguese (Brazilian), Russian, Japanese, Simplified Chinese, Italian and Korean. It
follows the Windows display language (Settings > Time & language > Language & region). Traditional Chinese shows the
Simplified texts, and any text not translated yet shows in English. To force a language:

```
ampify config language de
```

Use `en`, `fr`, `de`, `es`, `pt`, `ru`, `ja`, `zh`, `it` or `ko`, then restart Apple Music. `ampify config language ""` follows
Windows again.

## Go back to Apple Music

Three ways, from the lightest:

1. **Only the theme.** In the Marketplace, click **Turn off** on the theme in use, or **Use Apple Music's look**.
2. **Ampify off for a while**, nothing removed. There is no button yet: in a terminal run `ampify restore`, and
   `ampify apply` to turn it back on.
3. **Uninstall.** `ampify uninstall`, or Settings > Apps > Installed apps > "Ampify for Apple Music" (see
   [Uninstall](#uninstall)).

```
ampify restore     # Apple Music starts without Ampify from now on
ampify apply       # Ampify loads with Apple Music again
```

An Apple Music already open keeps Ampify until you close it. Your themes, extensions and settings stay.

## Safe mode and new Apple Music versions

- If Apple Music crashes or freezes at start twice in a row, Ampify turns itself off for the next start. Once Apple
  Music has run for 8 seconds with its window responding, Ampify tries again at the following start.
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
notification area, the update waits until you quit it or restart the PC. In Apple Music's **Settings > Ampify > Advanced >
Ampify updates**, choose to have it ask first, or never update (`ampify config kit_update ask` or `off`). "Ask me
first" never opens a window that waits for you: a notification offers the new version, a click on it installs it
(at once, or as soon as Apple Music is closed), and a notification left alone comes back only a week later.

To update by hand, run the install line again, or `ampify update --apply` with Apple Music closed. Every way keeps your
settings.

Your themes, snippets and extensions follow. At the first start of a new release, Ampify moves each element from the
catalog to its new version, and replaces a retired snippet with the one that took it in, your settings included.
Without internet, they stay as they were and the next start tries again. `ampify update --elements` does it by hand.

## Uninstall

Run `ampify uninstall` in a terminal, or open Settings > Apps > Installed apps, find "Ampify for Apple Music", click
the three dots, then Uninstall. Both open the same uninstaller: it removes the program, its start with Windows, its
loading in Apple Music and its entry in Settings > Apps, closing Apple Music first if it is open. A box, unticked, offers
to delete your themes, extensions and settings in `%USERPROFILE%\Ampify` too. Apple Music then opens and plays as
before. `ampify uninstall --yes` removes Ampify without a window and keeps your files (`--delete-data` deletes
them).

## Something is wrong

1. Run `ampify doctor`: it checks Ampify and lists each problem.
2. Run `ampify report`: it writes an anonymised zip of the state and the logs in `%USERPROFILE%\Ampify\reports`
   (the **Export a report** button of Ampify's settings page does the same). Look through it, then attach it to an
   [issue](https://github.com/Shirowwww/ampify/issues).

The logs are in `%USERPROFILE%\Ampify\logs\`. If an antivirus blocks a file, note the threat name and allow nothing
until you know what it is.
